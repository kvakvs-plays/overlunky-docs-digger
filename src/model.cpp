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

        // Enum value origins, in EnumValueOrigin order (Static is not written).
        constexpr std::array<const char*, 3> OriginNames = {"static", "doc_comment", "runtime_file"};
        static_assert(OriginNames.size() == static_cast<std::size_t>(EnumValueOrigin::RuntimeFile) + 1);

        // Callback origins, in CallbackOrigin order.
        constexpr std::array<const char*, 9> CallbackOriginNames = {
            "std_function",   "make_safe_cb", "handle_function", "direct_call", "stored_handle_function",
            "event_dispatch", "vtable_hook",  "doc_comment",     "unresolved"};
        static_assert(CallbackOriginNames.size() == static_cast<std::size_t>(CallbackOrigin::Unresolved) + 1);

        // Binder names, in Binder order (None is not written).
        constexpr std::array<const char*, 3> BinderNames = {"none", "front", "back"};
        static_assert(BinderNames.size() == static_cast<std::size_t>(Binder::Back) + 1);

        // A JSON array of the toJSON() of every element.
        template <typename T>
        llvm::json::Array arrayOf(const std::vector<T>& items) {
            llvm::json::Array array;
            for (const T& item : items) {
                array.push_back(toJSON(item));
            }
            return array;
        }

        // A JSON array of the toJSON() of every mapped value, in key order.
        template <typename Map>
        llvm::json::Array arrayOfValues(const Map& items) {
            llvm::json::Array array;
            for (const auto& [key, item] : items) {
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
        if (!type.lua.empty()) {
            object["lua"] = type.lua;
        }
        return object;
    }

    llvm::json::Value toJSON(const Param& param) {
        llvm::json::Object object{{"name", param.name}, {"type", toJSON(param.type)}};
        if (param.omittable) {
            object["omittable"] = true;
        }
        if (param.binder != Binder::None) {
            object["binder"] = BinderNames.at(static_cast<std::size_t>(param.binder));
        }
        if (!param.callback.empty()) {
            object["callback"] = toJSON(param.callback.front());
        }
        return object;
    }

    llvm::json::Value toJSON(const CallbackSig& callback) {
        llvm::json::Object object{{"origin", CallbackOriginNames.at(static_cast<std::size_t>(callback.origin))}};
        // A selected callback's signatures are in the events.
        if (callback.origin != CallbackOrigin::Unresolved && callback.selector.empty()) {
            object["params"] = arrayOf(callback.params);
            object["returns"] = arrayOf(callback.returns);
            object["nil_allowed"] = callback.nilAllowed;
        }
        const std::pair<const char*, const std::string*> texts[] = {{"nil_meaning", &callback.nilMeaning},
                                                                    {"stored_in", &callback.storedIn},
                                                                    {"selector", &callback.selector},
                                                                    {"unresolved", &callback.unresolved},
                                                                    {"prose", &callback.prose}};
        for (const auto& [key, text] : texts) {
            if (!text->empty()) {
                object[key] = *text;
            }
        }
        if (!callback.conflicts.empty()) {
            object["conflicts"] = llvm::json::Array(callback.conflicts);
        }
        if (!callback.loc.file.empty()) {
            object["source"] = toJSON(callback.loc);
        }
        return object;
    }

    llvm::json::Value toJSON(const Event& event) {
        llvm::json::Object object{{"table", event.table},
                                  {"name", event.luaName},
                                  {"stored_in", event.storedIn},
                                  {"callback", toJSON(event.callback)}};
        if (event.generic) {
            object["generic"] = true;
        }
        addDoc(object, event.doc);
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
        object["group"] = function.group;
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
        object["group"] = usertype.group;
        if (!usertype.section.empty()) {
            object["section"] = usertype.section;
        }
        object["members"] = arrayOfValues(usertype.members);
        return object;
    }

    llvm::json::Value toJSON(const EnumValue& value) {
        llvm::json::Object object{{"name", value.luaName}, {"text", value.text}};
        if (value.number) {
            object["value"] = *value.number;
        }
        else if (value.string) {
            object["value"] = *value.string;
        }
        if (value.bit) {
            object["bit"] = *value.bit;
        }
        if (!value.combines.empty()) {
            object["combines"] = value.combines;
        }
        addDoc(object, value.doc);
        if (value.origin != EnumValueOrigin::Static) {
            object["origin"] = OriginNames.at(static_cast<std::size_t>(value.origin));
        }
        if (!value.loc.file.empty()) {
            object["source"] = toJSON(value.loc);
        }
        return object;
    }

    llvm::json::Value toJSON(const Enum& table) {
        llvm::json::Object object{{"name", table.luaName}, {"values", arrayOfValues(table.values)}};
        if (table.loc.file.empty()) {
            object["origin"] = OriginNames.at(static_cast<std::size_t>(EnumValueOrigin::RuntimeFile));
        }
        else {
            object["source"] = toJSON(table.loc);
        }
        addDoc(object, table.doc);
        if (table.filledAtRuntime) {
            object["filled_at_runtime"] = true;
        }
        if (table.flags) {
            object["flags"] = true;
        }
        if (!table.usage.empty()) {
            object["usage"] = table.usage;
        }
        return object;
    }

    llvm::json::Value toJSON(const Alias& alias) {
        llvm::json::Object object{{"name", alias.name}, {"type", toJSON(alias.type)}, {"source", toJSON(alias.loc)}};
        addDoc(object, alias.doc);
        return object;
    }

    llvm::json::Value toJSON(const LibraryCall& call) {
        return llvm::json::Object{
            {"names", llvm::json::Array(call.names)}, {"function", call.function}, {"source", toJSON(call.loc)}};
    }

    llvm::json::Value toJSON(const ScriptCallback& callback) {
        llvm::json::Object object{
            {"name", callback.luaName}, {"type", toJSON(callback.type)}, {"source", toJSON(callback.loc)}};
        addDoc(object, callback.doc);
        return object;
    }

    llvm::json::Value toJSON(const Extraction& extraction) {
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
        return llvm::json::Object{{"functions", arrayOfValues(extraction.functions)},
                                  {"values", arrayOfValues(extraction.values)},
                                  {"usertypes", std::move(usertypeArray)},
                                  {"enums", arrayOfValues(extraction.enums)},
                                  {"aliases", arrayOfValues(extraction.aliases)},
                                  {"libraries", arrayOfValues(extraction.libraries)},
                                  {"script_callbacks", arrayOfValues(extraction.scriptCallbacks)},
                                  {"global_variables", llvm::json::Array(extraction.globalVariables)},
                                  {"events", arrayOfValues(extraction.events)},
                                  {"schema_version", SchemaVersion}};
    }

} // namespace dd
