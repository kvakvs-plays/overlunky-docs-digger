#include "callbacks.hpp"

#include "comments.hpp"
#include "resolve.hpp"
#include "types.hpp"

#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/StmtCXX.h>
#include <clang/AST/TypeLoc.h>
#include <clang/ASTMatchers/ASTMatchers.h>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dd {

    namespace {

        using namespace clang::ast_matchers;

        // Binding names of the matched nodes.
        constexpr const char* FlowNode = "flow";
        constexpr const char* SiteNode = "site";
        constexpr const char* SiteFunctionNode = "site-function";
        constexpr const char* EdgeArgumentNode = "edge-argument";
        constexpr const char* EdgeParamNode = "edge-param";
        constexpr const char* EdgeCallNode = "edge-call";
        constexpr const char* ContainerNode = "container";
        constexpr const char* VtableNode = "vtable";

        // How deep a parameter is followed into the C++ functions it is passed to.
        constexpr int MaxFlowDepth = 3;
        // How many pass-through steps namedDecl follows.
        constexpr int MaxSteps = 8;
        // How far up a dispatch site's guard is looked for (statements up to the enclosing function).
        constexpr int MaxAncestors = 64;

        // nil meanings shared by several rules.
        constexpr const char* Ignored = "ignored";
        constexpr const char* PassedOn = "passed on as an empty optional";
        constexpr const char* NoOverride = "no override: the C++ side keeps its default";

        // True for sol::function and sol::protected_function (sol::function is the latter with safeties on).
        bool isLuaFunction(clang::QualType type) {
            const std::string name = templateName(type);
            return name == "sol::basic_function" || name == "sol::basic_protected_function";
        }

        // The first statement in s (s included, depth first) that matches, or nullptr.
        template <typename Predicate>
        const clang::Stmt* findStmt(const clang::Stmt* s, const Predicate& matches) {
            if (s == nullptr) {
                return nullptr;
            }
            if (matches(*s)) {
                return s;
            }
            for (const clang::Stmt* child : s->children()) {
                if (const clang::Stmt* found = findStmt(child, matches)) {
                    return found;
                }
            }
            return nullptr;
        }

        // The declaration e names after following conversions, std::move, locals and captures; nullptr when e is not a
        // name.
        const clang::ValueDecl* namedDecl(const clang::Expr* e) {
            for (int step = 0; step < MaxSteps && e != nullptr; ++step) {
                const clang::Expr* next = definitionOf(withoutConversions(e)).expr;
                if (next == e) {
                    break;
                }
                e = next;
            }
            const auto* reference = llvm::dyn_cast_or_null<clang::DeclRefExpr>(e);
            return reference == nullptr ? nullptr : reference->getDecl();
        }

        // `Enum::Value` of the enum constant e names (also through a local), or nothing.
        std::optional<std::string> enumConstant(const clang::Expr* e) {
            const auto* constant = llvm::dyn_cast_or_null<clang::EnumConstantDecl>(namedDecl(e));
            const auto* type =
                constant == nullptr ? nullptr : llvm::dyn_cast<clang::EnumDecl>(constant->getDeclContext());
            if (type == nullptr) {
                return std::nullopt;
            }
            return type->getNameAsString() + "::" + constant->getNameAsString();
        }

        // The enum-typed parameter e names, or nullptr.
        const clang::ParmVarDecl* enumParam(const clang::Expr* e) {
            const auto* param = llvm::dyn_cast_or_null<clang::ParmVarDecl>(namedDecl(e));
            return param != nullptr && param->getType()->isEnumeralType() ? param : nullptr;
        }

        // Name of the enum type of an enum parameter (`ON`), or empty.
        std::string enumTypeName(clang::QualType type) {
            const auto* enumType = type.getNonReferenceType()->getAs<clang::EnumType>();
            return enumType == nullptr ? std::string() : enumType->getDecl()->getNameAsString();
        }

        // `Record::field`: the key of a callback storage.
        std::string storageKey(const clang::FieldDecl& field) {
            return field.getParent()->getQualifiedNameAsString() + "::" + field.getNameAsString();
        }

        // The variable a member chain starts from (`callback` in `callback.func`, `overrides[i]->func`), or nullptr.
        const clang::ValueDecl* objectOf(const clang::Expr* e) {
            for (int step = 0; step < MaxSteps && e != nullptr; ++step) {
                e = withoutConversions(e)->IgnoreParenImpCasts();
                if (const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(e)) {
                    return reference->getDecl();
                }
                if (const auto* member = llvm::dyn_cast<clang::MemberExpr>(e)) {
                    e = member->getBase();
                }
                else if (const auto* op = llvm::dyn_cast<clang::CXXOperatorCallExpr>(e); op && op->getNumArgs() > 0) {
                    e = op->getArg(0); // `->` and `[]` of smart pointers and maps
                }
                else {
                    return nullptr;
                }
            }
            return nullptr;
        }

        // The argument as written: implicit nodes, copies and std::move / std::forward removed.
        const clang::Expr* passedValue(const clang::Expr* e) {
            e = e->IgnoreImplicit()->IgnoreParens();
            if (const auto* copy = llvm::dyn_cast<clang::CXXConstructExpr>(e);
                copy != nullptr && copy->getNumArgs() == 1 && copy->getConstructor()->isCopyOrMoveConstructor()) {
                e = copy->getArg(0)->IgnoreImplicit()->IgnoreParens();
            }
            const auto* call = llvm::dyn_cast<clang::CallExpr>(e);
            const std::string name = call == nullptr ? std::string() : calleeName(*call);
            if ((name == "std::move" || name == "std::forward") && call->getNumArgs() == 1) {
                return call->getArg(0)->IgnoreImplicit()->IgnoreParens();
            }
            return e;
        }

        // A callback argument as Lua receives it: named after the variable or member passed, typed by the variable's
        // declaration (typedef names kept) or else by the expression.
        Param argumentParam(const clang::Expr* argument, const clang::ASTContext& context) {
            const clang::Expr* value = passedValue(argument);
            Param param;
            if (const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(value)) {
                param.name = reference->getDecl()->getNameAsString();
                param.type = classifyType(reference->getDecl()->getType(), context);
                return param;
            }
            if (const auto* member = llvm::dyn_cast<clang::MemberExpr>(value)) {
                param.name = member->getMemberDecl()->getNameAsString();
            }
            param.type = classifyType(value->getType(), context);
            return param;
        }

        // The first template argument of a call's callee when it is a type (`handle_function<bool>`,
        // `make_safe_cb<Sig>`).
        std::optional<clang::QualType> firstTemplateType(const clang::CallExpr& call) {
            const clang::FunctionDecl* callee = call.getDirectCallee();
            const clang::TemplateArgumentList* arguments =
                callee == nullptr ? nullptr : callee->getTemplateSpecializationArgs();
            if (arguments == nullptr || arguments->size() == 0 ||
                arguments->get(0).getKind() != clang::TemplateArgument::Type) {
                return std::nullopt;
            }
            return arguments->get(0).getAsType();
        }

        // True for result types whose value is thrown away: void and std::monostate.
        bool ignoresResult(clang::QualType type) { return type->isVoidType() || recordName(type) == "std::monostate"; }

        // What nil becomes when the C++ side needs a value of type (make_safe_cb's `R{}`, sol2's conversions).
        std::string defaultMeaning(const TypeInfo& type) {
            switch (type.kind) {
            case TypeKind::Optional:
                return PassedOn;
            case TypeKind::Bool:
                return "treated as false";
            case TypeKind::Int:
            case TypeKind::Float:
            case TypeKind::Enum:
                return "treated as 0";
            default:
                return "treated as " + type.spelling + "{}";
            }
        }

        // The location of e under the root, or an empty location.
        SourceLoc locationOf(const clang::Stmt& s, const SourceRoot& root, const clang::ASTContext& context) {
            return root.locate(context.getSourceManager(), s.getBeginLoc()).value_or(SourceLoc{});
        }

        // The nearest parent of node that is not an implicit wrapper (cast, temporary, cleanup, parentheses).
        clang::DynTypedNode realParent(clang::DynTypedNode node, clang::ASTContext& context) {
            for (int step = 0; step < MaxSteps; ++step) {
                const clang::DynTypedNodeList parents = context.getParents(node);
                if (parents.empty()) {
                    return {};
                }
                node = parents[0];
                const auto* e = node.get<clang::Expr>();
                if (e == nullptr || (e->IgnoreImplicit() == e && !llvm::isa<clang::ParenExpr>(e))) {
                    return node;
                }
            }
            return node;
        }

        // `X` of a `.value_or(X)` called on the result node is the object of, or nothing.
        std::optional<std::string> valueOrArgument(const clang::DynTypedNode& node, clang::ASTContext& context) {
            const auto* member = node.get<clang::MemberExpr>();
            if (member == nullptr || member->getMemberDecl()->getNameAsString() != "value_or") {
                return std::nullopt;
            }
            const auto* call = realParent(node, context).get<clang::CXXMemberCallExpr>();
            if (call == nullptr || call->getNumArgs() != 1) {
                return std::nullopt;
            }
            return sourceText(call->getArg(0), context);
        }

        // True when e is variable itself (not followed to its initializer).
        bool isVariable(const clang::Expr* e, const clang::VarDecl& variable) {
            const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(e->IgnoreParenImpCasts());
            return reference != nullptr && reference->getDecl() == &variable;
        }

        // `X` of the first `.value_or(X)` on variable in body, or nothing.
        std::optional<std::string> valueOrOnVariable(const clang::Stmt* body, const clang::VarDecl& variable,
                                                     const clang::ASTContext& context) {
            const auto* found =
                llvm::dyn_cast_or_null<clang::CXXMemberCallExpr>(findStmt(body, [&](const clang::Stmt& s) {
                    const auto* call = llvm::dyn_cast<clang::CXXMemberCallExpr>(&s);
                    return call != nullptr && call->getMethodDecl() != nullptr &&
                        call->getMethodDecl()->getNameAsString() == "value_or" && call->getNumArgs() == 1 &&
                        isVariable(call->getImplicitObjectArgument(), variable);
                }));
            return found == nullptr ? std::nullopt : std::optional(sourceText(found->getArg(0), context));
        }

        // What the C++ side does with a nil result kept in variable.
        std::string variableMeaning(const clang::VarDecl& variable, const clang::ASTContext& context) {
            const auto* function = llvm::dyn_cast_or_null<clang::FunctionDecl>(variable.getParentFunctionOrMethod());
            const std::optional<std::string> fallback =
                function == nullptr ? std::nullopt : valueOrOnVariable(function->getBody(), variable, context);
            return fallback ? "treated as " + *fallback : NoOverride;
        }

        // C6: what the C++ side does when the handle_function call returns no value (nil).
        std::string consumerMeaning(const clang::CallExpr& call, clang::ASTContext& context) {
            const clang::DynTypedNode parent = realParent(clang::DynTypedNode::create(call), context);
            if (std::optional<std::string> fallback = valueOrArgument(parent, context)) {
                return "treated as " + *fallback;
            }
            if (const auto* variable = parent.get<clang::VarDecl>()) {
                return variableMeaning(*variable, context);
            }
            return parent.get<clang::ReturnStmt>() != nullptr ? PassedOn : NoOverride;
        }

        // Arguments, result and nil meaning at a `handle_function<R>(backend, f, args...)` call.
        CallbackSig handleFunctionSignature(const clang::CallExpr& call, const SourceRoot& root,
                                            clang::ASTContext& context) {
            CallbackSig sig;
            sig.origin = CallbackOrigin::HandleFunction;
            for (unsigned i = 2; i < call.getNumArgs(); ++i) {
                sig.params.push_back(argumentParam(call.getArg(i), context));
            }
            sig.nilAllowed = true;
            const std::optional<clang::QualType> result = firstTemplateType(call);
            if (!result || ignoresResult(*result)) {
                sig.nilMeaning = Ignored;
            }
            else {
                sig.returns = classifyReturns(*result, context);
                sig.nilMeaning = consumerMeaning(call, context);
            }
            sig.loc = locationOf(call, root, context);
            return sig;
        }

        // The value a binder passes for a bound object of type: a lambda's call result, otherwise the object itself.
        clang::QualType boundValueType(clang::QualType type) {
            const clang::CXXRecordDecl* record = type->getAsCXXRecordDecl();
            const clang::CXXMethodDecl* call =
                record != nullptr && record->isLambda() ? record->getLambdaCallOperator() : nullptr;
            return call == nullptr ? type : call->getReturnType();
        }

        // Appends the values of the binder arguments (`FrontBinder{...}` or `BackBinder{...}`) of call.
        void addBinderParams(CallbackSig& sig, const clang::CallExpr& call, llvm::StringRef binder, Binder kind,
                             const clang::ASTContext& context) {
            for (const clang::Expr* argument : call.arguments()) {
                if (templateName(argument->getType()) != binder) {
                    continue;
                }
                for (const clang::QualType bound : templateTypeArguments(argument->getType())) {
                    Param& param = sig.params.emplace_back();
                    param.type = classifyType(boundValueType(bound.getNonReferenceType()), context);
                    param.binder = kind;
                }
            }
        }

        // C2: `make_safe_cb<R(A...)>(cb, FrontBinder{...}, BackBinder{...})`: front values, A..., back values; nil per
        // the make_safe_cb rule for R.
        CallbackSig safeCbSignature(const clang::CallExpr& call, const SourceRoot& root,
                                    const clang::ASTContext& context) {
            CallbackSig sig;
            const std::optional<clang::QualType> signature = firstTemplateType(call);
            const auto* proto = signature ? (*signature)->getAs<clang::FunctionProtoType>() : nullptr;
            if (proto == nullptr) {
                sig.unresolved = "make_safe_cb without a function signature";
                return sig;
            }
            sig.origin = CallbackOrigin::MakeSafeCb;
            addBinderParams(sig, call, "FrontBinder", Binder::Front, context);
            for (const clang::QualType param : proto->getParamTypes()) {
                sig.params.emplace_back().type = classifyType(param, context);
            }
            addBinderParams(sig, call, "BackBinder", Binder::Back, context);
            sig.returns = classifyReturns(proto->getReturnType(), context);
            sig.nilAllowed = true;
            sig.nilMeaning = sig.returns.empty() ? Ignored : defaultMeaning(sig.returns.front());
            sig.loc = locationOf(call, root, context);
            return sig;
        }

        // `f(args)` on the parameter itself: the arguments, and the type the result is converted to (sol2 converts
        // nil like make_safe_cb's `R{}` for bool and numbers).
        CallbackSig directCallSignature(const clang::CXXOperatorCallExpr& call, const SourceRoot& root,
                                        clang::ASTContext& context) {
            CallbackSig sig;
            sig.origin = CallbackOrigin::DirectCall;
            for (unsigned i = 1; i < call.getNumArgs(); ++i) {
                sig.params.push_back(argumentParam(call.getArg(i), context));
            }
            const auto* member = realParent(clang::DynTypedNode::create(call), context).get<clang::MemberExpr>();
            const auto* conversion =
                member == nullptr ? nullptr : llvm::dyn_cast<clang::CXXConversionDecl>(member->getMemberDecl());
            if (conversion != nullptr) {
                sig.returns = classifyReturns(conversion->getConversionType(), context);
            }
            sig.nilAllowed = true;
            sig.nilMeaning = sig.returns.empty() ? Ignored : defaultMeaning(sig.returns.front());
            sig.loc = locationOf(call, root, context);
            return sig;
        }

        // The container field call adds an element to (`callbacks.push_back(x)`), or empty.
        std::string addedTo(const clang::CXXMemberCallExpr& call) {
            const clang::CXXMethodDecl* method = call.getMethodDecl();
            const std::string name = method == nullptr ? std::string() : method->getNameAsString();
            if (name != "push_back" && name != "emplace_back" && name != "insert" && name != "emplace") {
                return {};
            }
            const auto* member = llvm::dyn_cast<clang::MemberExpr>(call.getImplicitObjectArgument()->IgnoreImplicit());
            const auto* field = member == nullptr ? nullptr : llvm::dyn_cast<clang::FieldDecl>(member->getMemberDecl());
            return field == nullptr ? std::string() : storageKey(*field);
        }

        // The container field a record built by e is added to (`callbacks.push_back(Record{...})`), or empty.
        std::string pushedInto(const clang::Expr& e, clang::ASTContext& context) {
            clang::DynTypedNode node = clang::DynTypedNode::create(e);
            for (int step = 0; step < MaxSteps && node.get<clang::Expr>() != nullptr; ++step) {
                node = realParent(node, context);
                if (const auto* call = node.get<clang::CXXMemberCallExpr>()) {
                    return addedTo(*call);
                }
            }
            return {};
        }

        // An Unresolved callback with the reason.
        CallbackSig unresolved(std::string reason) {
            CallbackSig sig;
            sig.unresolved = std::move(reason);
            return sig;
        }

        // What the per-TU passes share.
        struct Scope {
            // Decides which code is reported.
            const SourceRoot& root;
            // The translation unit.
            clang::ASTContext& context;
            // Where facts go.
            CallbackFacts& facts;
        };

        // Follows one sol::function parameter through a function body and takes the first pattern of the strongest
        // kind it reaches (plan C2, C3; Use orders the kinds).
        class FlowScan {
        public:
            // Scans function for param; depth counts the calls followed so far.
            FlowScan(const clang::ParmVarDecl& param, const clang::FunctionDecl& function, const Scope& scope,
                     int depth) : param_(param), function_(function), scope_(scope), depth_(depth) {}

            // The callback the parameter's flow gives.
            CallbackSig run() {
                visit(function_.getBody());
                return found_ ? std::move(*found_) : unresolved("no use of the parameter matches a known pattern");
            }

        private:
            // Kinds of use, strongest first: the strongest one found decides.
            enum class Use : std::uint8_t {
                SafeCb,
                HandleFunction,
                DirectCall,
                Stored,
                Followed,
                ForwardedToLua,
                PassedOn,
            };

            // True when e names the parameter (also through moves, locals and captures).
            bool refers(const clang::Expr* e) const { return e != nullptr && namedDecl(e) == &param_; }

            // Keeps callback when its use is stronger than the one found so far.
            void consider(Use use, CallbackSig callback) {
                if (!found_ || use < use_) {
                    use_ = use;
                    found_ = std::move(callback);
                }
            }

            // Checks s and everything below it.
            void visit(const clang::Stmt* s) {
                if (s == nullptr) {
                    return;
                }
                if (const auto* call = llvm::dyn_cast<clang::CallExpr>(s)) {
                    checkCall(*call);
                }
                else if (const auto* list = llvm::dyn_cast<clang::InitListExpr>(s)) {
                    checkInitList(*list);
                }
                else if (const auto* binary = llvm::dyn_cast<clang::BinaryOperator>(s);
                         binary != nullptr && binary->getOpcode() == clang::BO_Assign) {
                    checkAssignment(*binary->getLHS(), binary->getRHS());
                }
                for (const clang::Stmt* child : s->children()) {
                    visit(child);
                }
            }

            // make_safe_cb, handle_function, operators, and other calls the parameter is passed to.
            void checkCall(const clang::CallExpr& call) {
                if (const auto* op = llvm::dyn_cast<clang::CXXOperatorCallExpr>(&call)) {
                    checkOperator(*op);
                    return;
                }
                const std::string name = calleeName(call);
                if (!checkCallbackCall(call, name) && name != "std::move" && name != "std::forward") {
                    checkPassed(call);
                }
            }

            // make_safe_cb or handle_function on the parameter; false for other calls.
            bool checkCallbackCall(const clang::CallExpr& call, const std::string& name) {
                if ((name == "make_safe_cb" || name == "make_safe_clearable_cb") && call.getNumArgs() >= 1 &&
                    refers(call.getArg(0))) {
                    consider(Use::SafeCb, safeCbSignature(call, scope_.root, scope_.context));
                    return true;
                }
                if (name == "handle_function" && call.getNumArgs() >= 2 && refers(call.getArg(1))) {
                    consider(Use::HandleFunction, handleFunctionSignature(call, scope_.root, scope_.context));
                    return true;
                }
                return false;
            }

            // `x = f`, `f(args)` and `lua["a"]["b"](..., f)`.
            void checkOperator(const clang::CXXOperatorCallExpr& op) {
                if (op.getOperator() == clang::OO_Equal && op.getNumArgs() == 2) {
                    checkAssignment(*op.getArg(0), op.getArg(1));
                }
                else if (op.getOperator() == clang::OO_Call && op.getNumArgs() > 0) {
                    if (refers(op.getArg(0))) {
                        consider(Use::DirectCall, directCallSignature(op, scope_.root, scope_.context));
                    }
                    else {
                        checkForwardedToLua(op);
                    }
                }
            }

            // `Record{..., f, ...}`: stored in the field the parameter initializes.
            void checkInitList(const clang::InitListExpr& list) {
                const clang::InitListExpr* semantic = list.isSemanticForm() ? &list : list.getSemanticForm();
                const auto* record = semantic == nullptr ? nullptr : semantic->getType()->getAsCXXRecordDecl();
                if (record == nullptr || !record->hasDefinition()) {
                    return;
                }
                unsigned index = record->getNumBases(); // bases are initialized first
                for (const clang::FieldDecl* field : record->fields()) {
                    if (index >= semantic->getNumInits()) {
                        return;
                    }
                    if (refers(semantic->getInit(index++))) {
                        stored(*field, pushedInto(list, scope_.context));
                        return;
                    }
                }
            }

            // `object.field = f` stores it; an assignment to anything else (a Lua table key) ends the trace.
            void checkAssignment(const clang::Expr& target, const clang::Expr* value) {
                if (!refers(value)) {
                    return;
                }
                const auto* member = llvm::dyn_cast<clang::MemberExpr>(target.IgnoreImplicit()->IgnoreParens());
                const auto* field =
                    member == nullptr ? nullptr : llvm::dyn_cast<clang::FieldDecl>(member->getMemberDecl());
                if (field != nullptr) {
                    stored(*field, {});
                    return;
                }
                consider(Use::PassedOn, unresolved("assigned to " + sourceText(&target, scope_.context)));
            }

            // Stored in field (of a record added to container, when known): dispatched elsewhere; the function's enum
            // parameters may select the signature.
            void stored(const clang::FieldDecl& field, const std::string& container) {
                CallbackSig sig;
                sig.origin = CallbackOrigin::StoredHandleFunction;
                sig.storedIn = storageKey(field);
                for (const clang::ParmVarDecl* other : function_.parameters()) {
                    if (const std::string type = enumTypeName(other->getType()); !type.empty()) {
                        scope_.facts.storageSelectors[sig.storedIn].insert(type);
                    }
                }
                if (!container.empty()) {
                    sig.storedIn += " in " + container;
                }
                consider(Use::Stored, std::move(sig));
            }

            // `g(..., f, ...)`: followed into g when this translation unit defines it, else the trace ends there.
            void checkPassed(const clang::CallExpr& call) {
                for (unsigned i = 0; i < call.getNumArgs(); ++i) {
                    if (refers(call.getArg(i))) {
                        follow(call, i);
                        return;
                    }
                }
            }

            // Follows the parameter into argument index of call.
            void follow(const clang::CallExpr& call, unsigned index) {
                const clang::FunctionDecl* callee = call.getDirectCallee();
                const clang::FunctionDecl* definition = nullptr;
                const bool known = callee != nullptr && callee->hasBody(definition) &&
                    index < definition->getNumParams() && isLuaFunction(definition->getParamDecl(index)->getType());
                if (known && depth_ < MaxFlowDepth) {
                    consider(Use::Followed,
                             FlowScan(*definition->getParamDecl(index), *definition, scope_, depth_ + 1).run());
                    return;
                }
                const std::string name = callee == nullptr ? std::string("a call") : calleeName(call);
                consider(Use::PassedOn, unresolved("passed to " + name));
            }

            // `lua["Entity"]["set_pre_x"](ent, f)`: the callback of that Lua function, copied after all TUs.
            void checkForwardedToLua(const clang::CXXOperatorCallExpr& call) {
                for (unsigned i = 1; i < call.getNumArgs(); ++i) {
                    if (!refers(call.getArg(i))) {
                        continue;
                    }
                    KeyPath path = keyPathOf(call.getArg(0), scope_.context);
                    const bool named = !path.keys.empty() && isLuaState(path.root->getType()) &&
                        std::ranges::none_of(path.keys, [](const std::string& key) { return key.starts_with('['); });
                    if (!named) {
                        consider(Use::PassedOn, unresolved("passed to a Lua function chosen at runtime"));
                        return;
                    }
                    CallbackSig sig = unresolved("passed to the Lua function " + llvm::join(path.keys, "."));
                    sig.forwardedTo = std::move(path.keys);
                    sig.forwardedParam = i - 1;
                    consider(Use::ForwardedToLua, std::move(sig));
                    return;
                }
            }

            // The parameter followed.
            const clang::ParmVarDecl& param_;
            // The function whose body is scanned.
            const clang::FunctionDecl& function_;
            // Shared pass state.
            const Scope& scope_;
            // Calls followed so far.
            int depth_;
            // The strongest use found so far.
            Use use_ = Use::PassedOn;
            // Its callback.
            std::optional<CallbackSig> found_;
        };

        // The record field a stored callback is read from (`callback.func`, `overrides[i]->func.value()`), or nullptr.
        const clang::MemberExpr* storageMember(const clang::Expr* e) {
            e = withoutConversions(e)->IgnoreParens();
            if (const auto* call = llvm::dyn_cast<clang::CXXMemberCallExpr>(e); call != nullptr &&
                call->getMethodDecl() != nullptr && call->getMethodDecl()->getNameAsString() == "value") {
                e = call->getImplicitObjectArgument()->IgnoreImplicit();
            }
            const auto* member = llvm::dyn_cast<clang::MemberExpr>(e);
            return member != nullptr && llvm::isa<clang::FieldDecl>(member->getMemberDecl()) ? member : nullptr;
        }

        // What selects a dispatch site or a container write (plan C4).
        struct Guard {
            // An enum constant (`ON::FRAME`).
            std::string constant;
            // An enum parameter of the enclosing function.
            const clang::ParmVarDecl* param = nullptr;
            // A container field iterated around the site.
            std::string container;

            // True when anything was found.
            bool found() const { return !constant.empty() || param != nullptr || !container.empty(); }
        };

        // The key a compared value gives: an enum constant or an enum parameter.
        Guard keyOfSide(const clang::Expr* side) {
            if (std::optional<std::string> constant = enumConstant(side)) {
                return {std::move(*constant), nullptr, {}};
            }
            return {{}, enumParam(side), {}};
        }

        // `object.member == key` (either way round): the key; nothing when no side is a member of object.
        Guard equalityKey(const clang::Expr* lhs, const clang::Expr* rhs, const clang::ValueDecl* object) {
            if (objectOf(lhs) == object) {
                return keyOfSide(rhs);
            }
            return objectOf(rhs) == object ? keyOfSide(lhs) : Guard{};
        }

        // The first `object.member == key` comparison in condition.
        Guard conditionKey(const clang::Stmt* condition, const clang::ValueDecl* object) {
            Guard key;
            findStmt(condition, [&](const clang::Stmt& s) {
                if (const auto* binary = llvm::dyn_cast<clang::BinaryOperator>(&s);
                    binary != nullptr && binary->getOpcode() == clang::BO_EQ) {
                    key = equalityKey(binary->getLHS(), binary->getRHS(), object);
                }
                else if (const auto* op = llvm::dyn_cast<clang::CXXOperatorCallExpr>(&s);
                         op != nullptr && op->getOperator() == clang::OO_EqualEqual && op->getNumArgs() == 2) {
                    key = equalityKey(op->getArg(0), op->getArg(1), object);
                }
                return key.found();
            });
            return key;
        }

        // The container field a range-for iterates, or empty.
        std::string iteratedContainer(const clang::CXXForRangeStmt& loop) {
            const auto* member = llvm::dyn_cast_or_null<clang::MemberExpr>(
                loop.getRangeInit() == nullptr ? nullptr : loop.getRangeInit()->IgnoreImplicit());
            const auto* field = member == nullptr ? nullptr : llvm::dyn_cast<clang::FieldDecl>(member->getMemberDecl());
            return field == nullptr ? std::string() : storageKey(*field);
        }

        // The guard of one ancestor of a site: a case label, an if comparing a member of object (when the site is in
        // its then branch), or a range-for over a container.
        Guard ancestorGuard(const clang::DynTypedNode& node, const clang::Stmt* child, const clang::ValueDecl* object) {
            if (const auto* label = node.get<clang::CaseStmt>()) {
                return keyOfSide(label->getLHS());
            }
            if (const auto* branch = node.get<clang::IfStmt>();
                branch != nullptr && object != nullptr && child == branch->getThen()) {
                return conditionKey(branch->getCond(), object);
            }
            if (const auto* loop = node.get<clang::CXXForRangeStmt>()) {
                return {{}, nullptr, iteratedContainer(*loop)};
            }
            return {};
        }

        // The container field of the closest range-for around s inside its function, or empty.
        std::string enclosingLoop(const clang::Stmt& s, clang::ASTContext& context) {
            clang::DynTypedNode node = clang::DynTypedNode::create(s);
            for (int step = 0; step < MaxAncestors; ++step) {
                const clang::DynTypedNodeList parents = context.getParents(node);
                if (parents.empty() || parents[0].get<clang::FunctionDecl>() != nullptr) {
                    return {};
                }
                node = parents[0];
                if (const auto* loop = node.get<clang::CXXForRangeStmt>()) {
                    return iteratedContainer(*loop);
                }
            }
            return {};
        }

        // The closest guard of s inside its function.
        Guard findGuard(const clang::Stmt& s, const clang::ValueDecl* object, clang::ASTContext& context) {
            clang::DynTypedNode child = clang::DynTypedNode::create(s);
            for (int step = 0; step < MaxAncestors; ++step) {
                const clang::DynTypedNodeList parents = context.getParents(child);
                if (parents.empty() || parents[0].get<clang::FunctionDecl>() != nullptr ||
                    parents[0].get<clang::LambdaExpr>() != nullptr) {
                    break;
                }
                if (Guard guard = ancestorGuard(parents[0], child.get<clang::Stmt>(), object); guard.found()) {
                    return guard;
                }
                child = parents[0];
            }
            return {};
        }

        // An enum parameter used in e (`overrides[index]->func`), or nullptr.
        const clang::ParmVarDecl* enumParamIn(const clang::Expr* e) {
            const clang::ParmVarDecl* param = nullptr;
            findStmt(e, [&](const clang::Stmt& s) {
                const auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(&s);
                param = reference == nullptr ? nullptr : enumParam(reference);
                return param != nullptr;
            });
            return param;
        }

        // Sets the selector of site to param (its function and index).
        void setSelector(DispatchSite& site, const clang::ParmVarDecl& param) {
            if (const auto* function = llvm::dyn_cast<clang::FunctionDecl>(param.getDeclContext())) {
                site.selectorFunction = functionKey(*function);
                site.selectorParam = param.getFunctionScopeIndex();
            }
        }

        // ---- C5: vtable hooks ----

        // The value of a class-type template argument (a template parameter object, or a structural value), or nullptr.
        const clang::APValue* classValue(const clang::TemplateArgument& argument) {
            if (argument.getKind() == clang::TemplateArgument::StructuralValue) {
                return &argument.getAsStructuralValue();
            }
            if (argument.getKind() != clang::TemplateArgument::Declaration) {
                return nullptr;
            }
            const auto* object = llvm::dyn_cast<clang::TemplateParamObjectDecl>(argument.getAsDecl());
            return object == nullptr ? nullptr : &object->getValue();
        }

        // The text of a LiteralString template argument (`struct { char Str[N]; size_t Size; }`), or nothing.
        std::optional<std::string> literalString(const clang::TemplateArgument& argument) {
            const clang::APValue* value = classValue(argument);
            if (value == nullptr || !value->isStruct() || value->getStructNumFields() == 0 ||
                !value->getStructField(0).isArray()) {
                return std::nullopt;
            }
            const clang::APValue& chars = value->getStructField(0);
            std::string text;
            for (unsigned i = 0; i < chars.getArrayInitializedElts(); ++i) {
                const clang::APValue& c = chars.getArrayInitializedElt(i);
                if (!c.isInt() || c.getInt().isZero()) {
                    break;
                }
                text.push_back(static_cast<char>(c.getInt().getExtValue()));
            }
            return text;
        }

        // The concatenated LiteralString template arguments of call's callee (`JoinLiteralStrings<"set_pre_", Name>`).
        std::optional<std::string> joinedLiterals(const clang::CallExpr& call) {
            const clang::FunctionDecl* callee = call.getDirectCallee();
            const clang::TemplateArgumentList* arguments =
                callee == nullptr ? nullptr : callee->getTemplateSpecializationArgs();
            std::optional<std::string> joined;
            for (const clang::TemplateArgument& argument :
                 arguments == nullptr ? llvm::ArrayRef<clang::TemplateArgument>() : arguments->asArray()) {
                if (std::optional<std::string> part = literalString(argument)) {
                    joined = joined.value_or("") + *part;
                }
            }
            return joined;
        }

        // The Lua key a registration assigns: a literal, or computed from LiteralString template arguments.
        std::optional<std::string> registrationKey(const clang::Expr* key) {
            if (std::optional<std::string> literal = stringValue(key)) {
                return literal;
            }
            std::optional<std::string> joined;
            findStmt(key, [&](const clang::Stmt& s) {
                const auto* call = llvm::dyn_cast<clang::CallExpr>(&s);
                joined = call == nullptr ? std::nullopt : joinedLiterals(*call);
                return joined.has_value();
            });
            return joined;
        }

        // One `VTableEntry<"name", index, Sig, ...>` as written in the vtable's alias.
        struct WrittenEntry {
            // The entry's template argument.
            SourceLoc loc;
            // `/// NoDoc` (and any other comment) above it.
            Doc doc;
            // The virtual function a `MemFun<&T::f>` signature names: parameter names and docs.
            const clang::FunctionDecl* method = nullptr;
        };

        // The written template specialization a type argument is (through sugar), or a null loc.
        clang::TemplateSpecializationTypeLoc specializationLoc(const clang::TemplateArgumentLoc& argument) {
            if (argument.getArgument().getKind() != clang::TemplateArgument::Type ||
                argument.getTypeSourceInfo() == nullptr) {
                return {};
            }
            return argument.getTypeSourceInfo()->getTypeLoc().getAsAdjusted<clang::TemplateSpecializationTypeLoc>();
        }

        // Name of the template a written specialization names.
        std::string templateNameOf(const clang::TemplateSpecializationTypeLoc& loc) {
            const clang::TemplateDecl* decl = loc.getTypePtr()->getTemplateName().getAsTemplateDecl();
            return decl == nullptr ? std::string() : decl->getNameAsString();
        }

        // The member function a written `MemFun<&T::f>` argument names, or nullptr.
        const clang::FunctionDecl* memberFunction(const clang::TemplateArgumentLoc& argument) {
            const clang::TemplateSpecializationTypeLoc loc = specializationLoc(argument);
            const clang::Expr* named = !loc || loc.getNumArgs() == 0 ? nullptr : loc.getArgLoc(0).getSourceExpression();
            const std::vector<BoundFunction> bound =
                named == nullptr ? std::vector<BoundFunction>() : boundFunctions(named);
            return bound.empty() ? nullptr : bound.front().function;
        }

        // Adds argument to entries when it is a `VTableEntry<"name", ...>`.
        void addWrittenEntry(std::map<std::string, WrittenEntry>& entries, const clang::TemplateArgumentLoc& argument,
                             const Scope& scope) {
            const clang::TemplateSpecializationTypeLoc entry = specializationLoc(argument);
            const clang::Expr* name = !entry || templateNameOf(entry) != "VTableEntry" || entry.getNumArgs() < 3
                ? nullptr
                : entry.getArgLoc(0).getSourceExpression();
            const std::optional<std::string> text = name == nullptr ? std::nullopt : stringValue(name);
            if (text) {
                entries[*text] = {
                    scope.root.locate(scope.context.getSourceManager(), argument.getLocation()).value_or(SourceLoc{}),
                    docBefore(argument.getLocation(), scope.context), memberFunction(entry.getArgLoc(2))};
            }
        }

        // The entries written in the alias of variable's type (`using MovableVTable = HookableVTable<...>`), by name.
        std::map<std::string, WrittenEntry> writtenEntries(const clang::VarDecl& variable, const Scope& scope) {
            std::map<std::string, WrittenEntry> entries;
            const auto* alias = variable.getType()->getAs<clang::TypedefType>();
            const clang::TypeSourceInfo* written = alias == nullptr ? nullptr : alias->getDecl()->getTypeSourceInfo();
            const auto table = written == nullptr
                ? clang::TemplateSpecializationTypeLoc()
                : written->getTypeLoc().getAsAdjusted<clang::TemplateSpecializationTypeLoc>();
            for (unsigned i = 0; table && i < table.getNumArgs(); ++i) {
                addWrittenEntry(entries, table.getArgLoc(i), scope);
            }
            return entries;
        }

        // Names a hook callback's arguments: the object, then the virtual function's parameters.
        void nameHookParams(CallbackSig& callback, const clang::FunctionDecl* method) {
            if (callback.params.empty()) {
                return;
            }
            callback.params.front().name = "self";
            for (unsigned i = 0; method != nullptr && i < method->getNumParams() && i + 1 < callback.params.size();
                 ++i) {
                callback.params[i + 1].name = method->getParamDecl(i)->getNameAsString();
            }
        }

        // The hook rules of hookable_vtable.inl applied to the make_safe_cb callback of a registration.
        CallbackSig hookCallback(CallbackSig callback, bool pre, const clang::FunctionDecl* method) {
            if (callback.origin != CallbackOrigin::MakeSafeCb) {
                return callback;
            }
            callback.origin = CallbackOrigin::VtableHook;
            nameHookParams(callback, method);
            if (!pre) {
                callback.nilMeaning = Ignored;
            }
            else if (!callback.returns.empty() && callback.returns.front().kind == TypeKind::Bool) {
                callback.nilMeaning = "treated as false: the original function runs (true skips it)";
            }
            else {
                callback.nilMeaning = "the original function runs (a value replaces its result and skips it)";
            }
            return callback;
        }

        // Walks a HookableVTable constructor and the register_hooks it calls, turning each `lua_type[key] = lambda`
        // into a usertype member.
        class VtableWalk {
        public:
            // Adds the members to members; entries are the written ones.
            VtableWalk(std::map<std::string, WrittenEntry> entries, const Scope& scope, std::vector<Member>& members) :
                entries_(std::move(entries)), scope_(scope), members_(members) {}

            // Walks s and the register_hooks bodies it calls.
            void visit(const clang::Stmt* s) {
                if (s == nullptr) {
                    return;
                }
                if (const auto* op = llvm::dyn_cast<clang::CXXOperatorCallExpr>(s);
                    op != nullptr && op->getOperator() == clang::OO_Equal && op->getNumArgs() == 2) {
                    addRegistration(*op);
                }
                else if (const auto* call = llvm::dyn_cast<clang::CallExpr>(s)) {
                    visitRegisterHooks(*call);
                }
                for (const clang::Stmt* child : s->children()) {
                    visit(child);
                }
            }

        private:
            // Follows a call to VTableEntryImpl<...>::register_hooks into its instantiated body.
            void visitRegisterHooks(const clang::CallExpr& call) {
                const clang::FunctionDecl* callee = call.getDirectCallee();
                const clang::FunctionDecl* definition = nullptr;
                if (callee != nullptr && callee->getNameAsString() == "register_hooks" && callee->hasBody(definition)) {
                    visit(definition->getBody());
                }
            }

            // `lua_type[key] = lambda`: one member of the usertype.
            void addRegistration(const clang::CXXOperatorCallExpr& assignment) {
                const auto* subscript =
                    llvm::dyn_cast<clang::CXXOperatorCallExpr>(assignment.getArg(0)->IgnoreImplicit());
                const std::optional<std::string> key =
                    subscript == nullptr || subscript->getOperator() != clang::OO_Subscript
                    ? std::nullopt
                    : registrationKey(subscript->getArg(1));
                if (!key) {
                    return;
                }
                const std::vector<BoundFunction> bound = boundFunctions(assignment.getArg(1));
                if (bound.empty()) {
                    return;
                }
                const SourceLoc loc = locationOf(assignment, scope_.root, scope_.context);
                scope_.facts.consumedBindings.insert(loc);
                members_.push_back(member(*key, *bound.front().function, loc));
            }

            // The member for a registration of key, bound to function at loc.
            Member member(const std::string& key, const clang::FunctionDecl& function, const SourceLoc& loc) {
                const bool pre = llvm::StringRef(key).starts_with("set_pre_");
                const llvm::StringRef name = llvm::StringRef(key).drop_front(pre ? 8 : 9);
                const auto entry =
                    llvm::StringRef(key).starts_with("set_p") ? entries_.find(name.str()) : entries_.end();
                const clang::FunctionDecl* method = entry == entries_.end() ? nullptr : entry->second.method;
                Member result;
                result.luaName = key;
                result.kind = MemberKind::Method;
                result.loc = entry == entries_.end() ? loc : entry->second.loc;
                result.doc = entry == entries_.end() ? Doc{} : entry->second.doc;
                result.declDoc = method == nullptr ? Doc{} : docOfDecl(*method, scope_.context);
                result.overloads.push_back(signature(function, entry != entries_.end(), pre, method));
                return result;
            }

            // The signature of a registered lambda; the callbacks of hooks follow the hook rules.
            FunctionSig signature(const clang::FunctionDecl& function, bool hook, bool pre,
                                  const clang::FunctionDecl* method) {
                FunctionSig sig = signatureOf(function, scope_.root, scope_.context);
                sig.self = SelfKind::FirstParam;
                for (unsigned i = 0; i < function.getNumParams(); ++i) {
                    if (isLuaFunction(function.getParamDecl(i)->getType())) {
                        CallbackSig flow = FlowScan(*function.getParamDecl(i), function, scope_, 0).run();
                        if (hook) {
                            flow = hookCallback(std::move(flow), pre, method);
                        }
                        sig.params[i].callback = {std::move(flow)};
                    }
                }
                return sig;
            }

            // Written entries by name.
            std::map<std::string, WrittenEntry> entries_;
            // Shared pass state.
            const Scope& scope_;
            // Where members go.
            std::vector<Member>& members_;
        };

    } // namespace

    CallbackCollector::CallbackCollector(const SourceRoot& root, CallbackFacts& out) : root_(root), out_(out) {}

    void CallbackCollector::registerMatchers(MatchFinder& finder) {
        const auto luaFunction = qualType(hasUnqualifiedDesugaredType(recordType(hasDeclaration(
            classTemplateSpecializationDecl(hasAnyName("::sol::basic_function", "::sol::basic_protected_function"))))));
        finder.addMatcher(functionDecl(isDefinition(),
                                       hasAnyParameter(hasType(qualType(anyOf(luaFunction, references(luaFunction))))))
                              .bind(FlowNode),
                          this);
        finder.addMatcher(callExpr(callee(functionDecl(hasName("::handle_function"))),
                                   forCallable(functionDecl().bind(SiteFunctionNode)))
                              .bind(SiteNode),
                          this);
        finder.addMatcher(callExpr(forEachArgumentWithParam(
                                       expr(hasType(qualType(hasCanonicalType(enumType())))).bind(EdgeArgumentNode),
                                       parmVarDecl().bind(EdgeParamNode)))
                              .bind(EdgeCallNode),
                          this);
        finder.addMatcher(cxxOperatorCallExpr(hasOverloadedOperatorName("="),
                                              hasArgument(0,
                                                          ignoringImplicit(cxxOperatorCallExpr(
                                                              hasOverloadedOperatorName("[]"),
                                                              hasArgument(0, ignoringImplicit(memberExpr()))))))
                              .bind(ContainerNode),
                          this);
        finder.addMatcher(varDecl(hasType(qualType(hasUnqualifiedDesugaredType(recordType(
                                      hasDeclaration(classTemplateSpecializationDecl(hasName("::HookableVTable"))))))))
                              .bind(VtableNode),
                          this);
    }

    void CallbackCollector::run(const MatchFinder::MatchResult& result) {
        clang::ASTContext& context = *result.Context;
        const auto& nodes = result.Nodes;
        if (const auto* function = nodes.getNodeAs<clang::FunctionDecl>(FlowNode)) {
            addFlows(*function, context);
        }
        else if (const auto* site = nodes.getNodeAs<clang::CallExpr>(SiteNode)) {
            addSite(*site, *nodes.getNodeAs<clang::FunctionDecl>(SiteFunctionNode), context);
        }
        else if (const auto* argument = nodes.getNodeAs<clang::Expr>(EdgeArgumentNode)) {
            addEdge(*argument, *nodes.getNodeAs<clang::ParmVarDecl>(EdgeParamNode),
                    *nodes.getNodeAs<clang::CallExpr>(EdgeCallNode), context);
        }
        else if (const auto* assignment = nodes.getNodeAs<clang::CXXOperatorCallExpr>(ContainerNode)) {
            addContainerKey(*assignment, context);
        }
        else if (const auto* variable = nodes.getNodeAs<clang::VarDecl>(VtableNode)) {
            addVtable(*variable, context);
        }
    }

    void CallbackCollector::addFlows(const clang::FunctionDecl& function, clang::ASTContext& context) {
        if (!root_.locate(context.getSourceManager(), function.getLocation())) {
            return;
        }
        const Scope scope{root_, context, out_};
        for (const clang::ParmVarDecl* param : function.parameters()) {
            const std::string key = parameterKey(function, param->getFunctionScopeIndex());
            if (isLuaFunction(param->getType()) && !out_.flows.contains(key)) {
                out_.flows.emplace(key, FlowScan(*param, function, scope, 0).run());
            }
        }
    }

    void CallbackCollector::addSite(const clang::CallExpr& call, const clang::FunctionDecl& function,
                                    clang::ASTContext& context) {
        const clang::MemberExpr* member = call.getNumArgs() < 2 ? nullptr : storageMember(call.getArg(1));
        const std::optional<SourceLoc> loc = root_.locate(context.getSourceManager(), call.getBeginLoc());
        if (member == nullptr || !loc) {
            return; // not a stored callback (a parameter's own call is found by its flow), or outside the root
        }
        DispatchSite site;
        site.storage = storageKey(*llvm::cast<clang::FieldDecl>(member->getMemberDecl()));
        site.signature = handleFunctionSignature(call, root_, context);
        site.signature.origin = CallbackOrigin::StoredHandleFunction;
        site.loop = enclosingLoop(call, context);
        if (const clang::ParmVarDecl* param = enumParamIn(member)) {
            setSelector(site, *param);
        }
        else {
            Guard guard = findGuard(call, objectOf(member), context);
            site.constant = std::move(guard.constant);
            site.container = std::move(guard.container);
            if (guard.param != nullptr) {
                setSelector(site, *guard.param);
            }
        }
        out_.sites.emplace(std::pair(*loc, functionKey(function)), std::move(site));
    }

    void CallbackCollector::addEdge(const clang::Expr& argument, const clang::ParmVarDecl& param,
                                    const clang::CallExpr& call, const clang::ASTContext& context) {
        const auto* callee = llvm::dyn_cast<clang::FunctionDecl>(param.getDeclContext());
        if (callee == nullptr || !root_.locate(context.getSourceManager(), call.getBeginLoc())) {
            return;
        }
        SelectorEdge edge{{}, param.getFunctionScopeIndex(), {}, {}, 0};
        if (std::optional<std::string> constant = enumConstant(&argument)) {
            edge.constant = std::move(*constant);
        }
        else if (const clang::ParmVarDecl* forwarded = enumParam(&argument)) {
            const auto* caller = llvm::dyn_cast<clang::FunctionDecl>(forwarded->getDeclContext());
            if (caller == nullptr) {
                return;
            }
            edge.caller = functionKey(*caller);
            edge.callerParam = forwarded->getFunctionScopeIndex();
        }
        else {
            return;
        }
        edge.callee = functionKey(*callee);
        out_.edges.insert(std::move(edge));
    }

    void CallbackCollector::addContainerKey(const clang::CXXOperatorCallExpr& assignment, clang::ASTContext& context) {
        const auto* subscript = llvm::cast<clang::CXXOperatorCallExpr>(assignment.getArg(0)->IgnoreImplicit());
        const auto* member = llvm::cast<clang::MemberExpr>(subscript->getArg(0)->IgnoreImplicit());
        const auto* field = llvm::dyn_cast<clang::FieldDecl>(member->getMemberDecl());
        if (field == nullptr || !root_.locate(context.getSourceManager(), assignment.getBeginLoc())) {
            return;
        }
        const Guard guard = findGuard(assignment, objectOf(assignment.getArg(1)), context);
        if (!guard.constant.empty()) {
            out_.containerKeys.emplace(storageKey(*field), guard.constant);
        }
    }

    void CallbackCollector::addVtable(const clang::VarDecl& variable, clang::ASTContext& context) {
        const auto* construct = variable.getInit() == nullptr
            ? nullptr
            : llvm::dyn_cast<clang::CXXConstructExpr>(variable.getInit()->IgnoreImplicit());
        if (construct == nullptr || construct->getNumArgs() < 2) {
            return;
        }
        const KeyPath path = keyPathOf(withoutConversions(construct->getArg(1)), context);
        if (path.keys.size() != 1 || !isLuaState(path.root->getType()) ||
            !out_.vtableHooks.emplace(path.keys.front(), std::vector<Member>()).second) {
            return; // not bound on a usertype, or seen in another translation unit
        }
        const clang::CXXConstructorDecl* constructor = construct->getConstructor();
        if (constructor->isInheritingConstructor()) {
            constructor = constructor->getInheritedConstructor().getConstructor();
        }
        const clang::FunctionDecl* definition = nullptr;
        if (constructor->hasBody(definition)) {
            const Scope scope{root_, context, out_};
            VtableWalk(writtenEntries(variable, scope), scope, out_.vtableHooks[path.keys.front()])
                .visit(definition->getBody());
        }
    }

} // namespace dd
