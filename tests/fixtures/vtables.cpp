// C5: vtable hooks. Imitates overlunky's hookable_vtable.hpp/.inl (LiteralString keys, VTableEntry, register_hooks,
// a derived table with an inheriting constructor) and vtables_lua.cpp.
#include <sol/sol.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

template <std::size_t N>
struct LiteralString
{
    consteval LiteralString() = default;
    consteval LiteralString(const char (&str)[N])
    {
        std::copy_n(str, N, Str);
    }
    template <class T>
    constexpr T to() const
    {
        return T{Str, Size};
    }
    char Str[N]{};
    std::size_t Size{N - 1};
};
template <LiteralString Left, LiteralString Right>
consteval auto JoinLiteralStrings()
{
    LiteralString<Left.Size + Right.Size + 1> result{};
    std::copy_n(Left.Str, Left.Size, result.Str);
    std::copy_n(Right.Str, Right.Size + 1, result.Str + Left.Size);
    return result;
}

template <class... T>
struct FrontBinder
{
};
template <class... T>
struct BackBinder
{
};
template <class FunT, class CallableT, class... FrontTs, class... BackTs>
std::function<FunT> make_safe_clearable_cb(CallableT&& cb, std::uint32_t id, std::uint32_t aux_id, FrontBinder<FrontTs...> front, BackBinder<BackTs...> back)
{
    (void)cb, (void)id, (void)aux_id, (void)front, (void)back;
    return {};
}

template <class T>
struct MemberFun;
template <class R, class C, class... A>
struct MemberFun<R (C::*)(A...)>
{
    using BaseLessType = R(A...);
};
template <auto fun>
using MemFun = typename MemberFun<decltype(fun)>::BaseLessType;

template <LiteralString Name, std::uint32_t Index, class Signature, class BindBack = BackBinder<>>
struct VTableEntry
{
};

template <class Entry, class SelfT>
struct VTableEntryImpl;
template <LiteralString Name, std::uint32_t Index, class RetT, class... ArgsT, class BindBack, class SelfT>
struct VTableEntryImpl<VTableEntry<Name, Index, RetT(ArgsT...), BindBack>, SelfT>
{
    using PreRetT = std::conditional_t<std::is_void_v<RetT>, bool, std::optional<RetT>>;
    static void register_hooks(auto lua_type)
    {
        lua_type[JoinLiteralStrings<"set_pre_", Name>().template to<std::string_view>()] = [](SelfT* self, sol::function fun) -> std::uint32_t
        {
            auto hook = make_safe_clearable_cb<PreRetT(SelfT*, ArgsT...)>(std::move(fun), Index, 0, FrontBinder<>{}, BindBack{});
            (void)self, (void)hook;
            return Index;
        };
        lua_type[JoinLiteralStrings<"set_post_", Name>().template to<std::string_view>()] = [](SelfT* self, sol::function fun) -> std::uint32_t
        {
            auto hook = make_safe_clearable_cb<void(SelfT*, ArgsT...)>(std::move(fun), Index, 0, FrontBinder<>{}, BindBack{});
            (void)self, (void)hook;
            return Index;
        };
    }
};

template <class SelfT, class... Entries>
struct HookableVTable
{
    HookableVTable(sol::state& lua, auto lua_type, std::string table_name)
    {
        (VTableEntryImpl<Entries, SelfT>::register_hooks(lua_type), ...);
        lua_type["clear_virtual"] = [](SelfT* self, std::uint32_t callback_id)
        { (void)self, (void)callback_id; };
        (void)lua, (void)table_name;
    }
};
template <class SelfT, class... BaseEntries, class... Entries>
struct HookableVTable<SelfT, HookableVTable<SelfT, BaseEntries...>, Entries...> : HookableVTable<SelfT, Entries...>
{
    using HookableVTable<SelfT, Entries...>::HookableVTable;
};

struct RenderContext
{
    int layer;
};
struct Entity
{
    int uid;
    /// Kills the entity.
    virtual void kill(bool destroy_corpse, Entity* responsible)
    {
        (void)destroy_corpse, (void)responsible;
    }
    /// Can the entity be pushed.
    virtual bool can_be_pushed()
    {
        return false;
    }
};
struct Movable : Entity
{
    /// How much damage the movable deals.
    virtual std::uint8_t get_damage()
    {
        return 0;
    }
};

void register_vtables(sol::state& lua)
{
    lua.new_usertype<Entity>("Entity", "uid", &Entity::uid);
    lua.new_usertype<Movable>("Movable", sol::base_classes, sol::bases<Entity>());

    using EntityVTable = HookableVTable<
        Entity,
        VTableEntry<"dtor", 0, void()>,
        VTableEntry<"kill", 3, MemFun<&Entity::kill>>,
        /// NoDoc
        VTableEntry<"pushable", 10, MemFun<&Entity::can_be_pushed>>,
        VTableEntry<"render", 12, void(float), BackBinder<RenderContext>>>;
    static EntityVTable entity_vtable(lua, lua["Entity"], "ENTITY_OVERRIDE");

    using MovableVTable = HookableVTable<
        Entity,
        EntityVTable,
        VTableEntry<"get_damage", 43, MemFun<&Movable::get_damage>>>;
    static MovableVTable movable_vtable(lua, lua["Movable"], "ENTITY_OVERRIDE");
}
