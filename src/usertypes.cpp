#include "usertypes.hpp"

#include "comments.hpp"
#include "resolve.hpp"
#include "types.hpp"

#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/Basic/LangOptions.h>

#include <algorithm>
#include <map>
#include <vector>

namespace dd {

    namespace {

        // Type names as people write them, for bases.
        std::string typeName(clang::QualType type, const clang::ASTContext& context) {
            clang::PrintingPolicy policy(context.getLangOpts());
            policy.SuppressTagKeyword = true;
            return type.getAsString(policy);
        }

        // True when a function's first parameter receives the object: its class is record or one of its bases.
        bool takesObjectFirst(const clang::FunctionDecl& function, const clang::CXXRecordDecl* record) {
            if (record == nullptr || function.getNumParams() == 0 || !record->hasDefinition()) {
                return false;
            }
            const clang::CXXRecordDecl* first =
                function.getParamDecl(0)->getType().getNonReferenceType()->getAsCXXRecordDecl();
            if (first == nullptr) {
                return false;
            }
            return first->getCanonicalDecl() == record->getCanonicalDecl() || record->isDerivedFrom(first);
        }

        // How function receives the object when bound as a member of record.
        SelfKind selfOf(const clang::FunctionDecl& function, const clang::CXXRecordDecl* record) {
            const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(&function);
            if (method != nullptr && !method->getParent()->isLambda() && method->isInstance()) {
                return SelfKind::Implicit;
            }
            return takesObjectFirst(function, record) ? SelfKind::FirstParam : SelfKind::None;
        }

        // Signatures of the functions e binds, with their self kind.
        std::vector<FunctionSig> memberSignatures(const clang::Expr* e, const clang::CXXRecordDecl* record,
                                                  const SourceRoot& root, const clang::ASTContext& context) {
            std::vector<FunctionSig> sigs;
            for (const BoundFunction& bound : boundFunctions(e)) {
                FunctionSig sig = signatureOf(bound, root, context);
                sig.self = selfOf(*bound.function, record);
                sigs.push_back(std::move(sig));
            }
            return sigs;
        }

        // The field or static member `&T::x` names, or nullptr.
        const clang::ValueDecl* addressedMember(const clang::Expr* e) {
            const auto* address = llvm::dyn_cast<clang::UnaryOperator>(e);
            if (address == nullptr || address->getOpcode() != clang::UO_AddrOf) {
                return nullptr;
            }
            const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(address->getSubExpr()->IgnoreParenImpCasts());
            const clang::ValueDecl* decl = reference == nullptr ? nullptr : reference->getDecl();
            return llvm::isa_and_nonnull<clang::FieldDecl, clang::IndirectFieldDecl, clang::VarDecl>(decl) ? decl
                                                                                                           : nullptr;
        }

        // Type and comment of a field-like member.
        void describeField(Member& member, const clang::ValueDecl& field, const clang::ASTContext& context) {
            member.type = classifyType(field.getType(), context);
            const auto* indirect = llvm::dyn_cast<clang::IndirectFieldDecl>(&field);
            const clang::Decl& documented =
                indirect != nullptr ? static_cast<const clang::Decl&>(*indirect->getAnonField()) : field;
            member.declDoc = docOfDecl(documented, context);
        }

        // The first field of object that statement reads (`s.leader` in `return s.leader + 1;`), or nullptr.
        const clang::FieldDecl* fieldRead(const clang::Stmt* statement, const clang::ParmVarDecl* object) {
            if (statement == nullptr) {
                return nullptr;
            }
            if (const auto* member = llvm::dyn_cast<clang::MemberExpr>(statement)) {
                const auto* base = llvm::dyn_cast<clang::DeclRefExpr>(member->getBase()->IgnoreParenImpCasts());
                const auto* field = llvm::dyn_cast<clang::FieldDecl>(member->getMemberDecl());
                if (base != nullptr && base->getDecl() == object && field != nullptr) {
                    return field;
                }
            }
            for (const clang::Stmt* child : statement->children()) {
                if (const clang::FieldDecl* field = fieldRead(child, object)) {
                    return field;
                }
            }
            return nullptr;
        }

