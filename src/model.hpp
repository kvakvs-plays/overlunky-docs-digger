#pragma once

#include <llvm/Support/JSON.h>

#include <compare>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// The extracted Lua API as plain data (plan section 4). JSON is the only output; toJSON() overloads write it.
namespace dd {

    // Where an extracted entity is written, so readers can jump to the binding and outputs stay comparable.
    struct SourceLoc {
        // Path relative to the --root directory, with '/' separators on every platform.
        std::string file;
        // 1-based line of the entity's first token (after macro expansion to a file location).
        unsigned line = 0;
        // 1-based column, to tell several bindings on one line apart.
        unsigned column = 0;

        auto operator<=>(const SourceLoc&) const = default;
    };

    // The `///` comment written right above an entity, with the directives that change how it is documented.
    struct Doc {
        // Comment text without the `///` markers; `Returns`, `Default`, `note:` and `Warning:` stay in the text.
        std::vector<std::string> lines;
        // `/// NoDoc`: the entity exists in Lua but is kept out of the documentation.
        bool nodoc = false;
        // `/// Deprecated`: still works, documented apart with its replacement.
        bool deprecated = false;
    };

    // What a C++ type means on the Lua side; the output stage turns it into Lua names.
    enum class TypeKind : std::uint8_t {
        Void,
        Bool,
        Int,
        Float,
        String,
        Enum,
        Record,
        Pointer,
        Optional,
        Container,
        Tuple,
        Function,
        // sol2 handles to Lua values: object, table, reference, userdata, environment, injected state.
        LuaObject,
        // sol::variadic_args and variadic results: any number of values.
        Variadic,
        // sol::lua_nil: an assignment that removes a Lua value.
        Nil,
        // Not classified yet; the parity report lists every occurrence.
        Unknown,
    };

    // One C++ type as Lua sees it.
    struct TypeInfo {
        // As written in the source, typedefs kept (`std::vector<ENT_TYPE>`).
        std::string spelling;
        // Fully resolved type; only written when it differs from the spelling.
        std::string canonical;
        // Lua meaning of the type.
        TypeKind kind = TypeKind::Unknown;
        // Element, payload or member types (container elements, optional payload, tuple members, function params).
        std::vector<TypeInfo> inner;
        // Return types of a Function kind with a known signature (std::function).
        std::vector<TypeInfo> returns;
        // Lua may pass or get nil here (optional, raw pointer, sol::object).
        bool nullable = false;
        // Injected by sol2 (sol::this_state, ...): not a Lua-visible parameter.
        bool hidden = false;
    };

    // One parameter of a bound C++ function.
    struct Param {
        // Name in the C++ declaration; empty when unnamed.
        std::string name;
        // Type of the parameter.
        TypeInfo type;
        // Part of the trailing run of nullable parameters, so a Lua call may leave it out.
        bool omittable = false;
    };

    // How a usertype method receives its object (Lua calls it as `object:method(...)`).
    enum class SelfKind : std::uint8_t {
        // A plain function: not a method, or one that ignores the object.
        None,
        // A C++ member function: the object is `this`, params lists only the Lua arguments.
        Implicit,
        // A lambda or function whose first parameter is the object (`[](Entity& e, ...)`).
        FirstParam,
    };

    // One callable form of a Lua function: a lambda, a C++ function, or one member of a sol::overload.
    struct FunctionSig {
        // Parameters in declaration order, hidden ones included.
        std::vector<Param> params;
        // Values returned to Lua: empty for void, several for tuples and pairs.
        std::vector<TypeInfo> returns;
        // Qualified name of the bound C++ function, or `lambda at <file>:<line>`.
        std::string cppTarget;
        // Comment of the C++ declaration when the target is a named function (headers document those).
        Doc doc;
        // For usertype members: how the object is passed.
        SelfKind self = SelfKind::None;
    };

    // A Lua function created by `root["a"]["name"] = <callable>`, or documented by a commented-out binding.
    struct Function {
        // Table keys before the function name; empty for globals.
        std::vector<std::string> owner;
        // Name of the function in Lua.
        std::string luaName;
        // Source text of the root when it is not the Lua state (a local sol::table); empty for the state.
        std::string root;
        // The binding statement.
        SourceLoc loc;
        // Comment above the binding statement.
        Doc doc;
        // All callable forms; several for sol::overload.
        std::vector<FunctionSig> overloads;
        // Known only from a comment such as `// lua["get_entity"] = [](uint32_t uid) -> Entity*{};`: the function
        // is written in Lua, and the comment's signature text is all there is (types stay unclassified).
        bool docOnly = false;
        // `lua["printf"] = lua.safe_script(...)`: written in Lua, so there is no C++ signature.
        bool definedInLua = false;
        // `lua["load_script"] = lua["import"]`: the keys of the aliased function, whose overloads are copied here
        // once every translation unit is parsed.
        std::vector<std::string> aliasOf;
    };

