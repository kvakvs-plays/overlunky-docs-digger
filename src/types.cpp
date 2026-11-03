#include "types.hpp"

#include <clang/AST/DeclTemplate.h>
#include <clang/AST/PrettyPrinter.h>
#include <llvm/ADT/StringMap.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace dd {

    namespace {

        // Which template arguments of a known class template become TypeInfo::inner.
        enum class InnerShape : std::uint8_t {
            // None (strings, Lua handles).
            None,
            // The first one: element of a sequence, payload of an optional.
            First,
            // The first two: key and value of a mapping.
            FirstTwo,
            // All of them: tuple and pair members.
            All,
            // The function type argument of std::function: parameters into inner, result into returns.
            Signature,
            // sol2 wrappers converted as their argument (nested, as_table_t): the type is classified as that.
            Transparent,
        };

        // How a std or sol2 class template is seen from Lua.
        struct TemplateRule {
            // Lua meaning; ignored for Transparent.
            TypeKind kind;
            // Which template arguments are kept as inner types.
            InnerShape inner;
            // Lua may pass or get nil.
            bool nullable;
        };

        // Known std and sol2 class templates by qualified template name; anything else is a plain record.
        const llvm::StringMap<TemplateRule>& templateRules() {
            using enum InnerShape;
            static const llvm::StringMap<TemplateRule> rules = {
                {"std::basic_string", {TypeKind::String, None, false}},
                {"std::basic_string_view", {TypeKind::String, None, false}},
                {"std::optional", {TypeKind::Optional, First, true}},
                {"sol::optional", {TypeKind::Optional, First, true}},
                {"std::vector", {TypeKind::Container, First, false}},
                {"std::array", {TypeKind::Container, First, false}},
                {"std::deque", {TypeKind::Container, First, false}},
                {"std::list", {TypeKind::Container, First, false}},
                {"std::set", {TypeKind::Container, First, false}},
                {"std::unordered_set", {TypeKind::Container, First, false}},
                {"std::span", {TypeKind::Container, First, false}},
                {"std::map", {TypeKind::Container, FirstTwo, false}},
                {"std::unordered_map", {TypeKind::Container, FirstTwo, false}},
                {"std::tuple", {TypeKind::Tuple, All, false}},
                {"std::pair", {TypeKind::Tuple, All, false}},
                {"std::function", {TypeKind::Function, Signature, false}},
                {"sol::basic_function", {TypeKind::Function, None, false}},
                {"sol::basic_protected_function", {TypeKind::Function, None, false}},
                {"sol::basic_object", {TypeKind::LuaObject, None, true}},
                {"sol::basic_reference", {TypeKind::LuaObject, None, true}},
                {"sol::basic_table_core", {TypeKind::LuaObject, None, false}},
                {"sol::basic_lua_table", {TypeKind::LuaObject, None, false}},
                {"sol::basic_userdata", {TypeKind::LuaObject, None, false}},
                {"sol::basic_lightuserdata", {TypeKind::LuaObject, None, false}},
                {"sol::basic_environment", {TypeKind::LuaObject, None, false}},
                {"sol::nested", {TypeKind::Unknown, Transparent, false}},
                {"sol::as_table_t", {TypeKind::Unknown, Transparent, false}},
                {"sol::as_returns_t", {TypeKind::Unknown, Transparent, false}},
                // sol2 pushes the object a smart pointer or reference wrapper holds.
                {"std::unique_ptr", {TypeKind::Unknown, Transparent, false}},
                {"std::shared_ptr", {TypeKind::Unknown, Transparent, false}},
                {"std::reference_wrapper", {TypeKind::Unknown, Transparent, false}},
            };
            return rules;
        }

        // Plain sol2 classes with a fixed Lua meaning: kind, nullable, hidden.
        struct ClassRule {
            TypeKind kind;
            bool nullable;
            bool hidden;
        };

        // Known non-template sol2 classes.
        const llvm::StringMap<ClassRule>& classRules() {
            static const llvm::StringMap<ClassRule> rules = {
                {"sol::this_state", {TypeKind::LuaObject, false, true}},
                {"sol::this_main_state", {TypeKind::LuaObject, false, true}},
                {"sol::this_environment", {TypeKind::LuaObject, false, true}},
                {"sol::variadic_args", {TypeKind::Variadic, false, false}},
                {"sol::variadic_results", {TypeKind::Variadic, false, false}},
                {"sol::lua_nil_t", {TypeKind::Nil, true, false}},
                {"sol::lua_value", {TypeKind::LuaObject, true, false}},
                {"sol::stack_reference", {TypeKind::LuaObject, true, false}},
            };
            return rules;
        }

        // Type names as people write them: `bool`, no `struct`/`class` keywords.
        clang::PrintingPolicy printingPolicy(const clang::ASTContext& context) {
            clang::PrintingPolicy policy(context.getLangOpts());
            policy.SuppressTagKeyword = true;
            policy.Bool = true;
            return policy;
        }

        // Appends the type arguments among arguments to types, packs expanded.
        void appendTypes(llvm::ArrayRef<clang::TemplateArgument> arguments, std::vector<clang::QualType>& types) {
            for (const clang::TemplateArgument& argument : arguments) {
                if (argument.getKind() == clang::TemplateArgument::Type) {
                    types.push_back(argument.getAsType());
                }
                else if (argument.getKind() == clang::TemplateArgument::Pack) {
                    appendTypes(argument.pack_elements(), types);
                }
            }
        }

        // The type arguments as written when written names the template directly, so typedef names such as
        // ENT_TYPE survive; otherwise (typedefs of the whole type, alias templates) those of the specialization.
        std::vector<clang::QualType> typeArguments(clang::QualType written,
                                                   const clang::ClassTemplateSpecializationDecl& specialization) {
            std::vector<clang::QualType> types;
            const auto* asWritten = written.getNonReferenceType()->getAs<clang::TemplateSpecializationType>();
            if (asWritten != nullptr && !asWritten->isTypeAlias()) {
                appendTypes(asWritten->template_arguments(), types);
            }
            if (types.empty()) {
                appendTypes(specialization.getTemplateArgs().asArray(), types);
            }
            return types;
        }

        // Classifies the first count types of types into inner (all of them when count is 0).
        void addInner(TypeInfo& info, const std::vector<clang::QualType>& types, std::size_t count,
                      const clang::ASTContext& context) {
            const std::size_t used = count == 0 ? types.size() : std::min(count, types.size());
            for (std::size_t i = 0; i < used; ++i) {
                info.inner.push_back(classifyType(types[i], context));
            }
        }

        // std::function<R(A...)>: parameter types into inner, R into returns.
        void addSignature(TypeInfo& info, const std::vector<clang::QualType>& types, const clang::ASTContext& context) {
            const auto* signature = types.empty() ? nullptr : types.front()->getAs<clang::FunctionProtoType>();
            if (signature == nullptr) {
                return;
            }
            for (const clang::QualType param : signature->getParamTypes()) {
                info.inner.push_back(classifyType(param, context));
            }
            info.returns = classifyReturns(signature->getReturnType(), context);
        }

        // Kind, nullability and inner types of a known class template (not Transparent).
        void applyTemplateRule(TypeInfo& info, const TemplateRule& rule, const std::vector<clang::QualType>& types,
                               const clang::ASTContext& context) {
            info.kind = rule.kind;
            info.nullable = rule.nullable;
            switch (rule.inner) {
            case InnerShape::First:
                addInner(info, types, 1, context);
                break;
            case InnerShape::FirstTwo:
                addInner(info, types, 2, context);
                break;
            case InnerShape::All:
                addInner(info, types, 0, context);
                break;
            case InnerShape::Signature:
                addSignature(info, types, context);
                break;
            case InnerShape::None:
            case InnerShape::Transparent:
                break;
            }
        }

        // Records: known std/sol2 templates and classes by name, everything else a plain record.
        void classifyRecord(TypeInfo& info, clang::QualType written, const clang::CXXRecordDecl& record,
                            const clang::ASTContext& context) {
            const auto* specialization = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(&record);
            if (specialization != nullptr) {
                const std::string name = specialization->getSpecializedTemplate()->getQualifiedNameAsString();
                const auto rule = templateRules().find(name);
                if (rule != templateRules().end()) {
                    const std::vector<clang::QualType> types = typeArguments(written, *specialization);
                    if (rule->second.inner == InnerShape::Transparent && !types.empty()) {
                        TypeInfo unwrapped = classifyType(types.front(), context);
                        unwrapped.spelling = info.spelling;
                        info = std::move(unwrapped);
                        return;
                    }
                    applyTemplateRule(info, rule->second, types, context);
                    return;
                }
            }
            const auto rule = classRules().find(record.getQualifiedNameAsString());
            if (rule != classRules().end()) {
                info.kind = rule->second.kind;
                info.nullable = rule->second.nullable;
                info.hidden = rule->second.hidden;
                return;
            }
            info.kind = TypeKind::Record;
        }

        // Character types whose pointers are C strings.
        bool isCharacter(clang::QualType type) {
            return type->isAnyCharacterType() || type->isSpecificBuiltinType(clang::BuiltinType::Char_S) ||
                type->isSpecificBuiltinType(clang::BuiltinType::Char_U);
        }

        // Pointers: C strings, function pointers, or nullable pointers to an object.
        void classifyPointer(TypeInfo& info, clang::QualType pointee, const clang::ASTContext& context) {
            if (isCharacter(pointee)) {
                info.kind = TypeKind::String;
                return;
            }
            if (pointee->isFunctionType()) {
                info.kind = TypeKind::Function;
                return;
            }
            // sol2 passes the calling state to a `lua_State*` parameter, like sol::this_state.
            if (const clang::RecordDecl* record = pointee->getAsRecordDecl();
                record && record->getName() == "lua_State") {
                info.kind = TypeKind::LuaObject;
                info.hidden = true;
                return;
            }
            info.kind = TypeKind::Pointer;
            info.nullable = true;
            info.inner.push_back(classifyType(pointee, context));
        }

        // Member pointers and C arrays; returns false when type is neither.
        bool classifyIndirect(TypeInfo& info, clang::QualType type, const clang::ASTContext& context) {
            if (const auto* member = type->getAs<clang::MemberPointerType>()) {
                if (member->isMemberFunctionPointer()) {
                    info.kind = TypeKind::Function;
                    return true;
                }
                // `&T::field` binds the field, so Lua sees the field's type.
                TypeInfo field = classifyType(member->getPointeeType(), context);
                field.spelling = std::move(info.spelling);
                field.canonical = std::move(info.canonical);
                info = std::move(field);
                return true;
            }
            const clang::ArrayType* array = context.getAsArrayType(type);
            if (array == nullptr) {
                return false;
            }
            if (isCharacter(array->getElementType())) {
                info.kind = TypeKind::String;
                return true;
            }
            info.kind = TypeKind::Container;
            info.inner.push_back(classifyType(array->getElementType(), context));
            return true;
        }

        // Builtin and enum types; returns false when type is none of them.
        bool classifyScalar(TypeInfo& info, clang::QualType type) {
            if (type->isVoidType()) {
                info.kind = TypeKind::Void;
            }
            else if (type->isBooleanType()) {
                info.kind = TypeKind::Bool;
            }
            else if (type->isEnumeralType()) {
                info.kind = TypeKind::Enum;
            }
            else if (type->isIntegerType()) {
                info.kind = TypeKind::Int;
            }
            else if (type->isRealFloatingType()) {
                info.kind = TypeKind::Float;
            }
            else {
                return false;
            }
            return true;
        }

    } // namespace

    TypeInfo classifyType(clang::QualType type, const clang::ASTContext& context) {
        const clang::PrintingPolicy policy = printingPolicy(context);
        TypeInfo info;
        info.spelling = type.getAsString(policy);
        const clang::QualType bare = type.getNonReferenceType().getCanonicalType().getUnqualifiedType();
        info.canonical = bare.getAsString(policy);
        if (classifyScalar(info, bare)) {
            return info;
        }
        if (classifyIndirect(info, bare, context)) {
            return info;
        }
        if (bare->isPointerType()) {
            classifyPointer(info, bare->getPointeeType(), context);
        }
        else if (const clang::CXXRecordDecl* record = bare->getAsCXXRecordDecl()) {
            classifyRecord(info, type, *record, context);
        }
        return info;
    }

    std::vector<TypeInfo> classifyReturns(clang::QualType type, const clang::ASTContext& context) {
        TypeInfo info = classifyType(type, context);
        if (info.kind == TypeKind::Void) {
            return {};
        }
        if (info.kind == TypeKind::Tuple) {
            return std::move(info.inner);
        }
        return {std::move(info)};
    }

} // namespace dd
