#include "model.hpp"

#include <array>
#include <cstddef>

namespace dd {

    namespace {

        // Lower-case kind names used in the JSON output, in TypeKind order.
        constexpr std::array<const char*, 16> KindNames = {
            "void",     "bool",      "int",   "float",    "string",     "enum",     "record", "pointer",
            "optional", "container", "tuple", "function", "lua_object", "variadic", "nil",    "unknown"};
        static_assert(KindNames.size() == static_cast<std::size_t>(TypeKind::Unknown) + 1);

        // Name of kind in the JSON output.
        const char* kindName(TypeKind kind) { return KindNames.at(static_cast<std::size_t>(kind)); }

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

    llvm::json::Value toJSON(const Extraction& extraction) {
        llvm::json::Array functions;
        for (const auto& [key, function] : extraction.functions) {
            functions.push_back(toJSON(function));
        }
        llvm::json::Array values;
        for (const auto& [key, value] : extraction.values) {
            values.push_back(toJSON(value));
        }
        return llvm::json::Object{{"functions", std::move(functions)}, {"values", std::move(values)}};
    }

} // namespace dd
