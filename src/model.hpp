#pragma once

#include <llvm/Support/JSON.h>

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// The extracted Lua API as plain data (plan section 4). JSON is the only output; toJSON() overloads write it.
namespace dd {

    // Version of the JSON output (schema/docs-digger.schema.json): raised with every change to its fields, so readers
    // can refuse output they do not know.
    constexpr int SchemaVersion = 2;

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
        // `/// Group: <name>`: the documentation section a function goes in, overriding config/groups.json.
        std::string group;
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
        // The type in LuaLS notation (`integer`, `Entity?`, `fun(e: Entity): boolean`), set once every translation unit
        // is parsed (lua_names.hpp), so all outputs write the same names.
        std::string lua;
    };

    // Which end of a callback's arguments a value bound by make_safe_cb comes from.
    enum class Binder : std::uint8_t {
        // An argument of the C++ caller.
        None,
        // A FrontBinder value, passed before the caller's arguments.
        Front,
        // A BackBinder value, passed after them.
        Back,
    };

    struct CallbackSig;

    // One parameter of a bound C++ function, or one argument a Lua callback receives.
    struct Param {
        // Name in the C++ declaration; empty when unnamed.
        std::string name;
        // Type of the parameter.
        TypeInfo type;
        // Part of the trailing run of nullable parameters, so a Lua call may leave it out.
        bool omittable = false;
        // For callback arguments: a value make_safe_cb adds in front of or behind the caller's arguments.
        Binder binder = Binder::None;
        // The Lua function this parameter receives: empty or one signature (C1 to C7).
        std::vector<CallbackSig> callback{};
        // The function and parameter (resolve.hpp parameterKey), to join the callback facts found where the function
        // is defined, maybe in another translation unit (not written).
        std::string key{};
    };

    // How a callback signature was found (plan C1 to C7).
    enum class CallbackOrigin : std::uint8_t {
        // A std::function<R(A...)> parameter: sol2 converts the Lua function.
        StdFunction,
        // make_safe_cb<Sig> / make_safe_clearable_cb<Sig> with binders.
        MakeSafeCb,
        // handle_function<R> called on the parameter itself (or a capture of it).
        HandleFunction,
        // The parameter is called directly (`pred(entity)`).
        DirectCall,
        // Stored in a record field and dispatched with handle_function<R> elsewhere.
        StoredHandleFunction,
        // Stored and dispatched per enum key (`set_callback(cb, ON.X)`): the signatures are in Extraction::events.
        EventDispatch,
        // A vtable hook (`set_pre_<name>` / `set_post_<name>`).
        VtableHook,
        // Only the `The callback signature is ...` prose of the doc comment.
        DocComment,
        // No pattern matched; CallbackSig::unresolved says why.
        Unresolved,
    };

    // What a Lua callback receives and returns, and what a nil return means.
    struct CallbackSig {
        // Arguments the callback receives, binder values marked.
        std::vector<Param> params;
        // Values the C++ side reads back: empty when the result is ignored.
        std::vector<TypeInfo> returns;
        // nil (or no return) is accepted.
        bool nilAllowed = false;
        // What the C++ side does with nil ("ignored", "treated as false", ...).
        std::string nilMeaning;
        // How the signature was found.
        CallbackOrigin origin = CallbackOrigin::Unresolved;
        // The record field the callback is stored in (`ScreenCallback::func`), for stored callbacks.
        std::string storedIn;
        // The enum whose value selects the signature (`ON`); the signatures are in Extraction::events.
        std::string selector;
        // Why no signature was found, for Unresolved; the pattern that stopped the search.
        std::string unresolved;
        // The `The callback signature is ...` text of the doc comment, when there is one.
        std::string prose;
        // Disagreements between dispatch sites, or between the code and the prose.
        std::vector<std::string> conflicts;
        // Where the signature was read (make_safe_cb or handle_function call); empty for type-only origins.
        SourceLoc loc;
        // For DocComment and forwarded callbacks: the Lua function whose callback this is (not written).
        std::vector<std::string> forwardedTo;
        // For forwarded callbacks: the parameter of forwardedTo that receives it (not written).
        unsigned forwardedParam = 0;
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
        // Documentation section (plan P14), chosen once every translation unit is parsed (groups.hpp).
        std::string group;
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

    // Where an enum value is known from.
    enum class EnumValueOrigin : std::uint8_t {
        // A key and value pair written in the code.
        Static,
        // A commented-out pair inside the call (`create_named_table("ENT_TYPE" //, "FLOOR_BORDERTILE", 1`): an
        // example of the values the code adds at runtime.
        DocComment,
        // Reported by a running game (`docs/game_data/lua_enums.txt`).
        RuntimeFile,
    };

    // One key of a Lua enum table.
    struct EnumValue {
        // Key in Lua; empty for a commented-out hint such as `"", ...check [entities.txt]...`.
        std::string luaName;
        // The value as written (`ON::LOGO`, `0x1`, `g_level_max_y`), or as the runtime file has it.
        std::string text;
        // The value when it is an integer known at compile time (or in the runtime file).
        std::optional<long long> number;
        // The value when it is a string.
        std::optional<std::string> string;
        // From a `/* ENUM // KEY // doc */` block.
        Doc doc;
        // Where the value is known from.
        EnumValueOrigin origin = EnumValueOrigin::Static;
        // The key in the code; empty for runtime values.
        SourceLoc loc;
    };

    // A Lua table of named constants: `create_named_table("NAME", "K", v, ...)` or `new_enum(...)` (plan B3).
    struct Enum {
        // Name of the table in Lua.
        std::string luaName;
        // The creating call; empty for tables only the runtime file knows.
        SourceLoc loc;
        // Comment above the creating call.
        Doc doc;
        // Values by key and location (a key may repeat with another origin).
        std::map<MemberKey, EnumValue> values;
        // Keys are also added by code at runtime (`lua["ENT_TYPE"][name] = item.id` in a loop).
        bool filledAtRuntime = false;
    };

    // A typedef or `using` name that Lua-visible types are written with (`CallbackId`, `ENT_TYPE`).
    struct Alias {
        // Unqualified name.
        std::string name;
        // The aliased type.
        TypeInfo type;
        // The declaration.
        SourceLoc loc;
        // Comment of the declaration.
        Doc doc;
    };

    // The Lua standard libraries one `open_libraries(...)` call opens.
    struct LibraryCall {
        // Library names (`math`, `io`).
        std::vector<std::string> names;
        // Function the call is in (`load_unsafe_libraries` opens the unsafe ones).
        std::string function;
        // The call.
        SourceLoc loc;
    };

    // A documented global that the script defines and the C++ side reads (`on_frame`, P8).
    struct ScriptCallback {
        // Name of the global.
        std::string luaName;
        // C++ type the value is read as (`sol::optional<sol::function>`).
        TypeInfo type;
        // The reading declaration.
        SourceLoc loc;
        // Comment above the read.
        Doc doc;
    };

    // A `/* NAME // KEY // doc lines */` block comment: docs for the values of enum NAME or the members of
    // usertype NAME, matched by key once every translation unit is parsed.
    struct BlockDoc {
        // Enum or usertype name (the block's first word).
        std::string target;
        // Lines after the first one, without their `//` markers; a line naming a key starts that key's docs.
        std::vector<std::string> lines;
    };

    // The Lua callback for one key of an enum-keyed dispatch: an event (`set_callback(cb, ON.FRAME)`) or a theme
    // override (`CustomTheme:override(THEME_OVERRIDE.SPAWN_LEVEL, f)`).
    struct Event {
        // Lua table of the key (`ON`).
        std::string table;
        // Key in that table (`FRAME`).
        std::string luaName;
        // Record field the callbacks are stored in (`ScreenCallback::func`).
        std::string storedIn;
        // Signature from the key's dispatch sites (merged), or unresolved.
        CallbackSig callback;
        // The key has no dispatch site of its own: the signature is the generic dispatch's (screen events).
        bool generic = false;
        // Docs of the enum value.
        Doc doc;
    };

    // A handle_function call on a stored callback, with the key that selects it (plan C3, C4).
    struct DispatchSite {
        // Record field the callback is read from (`ScreenCallback::func`).
        std::string storage;
        // Arguments, return and nil meaning at this call.
        CallbackSig signature;
        // The enum constant guarding the call (`ON::FRAME`), as `Enum::Value`.
        std::string constant;
        // A function and enum parameter guarding the call (`LuaBackend::on_pre` param 0): its callers decide.
        std::string selectorFunction;
        // Index of that parameter.
        unsigned selectorParam = 0;
        // A range-for over this container field encloses the call (`LuaBackend::load_callbacks`).
        std::string container;
        // The container field of the closest range-for around the call, which tells callbacks of the same record
        // type apart (`pre_entity_spawn_callbacks`, `post_entity_spawn_callbacks`).
        std::string loop;
    };

    // An enum argument passed to a function parameter: a constant, or the caller's own enum parameter (plan C4).
    struct SelectorEdge {
        // Called function, with template arguments.
        std::string callee;
        // Parameter index in the callee.
        unsigned param = 0;
        // The constant passed (`ON::PRE_UPDATE`); empty when a caller parameter is forwarded.
        std::string constant;
        // The forwarding caller, with template arguments.
        std::string caller;
        // Parameter index in the caller.
        unsigned callerParam = 0;

        auto operator<=>(const SelectorEdge&) const = default;
    };

    // Callback facts collected per translation unit and joined after all of them (not written).
    struct CallbackFacts {
        // What the code does with each sol::function parameter, by parameterKey.
        std::map<std::string, CallbackSig> flows;
        // Dispatch sites by location and enclosing function (template instantiations share locations).
        std::map<std::pair<SourceLoc, std::string>, DispatchSite> sites;
        // Enum arguments of calls.
        std::set<SelectorEdge> edges;
        // Container fields written under an enum constant guard: (container, constant).
        std::set<std::pair<std::string, std::string>> containerKeys;
        // Enum types of the other parameters of functions that store a callback, by storage field: the keys of
        // that storage are a Lua API (events).
        std::map<std::string, std::set<std::string>> storageSelectors;
        // Vtable hook members by usertype Lua name.
        std::map<std::string, std::vector<Member>> vtableHooks;
        // Bindings the vtable pass replaced (the registration lambdas, recorded by B1 with root `lua_type`).
        std::set<SourceLoc> consumedBindings;
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
        // Enum tables by Lua name.
        std::map<std::string, Enum> enums;
        // Typedef names under the root by name; only those some Lua-visible type uses are written.
        std::map<std::string, Alias> aliases;
        // `open_libraries(...)` calls by location.
        std::map<SourceLoc, LibraryCall> libraries;
        // Documented reads of script-defined globals, by name and location.
        std::map<MemberKey, ScriptCallback> scriptCallbacks;
        // Names of documented values on the state: the API's global variables (`state`, `players`).
        std::vector<std::string> globalVariables;
        // Callbacks of enum-keyed dispatches (events, theme overrides) by storage, table and key.
        std::map<std::tuple<std::string, std::string, std::string>, Event> events;
        // Callback facts waiting for the join after all translation units (not written).
        CallbackFacts callbackFacts;
        // Block comments waiting to be matched with enum values and members (not written).
        std::vector<BlockDoc> blockDocs;
    };

    // JSON writers; empty and default fields are left out to keep the output small and diffable.
    llvm::json::Value toJSON(const SourceLoc& loc);
    llvm::json::Value toJSON(const Doc& doc);
    llvm::json::Value toJSON(const TypeInfo& type);
    llvm::json::Value toJSON(const Param& param);
    llvm::json::Value toJSON(const CallbackSig& callback);
    llvm::json::Value toJSON(const Event& event);
    llvm::json::Value toJSON(const FunctionSig& sig);
    llvm::json::Value toJSON(const Function& function);
    llvm::json::Value toJSON(const Value& value);
    llvm::json::Value toJSON(const Member& member);
    llvm::json::Value toJSON(const Usertype& usertype);
    llvm::json::Value toJSON(const EnumValue& value);
    llvm::json::Value toJSON(const Enum& table);
    llvm::json::Value toJSON(const Alias& alias);
    llvm::json::Value toJSON(const LibraryCall& call);
    llvm::json::Value toJSON(const ScriptCallback& callback);
    llvm::json::Value toJSON(const Extraction& extraction);

} // namespace dd