        // A property getter `[](Items& s) { return s.leader + 1; }` is documented by the field it reads, like the old
        // parser does.
        Doc getterFieldDoc(const clang::Expr* getter, const clang::ASTContext& context) {
            const std::vector<BoundFunction> functions = boundFunctions(getter);
            if (functions.empty() || functions.front().function->getNumParams() == 0) {
                return {};
            }
            const clang::FunctionDecl& function = *functions.front().function;
            const clang::FieldDecl* field = fieldRead(function.getBody(), function.getParamDecl(0));
            return field == nullptr ? Doc{} : docOfDecl(*field, context);
        }

        // sol::property(get[, set]): accessor signatures and the property type (getter result, else setter value).
        void describeProperty(Member& member, const clang::CallExpr& call, const clang::CXXRecordDecl* record,
                              const SourceRoot& root, const clang::ASTContext& context) {
            member.kind = MemberKind::Property;
            if (call.getNumArgs() >= 1) {
                member.getter = memberSignatures(call.getArg(0), record, root, context);
                if (member.declDoc.lines.empty()) {
                    member.declDoc = getterFieldDoc(call.getArg(0), context);
                }
            }
            if (call.getNumArgs() >= 2) {
                member.setter = memberSignatures(call.getArg(1), record, root, context);
            }
            if (!member.getter.empty() && !member.getter.front().returns.empty()) {
                member.type = member.getter.front().returns.front();
            }
            else if (!member.setter.empty() && !member.setter.front().params.empty()) {
                member.type = member.setter.front().params.back().type;
            }
        }

        // The value inside sol::readonly / sol::var: a field (`&T::f`) or any other value.
        void describeWrapped(Member& member, const clang::Expr* wrapped, const clang::ASTContext& context) {
            const Definition inner = definitionOf(wrapped);
            if (const clang::ValueDecl* field = addressedMember(inner.expr)) {
                describeField(member, *field, context);
                return;
            }
            member.type = classifyType(inner.expr->getType(), context);
        }

        // sol::property, sol::readonly and sol::var; returns false for other calls.
        bool describeWrapper(Member& member, const clang::Expr* e, const clang::CXXRecordDecl* record,
                             const SourceRoot& root, const clang::ASTContext& context) {
            const auto* call = llvm::dyn_cast<clang::CallExpr>(e);
            const std::string name = call == nullptr ? std::string() : calleeName(*call);
            if (name == "sol::property") {
                describeProperty(member, *call, record, root, context);
                return true;
            }
            if ((name == "sol::readonly" || name == "sol::var") && call->getNumArgs() == 1) {
                member.kind = name == "sol::readonly" ? MemberKind::Readonly : MemberKind::Value;
                describeWrapped(member, call->getArg(0), context);
                return true;
            }
            return false;
        }

        // Constructors named by a sol::constructors<Sig...> list: the matching C++ constructor when declared (it
        // has parameter names and a comment), else the signature's types.
        void addConstructorList(Usertype& usertype, clang::QualType list, const clang::CXXRecordDecl* record,
                                const SourceRoot& root, const clang::ASTContext& context);

        // sol::initializers(f, ...) / sol::factories(f, ...): each function is a constructor.
        void addInitializers(Usertype& usertype, const clang::CallExpr& call, const SourceRoot& root,
                             const clang::ASTContext& context) {
            for (const clang::Expr* function : call.arguments()) {
                for (FunctionSig& sig : memberSignatures(function, nullptr, root, context)) {
                    usertype.constructors.push_back(std::move(sig));
                }
            }
        }

        // Adds the constructors arg declares (constructor list, sol::initializers, sol::no_constructor); returns
        // false when arg is something else.
        bool addConstructors(Usertype& usertype, const clang::Expr* arg, const clang::CXXRecordDecl* record,
                             const SourceRoot& root, const clang::ASTContext& context) {
            const clang::Expr* value = definitionOf(arg).expr;
            const std::string type = templateName(value->getType());
            if (type == "sol::constructor_list") {
                addConstructorList(usertype, value->getType(), record, root, context);
                return true;
            }
            if (type == "sol::constructor_wrapper" || type == "sol::factory_wrapper") {
                if (const auto* call = llvm::dyn_cast<clang::CallExpr>(value)) {
                    addInitializers(usertype, *call, root, context);
                }
                return true;
            }
            if (recordName(value->getType()) == "sol::no_construction") {
                usertype.noConstructor = true;
                return true;
            }
            return false;
        }