    // A Lua value that is not a function: `lua["players"] = ...`, `lua["on_frame"] = sol::lua_nil`.
    struct Value {
        // Table keys before the value name; empty for globals.
        std::vector<std::string> owner;
        // Name of the value in Lua.
        std::string luaName;
        // Source text of the root when it is not the Lua state; empty for the state.
        std::string root;
        // The binding statement.
        SourceLoc loc;
        // Comment above the binding statement.
        Doc doc;
        // Type of the assigned C++ expression.
        TypeInfo type;
        // Known only from a commented-out binding (`// lua["options"] = lua.create_named_table("options");`):
        // the type keeps the comment's text as its spelling.
        bool docOnly = false;
    };

    // What a usertype member is in Lua.
    enum class MemberKind : std::uint8_t {
        // `&T::field`: read and written directly.
        Field,
        // `sol::readonly(&T::field)`: read only.
        Readonly,
        // `sol::property(get[, set])`: accessors; getter and setter tell which exist.
        Property,
        // A callable: lambda, member function, function, sol::overload.
        Method,
        // Anything else: a constant or a Lua value.
        Value,
    };

    // One member of a usertype (plan B2).
    struct Member {
        // Key in Lua; for meta functions the sol::meta_function name (`equal_to`).
        std::string luaName;
        // What the member is.
        MemberKind kind = MemberKind::Value;
        // Keyed by a sol::meta_function instead of a name (operators, __index, __pairs).
        bool meta = false;
        // Where the member is bound (the key argument or the `ut["k"] = ...` statement).
        SourceLoc loc;
        // Comment above the binding statement.
        Doc doc;
        // Comment of the bound C++ field (methods keep theirs in FunctionSig::doc).
        Doc declDoc;
        // Value type: field type, property type (getter result), or the value's type.
        TypeInfo type;
        // Callable forms of a Method.
        std::vector<FunctionSig> overloads;
        // Accessors of a Property; empty when missing.
        std::vector<FunctionSig> getter;
        // Setter of a Property; empty when read only.
        std::vector<FunctionSig> setter;
    };

    // Identity of a member: the same key may be bound twice (sol2 keeps the last one), so the location is part of it.
    struct MemberKey {
        // Lua key.
        std::string luaName;
        // Where it is bound.
        SourceLoc loc;

        auto operator<=>(const MemberKey&) const = default;
    };

    // A C++ type exposed to Lua with `new_usertype<T>("Name", ...)`.
    struct Usertype {
        // Name in Lua.
        std::string luaName;
        // C++ type as written in new_usertype<T>.
        std::string cppType;
        // The new_usertype call.
        SourceLoc loc;
        // Comment above the new_usertype statement.
        Doc doc;
        // Comment of the C++ class.
        Doc declDoc;
        // C++ base classes from sol::bases<...>, as written.
        std::vector<std::string> bases;
        // Constructors from sol::constructors<...> or `"new", sol::initializers(...)`.
        std::vector<FunctionSig> constructors;
        // sol::no_constructor: Lua cannot create it.
        bool noConstructor = false;
        // All members, from the new_usertype arguments, `ut["k"] = v` and `lua["Name"]["k"] = v`.
        std::map<MemberKey, Member> members;
    };

    // Identity of a binding: bindings repeat across translation units (headers, templates) and must stay once.
    struct BindingKey {
        // Table keys before the name.
        std::vector<std::string> owner;
        // Lua name.
        std::string luaName;
        // Where the binding is written.
        SourceLoc loc;

        auto operator<=>(const BindingKey&) const = default;
    };

    // Everything extracted from all translation units, ordered by owner, name and location.
    struct Extraction {
        // Lua functions.
        std::map<BindingKey, Function> functions;
        // Lua values that are not functions.
        std::map<BindingKey, Value> values;
        // Usertypes by canonical C++ type: members bound through `ut["k"]` only know the C++ type.
        std::map<std::string, Usertype> usertypes;
    };

    // JSON writers; empty and default fields are left out to keep the output small and diffable.
    llvm::json::Value toJSON(const SourceLoc& loc);
    llvm::json::Value toJSON(const Doc& doc);
    llvm::json::Value toJSON(const TypeInfo& type);
    llvm::json::Value toJSON(const Param& param);
    llvm::json::Value toJSON(const FunctionSig& sig);
    llvm::json::Value toJSON(const Function& function);
    llvm::json::Value toJSON(const Value& value);
    llvm::json::Value toJSON(const Member& member);
    llvm::json::Value toJSON(const Usertype& usertype);
    llvm::json::Value toJSON(const Extraction& extraction);

} // namespace dd
