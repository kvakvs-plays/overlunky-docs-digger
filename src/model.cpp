#include "model.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <tuple>

namespace dd {

    namespace {

        // Lower-case kind names used in the JSON output, in TypeKind order.
        constexpr std::array<const char*, 16> KindNames = {
            "void",     "bool",      "int",   "float",    "string",     "enum",     "record", "pointer",
            "optional", "container", "tuple", "function", "lua_object", "variadic", "nil",    "unknown"};
        static_assert(KindNames.size() == static_cast<std::size_t>(TypeKind::Unknown) + 1);

        // Name of kind in the JSON output.
        const char* kindName(TypeKind kind) { return KindNames.at(static_cast<std::size_t>(kind)); }

        // Member kind names, in MemberKind order.
        constexpr std::array<const char*, 5> MemberKindNames = {"field", "readonly", "property", "method", "value"};
        static_assert(MemberKindNames.size() == static_cast<std::size_t>(MemberKind::Value) + 1);

        // How a method gets its object, in SelfKind order (None is not written).
        constexpr std::array<const char*, 3> SelfKindNames = {"none", "implicit", "first_param"};
        static_assert(SelfKindNames.size() == static_cast<std::size_t>(SelfKind::FirstParam) + 1);

        // A JSON array of the toJSON() of every element.
        template <typename T>
        llvm::json::Array arrayOf(const std::vector<T>& items) {
            llvm::json::Array array;
            for (const T& item : items) {
                array.push_back(toJSON(item));
            }
            return array;
        }

        // Adds the documentation fields shared by functions and values.
        void addDoc(llvm::json::Object& object, const Doc& doc) {
            if (!doc.lines.empty()) {
                object["doc"] = toJSON(doc);
            }
            if (doc.nodoc) {
                object["nodoc"] = true;
            }
            if (doc.deprecated) {
                object["deprecated"] = true;
            }
        }

        // Adds the naming and location fields shared by functions and values.
        template <typename Binding>
        void addBinding(llvm::json::Object& object, const Binding& binding) {
            object["name"] = binding.luaName;
            if (!binding.owner.empty()) {
                object["owner"] = llvm::json::Array(binding.owner);
            }
            if (!binding.root.empty()) {
                object["root"] = binding.root;
            }
            object["source"] = toJSON(binding.loc);
        }

    } // namespace

    llvm::json::Value toJSON(const SourceLoc& loc) {
        return llvm::json::Object{{"file", loc.file}, {"line", loc.line}, {"column", loc.column}};
    }

    llvm::json::Value toJSON(const Doc& doc) { return llvm::json::Array(doc.lines); }

    llvm::json::Value toJSON(const TypeInfo& type) {
        llvm::json::Object object{{"spelling", type.spelling}, {"kind", kindName(type.kind)}};
        if (!type.canonical.empty() && type.canonical != type.spelling) {
            object["canonical"] = type.canonical;
        }
        if (!type.inner.empty()) {
            object["inner"] = arrayOf(type.inner);
        }
        if (!type.returns.empty()) {
            object["returns"] = arrayOf(type.returns);
        }
        if (type.nullable) {
            object["nullable"] = true;
        }
        if (type.hidden) {
            object["hidden"] = true;
        }
        return object;
    }

    llvm::json::Value toJSON(const Param& param) {
        llvm::json::Object object{{"name", param.name}, {"type", toJSON(param.type)}};
        if (param.omittable) {
            object["omittable"] = true;
        }
        return object;
    }

    llvm::json::Value toJSON(const FunctionSig& sig) {
        llvm::json::Object object{
            {"params", arrayOf(sig.params)}, {"returns", arrayOf(sig.returns)}, {"cpp", sig.cppTarget}};
        addDoc(object, sig.doc);
        if (sig.self != SelfKind::None) {
            object["self"] = SelfKindNames.at(static_cast<std::size_t>(sig.self));
        }
        return object;
    }

    llvm::json::Value toJSON(const Function& function) {
        llvm::json::Object object;
        addBinding(object, function);
        addDoc(object, function.doc);
        object["overloads"] = arrayOf(function.overloads);
        if (function.docOnly) {
            object["doc_only"] = true;
        }
        if (function.definedInLua) {
            object["defined_in_lua"] = true;
        }
        if (!function.aliasOf.empty()) {
            object["alias_of"] = llvm::json::Array(function.aliasOf);
        }
        return object;
    }

    llvm::json::Value toJSON(const Value& value) {
        llvm::json::Object object;
        addBinding(object, value);
        addDoc(object, value.doc);
        object["type"] = toJSON(value.type);
        if (value.docOnly) {
            object["doc_only"] = true;
        }
        return object;
    }

    llvm::json::Value toJSON(const Member& member) {
        llvm::json::Object object{{"name", member.luaName},
                                  {"kind", MemberKindNames.at(static_cast<std::size_t>(member.kind))},
                                  {"source", toJSON(member.loc)}};
        if (member.meta) {
            object["meta"] = true;
        }
        addDoc(object, member.doc);
        if (!member.declDoc.lines.empty()) {
            object["cpp_doc"] = toJSON(member.declDoc);
        }
        if (member.kind != MemberKind::Method) {
            object["type"] = toJSON(member.type);
        }
        if (!member.overloads.empty()) {
            object["overloads"] = arrayOf(member.overloads);
        }
        if (!member.getter.empty()) {
            object["getter"] = toJSON(member.getter.front());
        }
        if (!member.setter.empty()) {
            object["setter"] = toJSON(member.setter.front());
        }
        return object;
    }

    llvm::json::Value toJSON(const Usertype& usertype) {
        llvm::json::Object object{
            {"name", usertype.luaName}, {"cpp", usertype.cppType}, {"source", toJSON(usertype.loc)}};
        addDoc(object, usertype.doc);
        if (!usertype.declDoc.lines.empty()) {
            object["cpp_doc"] = toJSON(usertype.declDoc);
        }
        if (!usertype.bases.empty()) {
            object["bases"] = llvm::json::Array(usertype.bases);
        }
        if (!usertype.constructors.empty()) {
            object["constructors"] = arrayOf(usertype.constructors);
        }
        if (usertype.noConstructor) {
            object["no_constructor"] = true;
        }
        llvm::json::Array members;
        for (const auto& [key, member] : usertype.members) {
            members.push_back(toJSON(member));
        }
        object["members"] = std::move(members);
        return object;
    }

    llvm::json::Value toJSON(const Extraction& extraction) {
        llvm::json::Array functions;
        for (const auto& [key, function] : extraction.functions) {
            functions.push_back(toJSON(function));
        }
        llvm::json::Array values;
        for (const auto& [key, value] : extraction.values) {
            values.push_back(toJSON(value));
        }
        // Ordered by Lua name for readers; the map is keyed by C++ type for member lookup.
        std::vector<const Usertype*> usertypes;
        usertypes.reserve(extraction.usertypes.size());
        for (const auto& [cppType, usertype] : extraction.usertypes) {
            usertypes.push_back(&usertype);
        }
        std::ranges::sort(usertypes, [](const Usertype* a, const Usertype* b) {
            return std::tie(a->luaName, a->cppType) < std::tie(b->luaName, b->cppType);
        });
        llvm::json::Array usertypeArray;
        for (const Usertype* usertype : usertypes) {
            usertypeArray.push_back(toJSON(*usertype));
        }
        return llvm::json::Object{{"functions", std::move(functions)},
                                  {"values", std::move(values)},
                                  {"usertypes", std::move(usertypeArray)}};
    }

} // namespace dd