        // True when the parameter types of constructor equal those of signature.
        bool sameParams(const clang::CXXConstructorDecl& constructor, const clang::FunctionProtoType& signature) {
            if (constructor.getNumParams() != signature.getNumParams()) {
                return false;
            }
            for (unsigned i = 0; i < constructor.getNumParams(); ++i) {
                if (constructor.getParamDecl(i)->getType().getCanonicalType() !=
                    signature.getParamType(i).getCanonicalType()) {
                    return false;
                }
            }
            return true;
        }

        // The constructor of record declared with signature's parameters, or nullptr.
        const clang::CXXConstructorDecl* declaredConstructor(const clang::CXXRecordDecl* record,
                                                             const clang::FunctionProtoType& signature) {
            if (record == nullptr || !record->hasDefinition()) {
                return nullptr;
            }
            for (const clang::CXXConstructorDecl* constructor : record->ctors()) {
                if (sameParams(*constructor, signature)) {
                    return constructor;
                }
            }
            return nullptr;
        }

        void addConstructorList(Usertype& usertype, clang::QualType list, const clang::CXXRecordDecl* record,
                                const SourceRoot& root, const clang::ASTContext& context) {
            for (const clang::QualType sigType : templateTypeArguments(list)) {
                const auto* signature = sigType->getAs<clang::FunctionProtoType>();
                if (signature == nullptr) {
                    continue;
                }
                FunctionSig sig;
                if (const clang::CXXConstructorDecl* constructor = declaredConstructor(record, *signature)) {
                    sig = signatureOf(*constructor, root, context);
                }
                else {
                    for (const clang::QualType param : signature->getParamTypes()) {
                        sig.params.emplace_back().type = classifyType(param, context);
                    }
                    sig.cppTarget = usertype.cppType;
                }
                sig.returns = {classifyType(signature->getReturnType(), context)};
                usertype.constructors.push_back(std::move(sig));
            }
        }

        // The Lua key of a new_usertype argument: a string literal, or a sol::meta_function value.
        struct UsertypeKey {
            // The key text; empty when the argument is not a key.
            std::string name;
            // A sol::meta_function key.
            bool meta = false;
        };

        // Reads a key argument of new_usertype.
        UsertypeKey keyOf(const clang::Expr* arg) {
            if (std::optional<std::string> name = stringValue(arg)) {
                return {std::move(*name), false};
            }
            if (std::optional<std::string> name = enumeratorName(arg)) {
                return {std::move(*name), true};
            }
            return {};
        }

        // Adds one `key, value` argument pair of new_usertype: bases, constructors, or a member.
        void addPair(Usertype& usertype, const clang::Expr* key, const clang::Expr* value,
                     const clang::CXXRecordDecl* record, const SourceRoot& root, const clang::ASTContext& context) {
            if (addBasesOrConstructors(usertype, {key, value}, record, root, context)) {
                return;
            }
            const UsertypeKey name = keyOf(key);
            const std::optional<SourceLoc> loc = root.locate(context.getSourceManager(), key->getBeginLoc());
            if (name.name.empty() || !loc) {
                return;
            }
            Member member = describeMember(value, record, root, context);
            member.luaName = name.name;
            applyDocFallbacks(member, value->getBeginLoc(), record, context);
            member.meta = name.meta;
            member.loc = *loc;
            member.doc = docBefore(key->getBeginLoc(), context);
            usertype.members.emplace(MemberKey{member.luaName, member.loc}, std::move(member));
        }

        // The Lua name given as the first new_usertype argument.
        std::string usertypeName(const clang::CXXMemberCallExpr& call, const clang::ASTContext& context) {
            if (call.getNumArgs() == 0) {
                return {};
            }
            const auto* literal = llvm::dyn_cast<clang::StringLiteral>(call.getArg(0)->IgnoreImplicit());
            return literal != nullptr ? literal->getString().str() : sourceText(call.getArg(0), context);
        }

