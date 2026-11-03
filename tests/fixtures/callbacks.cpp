// C1-C4, C6, C7: callback signatures from std::function types, make_safe_cb, direct and stored handle_function
// calls, enum-keyed dispatch (events), nil semantics and the prose fallback. Imitates overlunky's lua_vm.cpp,
// lua_backend.cpp, safe_cb.hpp and handle_lua_function.hpp.
#include <sol/sol.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using CallbackId = int;
struct Entity
{
    int uid;
};
struct RenderContext
{
    int layer;
};
struct LuaBackend;

template <class Ret, class... Args>
std::conditional_t<std::is_void_v<Ret>, bool, std::optional<Ret>> handle_function(LuaBackend* backend, sol::function func, Args&&... args);

template <class T>
struct Binder
{
    T val;
};
template <class... T>
struct FrontBinder : Binder<T>...
{
};
template <class... T>
struct BackBinder : Binder<T>...
{
};
template <class... Ts>
FrontBinder(Ts&&...) -> FrontBinder<Ts...>;
template <class... Ts>
BackBinder(Ts&&...) -> BackBinder<Ts...>;

template <class FunT, class CallableT, class... FrontTs, class... BackTs>
std::function<FunT> make_safe_cb(CallableT&& cb, FrontBinder<FrontTs...> front = FrontBinder<>{}, BackBinder<BackTs...> back = BackBinder<>{})
{
    (void)cb, (void)front, (void)back;
    return {};
}

enum class ON
{
    FRAME,
    LEVEL,
    LOAD,
    RENDER_PRE,
    LOGO,
};

struct ScreenCallback
{
    sol::function func;
    ON screen;
};
struct IntervalCallback
{
    sol::function func;
    int interval;
};
struct OrphanCallback
{
    sol::function func;
};
struct SpawnCallback
{
    int id;
    sol::function func;
};

struct LuaBackend
{
    std::map<int, ScreenCallback> callbacks;
    std::map<int, ScreenCallback> load_callbacks;
    std::map<int, IntervalCallback> timers;
    std::vector<OrphanCallback> orphans;
    std::vector<SpawnCallback> pre_spawn_callbacks;
    std::vector<SpawnCallback> post_spawn_callbacks;
    int cbcount = 0;

    void update(int screen);
    bool on_pre(ON event);
    void store_command(sol::function command);
    int pre_spawn(float x);
    void post_spawn(Entity* entity);
};
LuaBackend* get_backend();
void register_elsewhere(sol::function command);

void LuaBackend::update(int screen)
{
    for (auto& [id, callback] : load_callbacks)
    {
        handle_function<void>(this, callback.func, std::string("load"));
    }
    for (auto& [id, callback] : callbacks)
    {
        if ((ON)screen == callback.screen)
        {
            handle_function<void>(this, callback.func);
        }
        else
        {
            switch (callback.screen)
            {
            case ON::FRAME:
            {
                handle_function<void>(this, callback.func);
                break;
            }
            default:
                break;
            }
        }
        if (callback.screen == ON::LEVEL)
        {
            handle_function<void>(this, callback.func, screen);
        }
    }
    for (auto& [id, timer] : timers)
    {
        std::optional<bool> keep_going = handle_function<bool>(this, timer.func);
        if (!keep_going.value_or(true))
        {
            timer.interval = 0;
        }
    }
}

bool LuaBackend::on_pre(ON event)
{
    bool skip = false;
    RenderContext render_ctx{};
    for (auto& [id, callback] : callbacks)
    {
        if (callback.screen == event)
        {
            skip |= handle_function<bool>(this, callback.func, render_ctx).value_or(false);
        }
    }
    return skip;
}

int LuaBackend::pre_spawn(float x)
{
    for (auto& callback : pre_spawn_callbacks)
    {
        if (auto replacement = handle_function<int>(this, callback.func, x))
            return *replacement;
    }
    return 0;
}

void LuaBackend::post_spawn(Entity* entity)
{
    for (auto& callback : post_spawn_callbacks)
    {
        handle_function<void>(this, callback.func, entity);
    }
}

void LuaBackend::store_command(sol::function command)
{
    orphans.push_back(OrphanCallback{std::move(command)});
}

bool pre_event(ON event)
{
    auto run = [=]()
    { return get_backend()->on_pre(event); };
    return run();
}

void game_loop()
{
    pre_event(ON::RENDER_PRE);
}

