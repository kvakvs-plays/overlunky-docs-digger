#include "lua_names.hpp"

#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>

#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace dd {

    namespace {

        // Characters of a qualified C++ name (`PRNG::PRNG_CLASS`).
        constexpr llvm::StringRef QualifiedChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_:";

        // True for a character of a qualified C++ name.
        bool isQualifiedChar(char c) { return QualifiedChars.contains(c); }

        // The canonical type, which TypeInfo leaves empty when it equals the spelling (decoded or hand-made types).
        const std::string& canonicalOf(const TypeInfo& type) {
            return type.canonical.empty() ? type.spelling : type.canonical;
        }

        // The spelling as a bare qualified name (`const ENT_TYPE &` → `ENT_TYPE`); empty when it is more than a name.
        std::string bareName(llvm::StringRef spelling) {
            llvm::StringRef text = spelling.trim();
            text.consume_front("const ");
            text = text.rtrim("& ");
            if (text.empty() || !llvm::all_of(text, isQualifiedChar)) {
                return {};
            }
            return text.str();
        }

        // `PRNG::PRNG_CLASS` → `PRNG_CLASS`.
        llvm::StringRef unqualified(llvm::StringRef name) {
            const std::size_t colon = name.rfind("::");
            return colon == llvm::StringRef::npos ? name : name.drop_front(colon + 2);
        }

        // The configured Lua type for the first of keys that has one.
        std::optional<std::string> configured(const LuaNaming& naming, std::initializer_list<llvm::StringRef> keys) {
            for (const llvm::StringRef key : keys) {
                const auto found = naming.names.find(key.str());
                if (!key.empty() && found != naming.names.end()) {
                    return found->second;
                }
            }
            return std::nullopt;
        }

        // A Lua name found by name rather than by kind: the usertype of a record, a configured name, or an enum table
        // or alias the type is written with.
        std::optional<std::string> namedType(const TypeInfo& type, const LuaNaming& naming) {
            if (type.kind == TypeKind::Record) {
                if (const auto usertype = naming.usertypes.find(canonicalOf(type));
                    usertype != naming.usertypes.end()) {
                    return usertype->second;
                }
            }
            const std::string bare = bareName(type.spelling);
            const llvm::StringRef name = unqualified(bare);
            if (std::optional<std::string> lua = configured(naming, {canonicalOf(type), bare, name})) {
                return lua;
            }
            if (naming.enums.contains(name.str()) || naming.aliases.contains(name.str())) {
                return name.str();
            }
            return std::nullopt;
        }

        // `T?`, without doubling the mark.
        std::string nullableOf(std::string lua) {
            if (!lua.ends_with('?')) {
                lua += '?';
            }
            return lua;
        }

        // The Lua name of the first inner type, or `any` when there is none.
        std::string firstInner(const TypeInfo& type, const LuaNaming& naming) {
            return type.inner.empty() ? "any" : luaType(type.inner.front(), naming);
        }

        // Pointers and optionals: the payload, which may be nil.
        std::string nullableType(const TypeInfo& type, const LuaNaming& naming) {
            return nullableOf(firstInner(type, naming));
        }

        // Element type of `T[]`: nil holes are not typed, compound types get parentheses.
        std::string elementType(std::string lua) {
            if (lua.ends_with('?')) {
                lua.pop_back();
            }
            const bool compound = lua.find_first_of(" |,") != std::string::npos || lua.starts_with("fun(");
            return compound ? "(" + lua + ")" : lua;
        }

        // Sequences as `T[]`, mappings as `table<K, V>`.
        std::string containerType(const TypeInfo& type, const LuaNaming& naming) {
            if (type.inner.size() >= 2) {
                return "table<" + luaType(type.inner[0], naming) + ", " + luaType(type.inner[1], naming) + ">";
            }
            return type.inner.empty() ? "table" : elementType(luaType(type.inner.front(), naming)) + "[]";
        }

        // The Lua names of types, joined with `, `.
        std::string joinTypes(const std::vector<TypeInfo>& types, const LuaNaming& naming) {
            std::vector<std::string> names;
            names.reserve(types.size());
            for (const TypeInfo& type : types) {
                names.push_back(luaType(type, naming));
            }
            return llvm::join(names, ", ");
        }

        // A tuple that is not a return list (inside a container): a LuaLS tuple `[A, B]`.
        std::string tupleType(const TypeInfo& type, const LuaNaming& naming) {
            return "[" + joinTypes(type.inner, naming) + "]";
        }

        // std::function as `fun(p1: A, p2: B): R`; other callables (sol::function, pointers) as `function`.
        std::string functionType(const TypeInfo& type, const LuaNaming& naming) {
            if (!llvm::StringRef(canonicalOf(type)).starts_with("std::function<")) {
                return "function";
            }
            std::vector<std::string> params;
            params.reserve(type.inner.size());
            for (std::size_t i = 0; i < type.inner.size(); ++i) {
                params.push_back("p" + std::to_string(i + 1) + ": " + luaType(type.inner[i], naming));
            }
            std::string lua = "fun(" + llvm::join(params, ", ") + ")";
            if (!type.returns.empty()) {
                lua += ": " + joinTypes(type.returns, naming);
            }
            return lua;
        }

        // sol2 handles: tables and userdata by name, anything else (object, reference, Lua value) as `any`.
        std::string luaObjectType(const TypeInfo& type, const LuaNaming& /*naming*/) {
            const llvm::StringRef canonical = canonicalOf(type);
            if (canonical.contains("table") || canonical.contains("environment")) {
                return "table";
            }
            return canonical.contains("userdata") ? "userdata" : "any";
        }

        // A record no usertype or configured name covers: its unqualified name (written, else canonical: a smart
        // pointer is spelled as written but stands for its payload), or `any` for template instances.
        std::string recordType(const TypeInfo& type, const LuaNaming& /*naming*/) {
            std::string bare = bareName(type.spelling);
            if (bare.empty()) {
                bare = bareName(canonicalOf(type));
            }
            return bare.empty() ? "any" : unqualified(bare).str();
        }

        // C++ and sol2 words of text-only types with a fixed Lua meaning (namespaces already removed).
        const llvm::StringMap<llvm::StringRef>& builtinWords() {
            static const llvm::StringMap<llvm::StringRef> words = {
                {"int", "integer"},        {"short", "integer"},    {"long", "integer"},      {"size_t", "integer"},
                {"int8_t", "integer"},     {"int16_t", "integer"},  {"int32_t", "integer"},   {"int64_t", "integer"},
                {"uint8_t", "integer"},    {"uint16_t", "integer"}, {"uint32_t", "integer"},  {"uint64_t", "integer"},
                {"float", "number"},       {"double", "number"},    {"bool", "boolean"},      {"string", "string"},
                {"string_view", "string"}, {"object", "any"},       {"function", "function"}, {"table", "table"},
            };
            return words;
        }

        // One word of a text-only type: configured, else a builtin word without `std::` / `sol::`; `const` is dropped.
        std::string textWord(llvm::StringRef word, const LuaNaming& naming) {
            if (word == "const") {
                return {};
            }
            if (std::optional<std::string> lua = configured(naming, {word})) {
                return *lua;
            }
            if (word.consume_front("std::") || word.consume_front("sol::")) {
                if (std::optional<std::string> lua = configured(naming, {word})) {
                    return *lua;
                }
            }
            const auto builtin = builtinWords().find(word);
            return builtin == builtinWords().end() ? word.str() : builtin->second.str();
        }

        // Types known only as comment text (doc-only entries): word by word, `&` and `*` dropped, `optional<T>` as
        // `T?`.
        std::string textType(const TypeInfo& type, const LuaNaming& naming) {
            llvm::StringRef text = type.spelling;
            std::string lua;
            while (!text.empty()) {
                const llvm::StringRef word = text.take_while(isQualifiedChar);
                if (word.empty()) {
                    if (text.front() != '&' && text.front() != '*') {
                        lua += text.front();
                    }
                    text = text.drop_front();
                    continue;
                }
                lua += textWord(word, naming);
                text = text.drop_front(word.size());
            }
            llvm::StringRef trimmed = llvm::StringRef(lua).trim();
            if (trimmed.consume_front("optional<") && trimmed.consume_back(">")) {
                return nullableOf(trimmed.trim().str());
            }
            return trimmed.str();
        }

        // Names a type of one kind.
        using KindNamer = std::string (*)(const TypeInfo&, const LuaNaming&);

        // Returns a fixed name for kinds that need nothing else.
        template <const char* Name>
        std::string fixed(const TypeInfo& /*type*/, const LuaNaming& /*naming*/) {
            return Name;
        }

        constexpr char Nil[] = "nil";
        constexpr char Boolean[] = "boolean";
        constexpr char Integer[] = "integer";
        constexpr char Number[] = "number";
        constexpr char String[] = "string";
        constexpr char Any[] = "any";

        // Namers in TypeKind order.
        constexpr std::array<KindNamer, 16> KindNamers = {
            fixed<Nil>, // Void
            fixed<Boolean>, // Bool
            fixed<Integer>, // Int
            fixed<Number>, // Float
            fixed<String>, // String
            fixed<Integer>, // Enum (no Lua table of its name)
            recordType, // Record
            nullableType, // Pointer
            nullableType, // Optional
            containerType, // Container
            tupleType, // Tuple
            functionType, // Function
            luaObjectType, // LuaObject
            fixed<Any>, // Variadic (the parameter itself is written `...`)
            fixed<Nil>, // Nil
            textType, // Unknown
        };
        static_assert(KindNamers.size() == static_cast<std::size_t>(TypeKind::Unknown) + 1);

        void nameCallback(CallbackSig& callback, const LuaNaming& naming);

        // Sets lua on type and everything inside it.
        void nameType(TypeInfo& type, const LuaNaming& naming) {
            for (TypeInfo& inner : type.inner) {
                nameType(inner, naming);
            }
            for (TypeInfo& returned : type.returns) {
                nameType(returned, naming);
            }
            type.lua = luaType(type, naming);
        }

        // Names the parameter types and the callbacks they receive.
        void nameParams(std::vector<Param>& params, const LuaNaming& naming) {
            for (Param& param : params) {
                nameType(param.type, naming);
                for (CallbackSig& callback : param.callback) {
                    nameCallback(callback, naming);
                }
            }
        }

        // Names a callback's arguments and returns.
        void nameCallback(CallbackSig& callback, const LuaNaming& naming) {
            nameParams(callback.params, naming);
            for (TypeInfo& returned : callback.returns) {
                nameType(returned, naming);
            }
        }

        // Names the parameters and returns of signatures.
        void nameSignatures(std::vector<FunctionSig>& signatures, const LuaNaming& naming) {
            for (FunctionSig& sig : signatures) {
                nameParams(sig.params, naming);
                for (TypeInfo& returned : sig.returns) {
                    nameType(returned, naming);
                }
            }
        }

        // Names a usertype's constructors and members.
        void nameUsertype(Usertype& usertype, const LuaNaming& naming) {
            nameSignatures(usertype.constructors, naming);
            for (auto& [key, member] : usertype.members) {
                nameType(member.type, naming);
                nameSignatures(member.overloads, naming);
                nameSignatures(member.getter, naming);
                nameSignatures(member.setter, naming);
            }
        }

        // Names the types of map entries that have one `type` field (values, aliases, script callbacks).
        template <typename Map>
        void nameTypeFields(Map& entries, const LuaNaming& naming) {
            for (auto& [key, entry] : entries) {
                nameType(entry.type, naming);
            }
        }

    } // namespace

    // Reads the Lua types file.
    bool fromJSON(const llvm::json::Value& value, std::map<std::string, std::string>& names, llvm::json::Path path) {
        llvm::json::ObjectMapper map(value, path);
        return map && map.map("names", names);
    }

    llvm::Error loadLuaTypes(const std::string& path, std::map<std::string, std::string>& names) {
        llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer = llvm::MemoryBuffer::getFile(path, true);
        if (!buffer) {
            return llvm::createStringError(buffer.getError(), "cannot read " + path);
        }
        llvm::Expected<llvm::json::Value> json = llvm::json::parse((*buffer)->getBuffer());
        if (!json) {
            return json.takeError();
        }
        llvm::json::Path::Root root(path);
        return fromJSON(*json, names, root) ? llvm::Error::success() : root.getError();
    }

    LuaNaming luaNaming(const Extraction& extraction, std::map<std::string, std::string> names) {
        LuaNaming naming;
        for (const auto& [cppType, usertype] : extraction.usertypes) {
            if (!usertype.luaName.empty()) {
                naming.usertypes.emplace(cppType, usertype.luaName);
            }
        }
        for (const auto& [name, table] : extraction.enums) {
            naming.enums.insert(name);
        }
        for (const auto& [name, alias] : extraction.aliases) {
            naming.aliases.insert(name);
        }
        naming.names = std::move(names);
        return naming;
    }

    std::string luaType(const TypeInfo& type, const LuaNaming& naming) {
        if (std::optional<std::string> named = namedType(type, naming)) {
            return *named;
        }
        return KindNamers.at(static_cast<std::size_t>(type.kind))(type, naming);
    }

    void assignLuaNames(Extraction& extraction, const LuaNaming& naming) {
        for (auto& [key, function] : extraction.functions) {
            nameSignatures(function.overloads, naming);
        }
        for (auto& [key, value] : extraction.values) {
            nameType(value.type, naming);
            if (value.docOnly) {
                // The spelling is the commented-out right side (`lua.create_named_table("options")`), not a type.
                value.type.lua = "any";
            }
        }
        for (auto& [key, usertype] : extraction.usertypes) {
            nameUsertype(usertype, naming);
        }
        nameTypeFields(extraction.aliases, naming);
        nameTypeFields(extraction.scriptCallbacks, naming);
        for (auto& [key, event] : extraction.events) {
            nameCallback(event.callback, naming);
        }
    }

} // namespace dd