        // True when a parameter of type is the object of usertype (its class or one of its bases).
        bool isObjectType(const TypeInfo& type, const std::string& key, const Usertype& usertype) {
            const std::string& canonical = type.canonical.empty() ? type.spelling : type.canonical;
            return canonical == key || std::ranges::find(usertype.bases, canonical) != usertype.bases.end();
        }

        // True when a comment of the bound C++ code documents member already.
        bool isDocumented(const Member& member) {
            const auto documented = [](const FunctionSig& sig) { return !sig.doc.lines.empty(); };
            return !member.declDoc.lines.empty() || std::ranges::any_of(member.overloads, documented) ||
                std::ranges::any_of(member.getter, documented);
        }

        // The member called name of record or of one of its bases, or nullptr.
        const clang::NamedDecl* findMember(const clang::CXXRecordDecl* record, llvm::StringRef name,
                                           const clang::ASTContext& context) {
            if (record == nullptr || !record->hasDefinition()) {
                return nullptr;
            }
            const clang::DeclContextLookupResult found = record->lookup(&context.Idents.get(name));
            if (!found.empty()) {
                return found.front();
            }
            for (const clang::CXXBaseSpecifier& base : record->bases()) {
                if (const clang::NamedDecl* member = findMember(base.getType()->getAsCXXRecordDecl(), name, context)) {
                    return member;
                }
            }
            return nullptr;
        }

        // Member for a function bound through `lua["Name"]["k"]`; only types are known here, so the object is
        // recognized by the first parameter's type.
        Member memberFromFunction(const Function& function, const std::string& key, const Usertype& usertype) {
            Member member;
            member.luaName = function.luaName;
            member.kind = MemberKind::Method;
            member.loc = function.loc;
            member.doc = function.doc;
            member.overloads = function.overloads;
            for (FunctionSig& sig : member.overloads) {
                if (!sig.params.empty() && isObjectType(sig.params.front().type, key, usertype)) {
                    sig.self = SelfKind::FirstParam;
                }
            }
            return member;
        }

        // Member for a value bound through `lua["Name"]["k"]`.
        Member memberFromValue(const Value& value) {
            Member member;
            member.luaName = value.luaName;
            member.loc = value.loc;
            member.doc = value.doc;
            member.type = value.type;
            return member;
        }

        // A usertype with its key in Extraction::usertypes.
        struct OwnerEntry {
            // Key (canonical C++ type).
            const std::string* key;
            // The usertype.
            Usertype* usertype;
        };

        // The usertype owning a binding with these keys and root, or an empty entry.
        template <typename Binding>
        OwnerEntry ownerOf(const Binding& binding, const std::map<std::string, OwnerEntry>& byName) {
            if (binding.owner.size() != 1 || !binding.root.empty()) {
                return {nullptr, nullptr};
            }
            const auto it = byName.find(binding.owner.front());
            return it == byName.end() ? OwnerEntry{nullptr, nullptr} : it->second;
        }

    } // namespace

    std::optional<clang::QualType> usertypeOf(clang::QualType type) {
        if (templateName(type) != "sol::basic_usertype") {
            return std::nullopt;
        }
        const std::vector<clang::QualType> arguments = templateTypeArguments(type);
        return arguments.empty() ? std::nullopt : std::optional(arguments.front());
    }

    std::string usertypeKey(clang::QualType type) {
        // Printed like TypeInfo::canonical (C++, no `struct` keyword), so parameter types can be compared with it.
        clang::LangOptions language;
        language.CPlusPlus = true;
        clang::PrintingPolicy policy(language);
        policy.SuppressTagKeyword = true;
        policy.Bool = true;
        return type.getNonReferenceType().getCanonicalType().getUnqualifiedType().getAsString(policy);
    }

    bool addBasesOrConstructors(Usertype& usertype, const BoundPair& pair, const clang::CXXRecordDecl* record,
                                const SourceRoot& root, const clang::ASTContext& context) {
        const auto [key, value] = pair;
        // sol::base_classes is a `bases<>` value (an alias of sol::base_list); the value after it lists the bases.
        if (key != nullptr && templateName(key->getType()) == "sol::base_list" &&
            templateTypeArguments(key->getType()).empty()) {
            for (const clang::QualType base : templateTypeArguments(definitionOf(value).expr->getType())) {
                usertype.bases.push_back(typeName(base, context));
            }
            return true;
        }
        return addConstructors(usertype, value, record, root, context);
    }

