// Unit tests of the Lua type names (lua_names.hpp): one case per rule of luaType().
#include "lua_names.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

    // A type with the given facts; canonical stays empty (equal to the spelling) unless given.
    dd::TypeInfo type(dd::TypeKind kind, std::string spelling, std::vector<dd::TypeInfo> inner = {},
                      std::string canonical = {}) {
        dd::TypeInfo info;
        info.kind = kind;
        info.spelling = std::move(spelling);
        info.canonical = std::move(canonical);
        info.inner = std::move(inner);
        return info;
    }

    // Lookup tables shared by the tests: one usertype, two enum tables, one alias and two configured names.
    dd::LuaNaming naming() {
        dd::LuaNaming result;
        result.usertypes = {{"Movable", "Movable"}, {"Entity", "Entity"}};
        result.enums = {"LAYER", "ENT_TYPE"};
        result.aliases = {"CallbackId"};
        result.names = {{"ImVec2", "Vec2"}, {"uint32_t", "integer"}};
        return result;
    }

    using dd::TypeKind;

    TEST(LuaNames, Scalars) {
        EXPECT_EQ(dd::luaType(type(TypeKind::Int, "uint8_t"), naming()), "integer");
        EXPECT_EQ(dd::luaType(type(TypeKind::Float, "float"), naming()), "number");
        EXPECT_EQ(dd::luaType(type(TypeKind::Bool, "bool"), naming()), "boolean");
        EXPECT_EQ(dd::luaType(type(TypeKind::String, "std::string_view"), naming()), "string");
        EXPECT_EQ(dd::luaType(type(TypeKind::Void, "void"), naming()), "nil");
    }

    TEST(LuaNames, EnumsAndAliasesByName) {
        EXPECT_EQ(dd::luaType(type(TypeKind::Enum, "LAYER"), naming()), "LAYER");
        EXPECT_EQ(dd::luaType(type(TypeKind::Enum, "PRNG::PRNG_CLASS"), naming()), "integer");
        EXPECT_EQ(dd::luaType(type(TypeKind::Int, "const ENT_TYPE &", {}, "unsigned int"), naming()), "ENT_TYPE");
        EXPECT_EQ(dd::luaType(type(TypeKind::Int, "CallbackId", {}, "int"), naming()), "CallbackId");
    }

    TEST(LuaNames, Records) {
        EXPECT_EQ(dd::luaType(type(TypeKind::Record, "const Movable &", {}, "Movable"), naming()), "Movable");
        EXPECT_EQ(dd::luaType(type(TypeKind::Record, "ImVec2"), naming()), "Vec2");
        EXPECT_EQ(dd::luaType(type(TypeKind::Record, "Detail::Unbound"), naming()), "Unbound");
        EXPECT_EQ(dd::luaType(type(TypeKind::Record, "Holder<int>"), naming()), "any");
    }

    TEST(LuaNames, Nullable) {
        const dd::TypeInfo entity = type(TypeKind::Record, "Entity");
        EXPECT_EQ(dd::luaType(type(TypeKind::Pointer, "Entity *", {entity}), naming()), "Entity?");
        EXPECT_EQ(dd::luaType(type(TypeKind::Optional, "std::optional<int>", {type(TypeKind::Int, "int")}), naming()),
                  "integer?");
        const dd::TypeInfo optionalPointer =
            type(TypeKind::Optional, "std::optional<Entity *>", {type(TypeKind::Pointer, "Entity *", {entity})});
        EXPECT_EQ(dd::luaType(optionalPointer, naming()), "Entity?");
    }

    TEST(LuaNames, Containers) {
        const dd::TypeInfo entity = type(TypeKind::Pointer, "Entity *", {type(TypeKind::Record, "Entity")});
        EXPECT_EQ(dd::luaType(type(TypeKind::Container, "std::vector<Entity *>", {entity}), naming()), "Entity[]");
        const dd::TypeInfo map = type(TypeKind::Container, "std::map<std::string, ENT_TYPE>",
                                      {type(TypeKind::String, "std::string"), type(TypeKind::Int, "ENT_TYPE")});
        EXPECT_EQ(dd::luaType(map, naming()), "table<string, ENT_TYPE>");
        const dd::TypeInfo pair = type(TypeKind::Tuple, "std::pair<int, float>",
                                       {type(TypeKind::Int, "int"), type(TypeKind::Float, "float")});
        EXPECT_EQ(dd::luaType(type(TypeKind::Container, "std::vector<std::pair<int, float>>", {pair}), naming()),
                  "([integer, number])[]");
    }

    TEST(LuaNames, Functions) {
        dd::TypeInfo function = type(TypeKind::Function, "std::function<bool (Movable *)>",
                                     {type(TypeKind::Pointer, "Movable *", {type(TypeKind::Record, "Movable")})});
        function.returns.push_back(type(TypeKind::Bool, "bool"));
        EXPECT_EQ(dd::luaType(function, naming()), "fun(p1: Movable?): boolean");
        EXPECT_EQ(dd::luaType(type(TypeKind::Function, "std::function<void ()>"), naming()), "fun()");
        EXPECT_EQ(
            dd::luaType(type(TypeKind::Function, "sol::function", {}, "sol::basic_protected_function<>"), naming()),
            "function");
    }

    TEST(LuaNames, LuaObjects) {
        EXPECT_EQ(dd::luaType(type(TypeKind::LuaObject, "sol::object", {}, "sol::basic_object<>"), naming()), "any");
        EXPECT_EQ(dd::luaType(type(TypeKind::LuaObject, "sol::table", {}, "sol::basic_table_core<>"), naming()),
                  "table");
        EXPECT_EQ(dd::luaType(type(TypeKind::Variadic, "sol::variadic_args"), naming()), "any");
    }

    TEST(LuaNames, TextOnlyTypes) {
        EXPECT_EQ(dd::luaType(type(TypeKind::Unknown, "std::string"), naming()), "string");
        EXPECT_EQ(dd::luaType(type(TypeKind::Unknown, "uint32_t"), naming()), "integer");
        EXPECT_EQ(dd::luaType(type(TypeKind::Unknown, "Entity*"), naming()), "Entity");
        EXPECT_EQ(dd::luaType(type(TypeKind::Unknown, "sol::optional<CallbackId>"), naming()), "CallbackId?");
        EXPECT_EQ(dd::luaType(type(TypeKind::Unknown, "const ImVec2&"), naming()), "Vec2");
        EXPECT_EQ(dd::luaType(type(TypeKind::Unknown, "optional<int>"), naming()), "integer?");
        EXPECT_EQ(dd::luaType(type(TypeKind::Unknown, "std::vector<float>"), naming()), "vector<number>");
    }

} // namespace
