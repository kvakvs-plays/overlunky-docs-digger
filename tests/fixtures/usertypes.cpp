// B2 usertypes: new_usertype arguments, later ut["k"] = v members, and lua["Name"]["k"] bindings.
#include <sol/sol.hpp>

#include <cstdint>
#include <string>

/// Base of everything in a level.
struct Entity
{
    /// Unique id of the entity.
    int32_t uid;
    union
    {
        /// Flags as one number.
        uint32_t flags;
        struct
        {
            /// Low half of the flags.
            uint16_t low;
            uint16_t high;
        };
    };
    float x;

    /// Moves the entity by an offset.
    void move(float dx, float dy);
    float distance() const;
};

struct Movable : Entity
{
    float velocity;
};

/// RGBA color.
struct Color
{
    /// Black, fully opaque.
    Color();
    /// From components in 0..1.
    Color(float r, float g, float b, float a);
    bool operator==(const Color& other) const;
    float r;
    float g;
    static Color white();
};

struct Particle
{
    /// Horizontal position, in tiles.
    float* x;
};

template <auto MemberT>
auto MakeParticleMemberAccess()
{
    return sol::property(
        [](const Particle& p)
        { return *(p.*MemberT); },
        [](Particle& p, float v)
        { *(p.*MemberT) = v; });
};

Color make_color(std::string name);

struct Items
{
    /// Index of leader player in coop.
    uint8_t leader;
    /// Number of items.
    int count;
    /// Set while paused.
    uint8_t paused;
    /// Splits the items in two.
    int split();
    /// Total weight of the items.
    int weight() const;
    /// Reads the pause flag.
    bool is_paused() const;
};

template <auto fun>
struct Forward
{
    static int call(Items& self)
    {
        return (self.*fun)();
    }
};

template <auto fun>
auto self_return()
{
    using FunT = decltype(fun);
    static_assert(std::is_member_function_pointer_v<FunT>);
    return &Forward<fun>::call;
}

void register_usertypes(sol::state& lua)
{
    /// A color in the 0..1 range.
    auto color_type = lua.new_usertype<Color>("Color", sol::constructors<Color(), Color(float, float, float, float)>{}, //
                                              sol::meta_function::equal_to,
                                              &Color::operator==);
    color_type["r"] = &Color::r;
    color_type["g"] = sol::readonly(&Color::g);
    color_type["white"] = &Color::white;
    /// Builds a color from a name.
    color_type["from_name"] = make_color;

    lua.new_usertype<Entity>(
        "Entity",
        sol::no_constructor,
        "uid",
        &Entity::uid,
        "flags",
        &Entity::flags,
        "low",
        &Entity::low,
        "move",
        &Entity::move,
        "distance",
        sol::overload(&Entity::distance, [](Entity& e, Entity& other) -> float
                      { return e.x - other.x; }));

    auto overlay = sol::property([](Entity& e) -> Entity*
                                 { return &e; },
                                 [](Entity& e, Entity* other)
                                 { e.x = other == nullptr ? 0.0f : other->x; });
    auto movable_type = lua.new_usertype<Movable>("Movable", sol::base_classes, sol::bases<Entity>());
    movable_type["velocity"] = &Movable::velocity;
    movable_type["overlay"] = std::move(overlay);
    movable_type["abs_x"] = sol::property([](Movable& m) -> float
                                          { return m.x; });
    /// NoDoc
    movable_type["internal"] = [](Movable& m)
    { return m.uid; };

    lua.new_usertype<Particle>("Particle", "new", sol::initializers([](Particle& p) { p.x = nullptr; }), "x",
                               MakeParticleMemberAccess<&Particle::x>());

    auto items_type = lua.new_usertype<Items>(
        "Items",
        "leader",
        // &Items::leader,
        sol::property([](Items& s) -> int
                      { return s.leader + 1; }),
        "split",
        self_return<&Items::split>(),
        "count",
        // &Items::count); // for the autodoc
        [](Items& s)
        { return s.count; },
        "weight",
        [](Items& s)
        { return s.weight() * 2; });
    // items_type["paused"] = &Items::paused;
    /// NoDoc
    items_type["paused"] = sol::property(&Items::is_paused);

    /// Casts an entity to a movable.
    lua["Entity"]["as_movable"] = [](Entity& e) -> Movable*
    { return static_cast<Movable*>(&e); };
    /// Brightest channel value.
    color_type["max_channel"] = sol::var(1.0f);
}