    Member describeMember(const clang::Expr* value, const clang::CXXRecordDecl* record, const SourceRoot& root,
                          const clang::ASTContext& context) {
        Member member;
        const Definition definition = definitionOf(value);
        if (definition.documentedMember != nullptr) {
            member.declDoc = docOfDecl(*definition.documentedMember, context);
        }
        if (describeWrapper(member, definition.expr, record, root, context)) {
            return member;
        }
        if (const clang::ValueDecl* field = addressedMember(definition.expr)) {
            member.kind = MemberKind::Field;
            describeField(member, *field, context);
            return member;
        }
        member.overloads = memberSignatures(definition.expr, record, root, context);
        if (!member.overloads.empty()) {
            member.kind = MemberKind::Method;
            return member;
        }
        member.type = classifyType(definition.expr->getType(), context);
        return member;
    }

    void applyDocFallbacks(Member& member, clang::SourceLocation loc, const clang::CXXRecordDecl* record,
                           const clang::ASTContext& context) {
        // A hint is written on purpose for the docs, so it wins over the bound code's own comments.
        const std::optional<std::string> hinted = autodocHint(loc, context);
        const clang::NamedDecl* hintedDecl = hinted ? findMember(record, *hinted, context) : nullptr;
        if (hintedDecl != nullptr) {
            member.declDoc = docOfDecl(*hintedDecl, context);
            return;
        }
        if (isDocumented(member)) {
            return;
        }
        if (const clang::NamedDecl* decl = findMember(record, member.luaName, context)) {
            member.declDoc = docOfDecl(*decl, context);
        }
    }

    void addUsertype(const clang::CXXMemberCallExpr& call, const SourceRoot& root, const clang::ASTContext& context,
                     Extraction& out) {
        const clang::TemplateArgumentList* arguments = call.getMethodDecl()->getTemplateSpecializationArgs();
        const std::optional<SourceLoc> loc = root.locate(context.getSourceManager(), call.getBeginLoc());
        if (arguments == nullptr || arguments->size() == 0 ||
            arguments->get(0).getKind() != clang::TemplateArgument::Type || !loc) {
            return;
        }
        const clang::QualType type = arguments->get(0).getAsType();
        Usertype& usertype = out.usertypes[usertypeKey(type)];
        if (!usertype.luaName.empty()) {
            return; // seen from another translation unit
        }
        const clang::CXXRecordDecl* record = type->getAsCXXRecordDecl();
        usertype.luaName = usertypeName(call, context);
        usertype.cppType = typeName(type, context);
        usertype.loc = *loc;
        usertype.doc = docBefore(call.getBeginLoc(), context);
        usertype.declDoc = record == nullptr ? Doc{} : docOfDecl(*record, context);
        unsigned i = 1;
        while (i < call.getNumArgs()) {
            if (addConstructors(usertype, call.getArg(i), record, root, context)) {
                ++i;
                continue;
            }
            if (i + 1 < call.getNumArgs()) {
                addPair(usertype, call.getArg(i), call.getArg(i + 1), record, root, context);
            }
            i += 2;
        }
    }

    void attachOwnedBindings(Extraction& extraction) {
        std::map<std::string, OwnerEntry> byName;
        for (auto& [key, usertype] : extraction.usertypes) {
            byName.emplace(usertype.luaName, OwnerEntry{&key, &usertype});
        }
        std::erase_if(extraction.functions, [&byName](const auto& entry) {
            const OwnerEntry owner = ownerOf(entry.second, byName);
            if (owner.usertype != nullptr) {
                owner.usertype->members.emplace(MemberKey{entry.second.luaName, entry.second.loc},
                                                memberFromFunction(entry.second, *owner.key, *owner.usertype));
            }
            return owner.usertype != nullptr;
        });
        std::erase_if(extraction.values, [&byName](const auto& entry) {
            const OwnerEntry owner = ownerOf(entry.second, byName);
            if (owner.usertype != nullptr) {
                owner.usertype->members.emplace(MemberKey{entry.second.luaName, entry.second.loc},
                                                memberFromValue(entry.second));
            }
            return owner.usertype != nullptr;
        });
    }

} // namespace dd