void register_callbacks(sol::state& lua)
{
    lua.create_named_table("ON", "FRAME", ON::FRAME, "LEVEL", ON::LEVEL, "LOAD", ON::LOAD, "RENDER_PRE", ON::RENDER_PRE, "LOGO", ON::LOGO);
    /* ON
    // FRAME
    // Runs every frame.
    // LEVEL
    // Params: int screen
    // Runs on a level.
    // LOAD
    // Params: LoadContext load_ctx, int extra
    // Runs when loading.
    // RENDER_PRE
    // Params: RenderContext render_ctx
    // Return: `bool skip`
    // Runs before rendering.
    // LOGO
    // Runs on the logo screen.
    */

    /// C4: the signature depends on the event.
    lua["set_callback"] = [](sol::function cb, ON event) -> CallbackId
    {
        LuaBackend* backend = get_backend();
        auto luaCb = ScreenCallback{cb, event};
        if (luaCb.screen == ON::LOAD)
            backend->load_callbacks[backend->cbcount] = luaCb;
        else
            backend->callbacks[backend->cbcount] = luaCb;
        return backend->cbcount++;
    };
    /// C3: stored, dispatched elsewhere; nil keeps the timer (value_or on a local).
    /// <br/>The callback signature is bool interval()
    lua["set_interval"] = [](sol::function cb, int frames) -> CallbackId
    {
        LuaBackend* backend = get_backend();
        backend->timers[backend->cbcount] = IntervalCallback{cb, frames};
        return backend->cbcount++;
    };
    /// C2 with binders: the prose disagrees on the parameter count.
    /// <br/>The callback signature is bool render(Entity self)
    lua["set_render"] = [](int uid, sol::function fun) -> CallbackId
    {
        auto safe = make_safe_cb<bool(Entity*)>(
            std::move(fun),
            FrontBinder{[]()
                        { return RenderContext{}; }},
            BackBinder{uid});
        (void)safe;
        return uid;
    };
    /// C2 with an optional result.
    lua["set_spawn"] = [](sol::function fun) -> CallbackId
    {
        std::function<std::optional<int>(float, float)> safe = make_safe_cb<std::optional<int>(float, float)>(std::move(fun));
        (void)safe;
        return 0;
    };
    /// Direct handle_function on a capture; the result is returned (nil passes on).
    lua["set_open"] = [](int uid, sol::function fun) -> std::function<std::optional<bool>(Entity*)>
    {
        (void)uid;
        return [fun = std::move(fun)](Entity* opener) -> std::optional<bool>
        { return handle_function<bool>(get_backend(), fun, opener); };
    };
    /// The function `bool is_valid(float x, float y)` filters, the function `nil do_spawn(float x, float y)` spawns.
    lua["define_spawn"] = [](sol::function do_spawn, sol::function is_valid)
    {
        auto valid = make_safe_cb<bool(float, float)>(std::move(is_valid));
        auto spawn = make_safe_cb<void(float, float)>(std::move(do_spawn));
        (void)valid, (void)spawn;
    };
    /// Direct call: the result converted to bool.
    lua["filter"] = [](std::vector<int> uids, sol::function predicate) -> std::vector<int>
    {
        std::vector<int> kept;
        for (int uid : uids)
        {
            if (static_cast<bool>(predicate(uid)))
                kept.push_back(uid);
        }
        return kept;
    };
    /// C1: the signature is the type.
    lua["sort"] = [](std::vector<int> uids, std::function<bool(int, int)> less) -> std::vector<int>
    {
        (void)less;
        return uids;
    };
    /// Followed into a function defined here: stored in a field nothing reads.
    lua["store_command"] = [](sol::function command)
    { get_backend()->store_command(std::move(command)); };
    /// Passed to a function defined elsewhere: unresolved, the prose fills in.
    /// <br/>The callback signature is nil command(string text)
    lua["register_command"] = [](sol::function command)
    { register_elsewhere(std::move(command)); };
    /// C3: one record type in two containers; each binding gets the sites of its own container.
    lua["set_pre_spawn"] = [](sol::function cb) -> CallbackId
    {
        LuaBackend* backend = get_backend();
        backend->pre_spawn_callbacks.push_back(SpawnCallback{backend->cbcount, std::move(cb)});
        return backend->cbcount++;
    };
    lua["set_post_spawn"] = [](sol::function cb) -> CallbackId
    {
        LuaBackend* backend = get_backend();
        backend->post_spawn_callbacks.push_back(SpawnCallback{backend->cbcount, std::move(cb)});
        return backend->cbcount++;
    };
    /// Forwarded to another Lua function: copies its callback.
    lua["set_render_compat"] = [&lua](int uid, sol::function fun) -> CallbackId
    { return lua["set_render"](uid, std::move(fun)); };
}
