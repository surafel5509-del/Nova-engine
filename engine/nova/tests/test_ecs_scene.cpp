// ECS + scene graph tests.
#include "tests/TestHarness.h"

#include <set>
#include <string>

#include "ecs/Component.h"
#include "ecs/System.h"
#include "ecs/World.h"
#include "scene/Node.h"

using namespace nova;

// --- test components -------------------------------------------------------
//
// Components live in a *named* namespace on purpose: the registry derives the
// serialized component name from typeid(T).name(), and types in an anonymous
// namespace get compiler-mangled names that are not portable across GCC,
// Clang and MSVC. Real engine components are always named.

namespace testcomp {

struct Position {
    float x = 0, y = 0;
};
struct Velocity {
    float dx = 0, dy = 0;
};
struct Health {
    int hp = 100;
};
struct Name {
    std::string value;  // deliberately non-trivial: exercises destructors
};

}  // namespace testcomp

using testcomp::Health;
using testcomp::Name;
using testcomp::Position;
using testcomp::Velocity;

// ---------------------------------------------------------------------------
// Entity handles
// ---------------------------------------------------------------------------

NOVA_TEST(Entity, HandlesDetectStaleReferences) {
    World w;
    const Entity a = w.create();
    const Entity b = w.create();
    CHECK(a.valid() && b.valid());
    CHECK(a != b);
    CHECK(w.alive(a) && w.alive(b));

    w.destroy(a);
    CHECK(!w.alive(a));
    CHECK(w.alive(b));
    CHECK(!a.valid() == false);  // the handle object itself is still well formed

    // The slot is recycled but the old handle must stay dead forever.
    const Entity c = w.create();
    CHECK(c.index == a.index);
    CHECK(c.generation != a.generation);
    CHECK(!w.alive(a));
    CHECK(w.alive(c));
}

NOVA_TEST(Entity, DoubleDestroyIsSafe) {
    World w;
    const Entity a = w.create();
    w.destroy(a);
    w.destroy(a);  // must be a no-op, not a crash or double free
    w.destroy(Entity::invalid());
    CHECK(w.entityCount() == 0);
    CHECK(!w.alive(Entity(5, 5)));
}

NOVA_TEST(Entity, ToStringIsStable) {
    CHECK(toString(Entity(12, 3)) == "12:3");
    CHECK(toString(Entity::invalid()) == "Entity(null)");
}

// ---------------------------------------------------------------------------
// Component storage
// ---------------------------------------------------------------------------

NOVA_TEST(Ecs, AddGetRemove) {
    World w;
    const Entity e = w.create();
    CHECK(!w.has<Position>(e));

    w.add<Position>(e);
    CHECK(w.has<Position>(e));
    Position* p = w.get<Position>(e);
    CHECK(p != nullptr);
    // Freshly added components are zero initialised.
    CHECK_NEAR(p->x, 0.0, 1e-6);
    p->x = 5.0f;
    p->y = -3.0f;
    CHECK_NEAR(w.get<Position>(e)->x, 5.0, 1e-6);

    // Adding twice must not reset the value.
    w.add<Position>(e);
    CHECK_NEAR(w.get<Position>(e)->x, 5.0, 1e-6);

    w.remove<Position>(e);
    CHECK(!w.has<Position>(e));
    CHECK(w.get<Position>(e) == nullptr);
}

NOVA_TEST(Ecs, SetAndEmplace) {
    World w;
    const Entity e = w.create();
    w.set(e, Position{3.0f, 4.0f});
    CHECK(w.has<Position>(e));
    CHECK_NEAR(w.get<Position>(e)->y, 4.0, 1e-6);

    // emplace replaces an existing component entirely.
    w.emplace<Position>(e, 9.0f, 8.0f);
    CHECK_NEAR(w.get<Position>(e)->x, 9.0, 1e-6);
    CHECK_NEAR(w.get<Position>(e)->y, 8.0, 1e-6);
}

NOVA_TEST(Ecs, DestroyingEntityRemovesAllComponents) {
    World w;
    const Entity e = w.create();
    w.set(e, Position{1, 2});
    w.set(e, Health{50});
    w.set(e, Name{"hero"});
    CHECK(w.stats().totalComponents == 3);

    w.destroy(e);
    CHECK(w.stats().totalComponents == 0);
    // Reusing the slot must start clean.
    const Entity f = w.create();
    CHECK(!w.has<Position>(f));
    CHECK(!w.has<Health>(f));
    CHECK(!w.has<Name>(f));
}

NOVA_TEST(Ecs, NonTrivialComponentsAreConstructedAndDestroyed) {
    // Leaked/double-destroyed std::string shows up under ASan.
    World w;
    for (int i = 0; i < 100; ++i) {
        const Entity e = w.create();
        w.set(e, Name{"a-fairly-long-string-to-defeat-sso-" + std::to_string(i)});
    }
    for (int i = 0; i < 100; i += 2) w.destroy(Entity(static_cast<EntityIndex>(i), 1));
    for (int i = 1; i < 100; i += 2) w.destroy(Entity(static_cast<EntityIndex>(i), 1));
    CHECK(w.entityCount() == 0);
    CHECK(w.stats().totalComponents == 0);
}

NOVA_TEST(Ecs, ComponentSlotsAreIndependent) {
    World w;
    std::vector<Entity> es;
    for (int i = 0; i < 50; ++i) {
        es.push_back(w.create());
        w.set(es.back(), Position{static_cast<float>(i), static_cast<float>(i) * 2});
    }
    for (int i = 0; i < 50; ++i) {
        CHECK_NEAR(w.get<Position>(es[i])->x, i, 1e-6);
        CHECK_NEAR(w.get<Position>(es[i])->y, i * 2, 1e-6);
    }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

NOVA_TEST(Ecs, EachFiltersByRequiredComponents) {
    World w;
    for (int i = 0; i < 10; ++i) {
        const Entity e = w.create();
        w.set(e, Position{static_cast<float>(i), 0.0f});
        if (i % 2 == 0) w.set(e, Velocity{1.0f, 1.0f});
    }
    // Only even entities have Velocity.
    int seen = 0;
    w.each<Position, Velocity>([&](Entity e, Position& p, Velocity& v) {
        const float before = p.x;
        p.x += v.dx;  // mutate through the reference
        CHECK_NEAR(p.x, before + 1.0f, 1e-6);
        // Only entities created with an even index carry a Velocity.
        CHECK_NEAR(before, std::floor(before), 1e-6);
        ++seen;
    });
    CHECK(seen == 5);
    CHECK(w.count<Position, Velocity>() == 5);
    CHECK(w.count<Position>() == 10);
    CHECK(w.count<Health>() == 0);
}

NOVA_TEST(Ecs, EachConstDoesNotMutate) {
    World w;
    for (int i = 0; i < 4; ++i) {
        const Entity e = w.create();
        w.set(e, Position{static_cast<float>(i), 0.0f});
    }
    float total = 0.0f;
    w.eachConst<Position>([&](Entity, const Position& p) { total += p.x; });
    CHECK_NEAR(total, 0 + 1 + 2 + 3, 1e-5);
    // Values are unchanged after a const pass.
    float total2 = 0.0f;
    w.eachConst<Position>([&](Entity, const Position& p) { total2 += p.x; });
    CHECK_NEAR(total2, total, 1e-5);
}

NOVA_TEST(Ecs, EachAnyIsUnionNotIntersection) {
    World w;
    const Entity a = w.create();
    const Entity b = w.create();
    const Entity c = w.create();
    w.set(a, Position{});
    w.set(b, Health{});
    // c has neither.

    std::set<uint64_t> hit;
    w.eachAny<Position, Health>([&](Entity e) { hit.insert(e.id()); });
    CHECK(hit.size() == 2);
    CHECK(hit.count(a.id()) == 1);
    CHECK(hit.count(b.id()) == 1);
    CHECK(hit.count(c.id()) == 0);
}

NOVA_TEST(Ecs, QuerySkipsEntitiesDestroyedDuringIteration) {
    // A common gameplay bug: a damage system destroys entities while the
    // caller iterates. The world must not hand out stale handles.
    World w;
    std::vector<Entity> es;
    for (int i = 0; i < 10; ++i) {
        es.push_back(w.create());
        w.set(es.back(), Health{1});
    }
    int processed = 0;
    w.each<Health>([&](Entity e, Health& hp) {
        ++processed;
        hp.hp = 0;
        w.destroy(e);
    });
    CHECK(processed == 10);
    CHECK(w.entityCount() == 0);
}

NOVA_TEST(Ecs, ThreeComponentQuery) {
    World w;
    for (int i = 0; i < 8; ++i) {
        const Entity e = w.create();
        w.set(e, Position{static_cast<float>(i), 0.0f});
        w.set(e, Velocity{2.0f, 0.0f});
        if (i < 4) w.set(e, Health{10});
    }
    int n = 0;
    w.each<Position, Velocity, Health>([&](Entity, Position& p, Velocity& v, Health& h) {
        p.x += v.dx;
        h.hp -= 1;
        ++n;
    });
    CHECK(n == 4);
    CHECK(w.count<Position, Velocity, Health>() == 4);
}

NOVA_TEST(Ecs, FindWithComponent) {
    World w;
    const Entity a = w.create(), b = w.create();
    w.set(a, Health{});
    w.set(b, Health{});
    const auto found = w.findWithComponent("Health");
    CHECK(found.size() == 2);
    CHECK(found[0] == a && found[1] == b);  // sorted by handle
    CHECK(w.findWithComponent("NoSuchComponent").empty());
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

NOVA_TEST(Ecs, ComponentRegistryAssignsStableIds) {
    const ComponentId p = ComponentRegistry::typeId<Position>();
    const ComponentId h = ComponentRegistry::typeId<Health>();
    CHECK(p != h);
    // Repeated calls return the same id (function-local static).
    CHECK(ComponentRegistry::typeId<Position>() == p);
    CHECK(ComponentRegistry::instance().info(p).name == "Position");
    CHECK(ComponentRegistry::instance().info(p).size == sizeof(Position));
    CHECK(ComponentRegistry::instance().idFor("Health") == h);
    CHECK(ComponentRegistry::instance().idFor("Nope") == kInvalidComponent);
    CHECK(ComponentRegistry::instance().hasName("Position"));
}

// ---------------------------------------------------------------------------
// Scheduler
// ---------------------------------------------------------------------------

NOVA_TEST(Scheduler, RunsInStageThenOrder) {
    SystemScheduler sched;
    std::vector<std::string> order;
    auto record = [&](const char* name) {
        return [&order, name](World&, float) { order.push_back(name); };
    };
    // Registered out of order on purpose.
    sched.add("render", Stage::Late, record("render"));
    sched.add("physics", Stage::Physics, record("physics"));
    sched.add("input", Stage::Input, record("input"));
    sched.add("gameplay", Stage::Gameplay, record("gameplay"));

    World w;
    sched.update(w, 0.016f);
    CHECK(order.size() == 4);
    CHECK(order[0] == "input");
    CHECK(order[1] == "gameplay");
    CHECK(order[2] == "physics");
    CHECK(order[3] == "render");
}

NOVA_TEST(Scheduler, DisableAndReplace) {
    SystemScheduler sched;
    int a = 0, b = 0;
    sched.add("a", Stage::Gameplay, [&a](World&, float) { ++a; });
    sched.add("b", Stage::Gameplay, [&b](World&, float) { ++b; });
    World w;

    sched.setEnabled("a", false);
    sched.update(w, 0.016f);
    CHECK(a == 0 && b == 1);

    sched.setEnabled("a", true);
    // Re-registering the same name replaces the function (hot reload).
    sched.add("a", Stage::Gameplay, [&a](World&, float) { a += 10; });
    sched.update(w, 0.016f);
    CHECK(a == 10);
    CHECK(sched.size() == 2);
}

NOVA_TEST(Scheduler, StageRangeFilter) {
    SystemScheduler sched;
    int physics = 0, late = 0;
    sched.add("p", Stage::Physics, [&physics](World&, float) { ++physics; });
    sched.add("l", Stage::Late, [&late](World&, float) { ++late; });
    World w;

    // The editor's single-step button runs just the physics stage.
    sched.update(w, 0.016f, Stage::Physics, Stage::Physics);
    CHECK(physics == 1 && late == 0);
    sched.update(w, 0.016f);
    CHECK(physics == 2 && late == 1);
}

NOVA_TEST(Scheduler, ProfileAccumulates) {
    SystemScheduler sched;
    sched.add("s", Stage::Gameplay, [](World&, float) {});
    World w;
    sched.update(w, 0.016f);
    sched.update(w, 0.016f);
    const auto prof = sched.profile();
    CHECK(prof.size() == 1);
    CHECK(prof[0].name == "s");
    CHECK(prof[0].calls == 2);
    sched.resetProfile();
    CHECK(sched.profile()[0].calls == 0);
}

NOVA_TEST(Scheduler, SystemsCanReadAndWriteTheWorld) {
    // The canonical movement system.
    SystemScheduler sched;
    sched.add("movement", Stage::Gameplay, [](World& world, float dt) {
        world.each<Position, Velocity>([dt](Entity, Position& p, Velocity& v) {
            p.x += v.dx * dt;
            p.y += v.dy * dt;
        });
    });
    World w;
    const Entity e = w.create();
    w.set(e, Position{0, 0});
    w.set(e, Velocity{10, -5});

    for (int i = 0; i < 60; ++i) sched.update(w, 1.0f / 60.0f);
    CHECK_NEAR(w.get<Position>(e)->x, 10.0, 1e-3);
    CHECK_NEAR(w.get<Position>(e)->y, -5.0, 1e-3);
}

// ---------------------------------------------------------------------------
// Scene graph
// ---------------------------------------------------------------------------

NOVA_TEST(Scene, CreateAndDestroy) {
    Scene s;
    const NodeId root = s.createNode("Root");
    const NodeId a = s.createNode("A", root);
    CHECK(s.valid(root) && s.valid(a));
    CHECK(s.nodeCount() == 2);
    CHECK(s.parent(a) == root);
    CHECK(s.children(root).size() == 1);
    CHECK(s.roots().size() == 1);
    CHECK(s.roots()[0] == root);

    s.destroyNode(root);
    // Destroying a parent destroys the whole subtree.
    CHECK(!s.valid(root));
    CHECK(!s.valid(a));
    CHECK(s.nodeCount() == 0);
    CHECK(s.roots().empty());
}

NOVA_TEST(Scene, TransformComposition) {
    Scene s;
    const NodeId parent = s.createNode("Parent");
    const NodeId child = s.createNode("Child", parent);
    s.setLocalTransform(parent, Transform::fromPosition({10, 0, 0}));
    s.setLocalTransform(child, Transform::fromPosition({0, 5, 0}));

    const Vec3 wp = s.worldPosition(child);
    CHECK_NEAR_VEC3(wp, Vec3(10, 5, 0), 1e-4);
    // Local is untouched by the parent.
    CHECK_NEAR_VEC3(s.localTransform(child).position, Vec3(0, 5, 0), 1e-4);
}

NOVA_TEST(Scene, WorldTransformCacheInvalidates) {
    Scene s;
    const NodeId parent = s.createNode("P");
    const NodeId child = s.createNode("C", parent);
    s.setLocalTransform(parent, Transform::fromPosition({1, 0, 0}));
    s.setLocalTransform(child, Transform::fromPosition({0, 1, 0}));
    CHECK_NEAR_VEC3(s.worldPosition(child), Vec3(1, 1, 0), 1e-4);
    CHECK(s.dirtyTransformCount() == 0);

    s.setLocalTransform(parent, Transform::fromPosition({5, 0, 0}));
    CHECK(s.dirtyTransformCount() == 2);  // parent and child invalidated
    CHECK_NEAR_VEC3(s.worldPosition(child), Vec3(5, 1, 0), 1e-4);
}

NOVA_TEST(Scene, DeepHierarchyDoesNotOverflow) {
    // Recursive implementations die here; ours must not.
    Scene s;
    const NodeId root = s.createNode("r0");
    NodeId cur = root;
    for (int i = 1; i < 20000; ++i) cur = s.createNode("n" + std::to_string(i), cur);

    s.setLocalTransform(root, Transform::fromPosition({1, 0, 0}));
    s.setLocalTransform(cur, Transform::fromPosition({0, 1, 0}));
    // Only the root and the leaf contribute: every middle node is at the
    // origin, so the leaf lands at (1, 1, 0).
    CHECK_NEAR_VEC3(s.worldPosition(cur), Vec3(1, 1, 0), 1e-3);
    CHECK(s.depth(cur) == 19999);

    size_t count = 0;
    s.traverse(root, [&](NodeId, int) { ++count; });
    CHECK(count == 20000);
}

NOVA_TEST(Scene, ReparentRejectsCycles) {
    Scene s;
    const NodeId a = s.createNode("a");
    const NodeId b = s.createNode("b", a);
    const NodeId c = s.createNode("c", b);

    CHECK(s.reparent(c, a));            // c under a: legal
    CHECK(s.parent(c) == a);
    CHECK(!s.reparent(a, c));           // a under its own descendant: illegal
    CHECK(!s.reparent(a, a));           // a under itself: illegal
    CHECK(s.parent(a) == kInvalidNode);
    CHECK(s.reparent(c, kInvalidNode)); // back to root
    CHECK(s.roots().size() == 2);
}

NOVA_TEST(Scene, PathAndAncestors) {
    Scene s;
    const NodeId level = s.createNode("Level");
    const NodeId enemies = s.createNode("Enemies", level);
    const NodeId slime = s.createNode("Slime", enemies);

    CHECK(s.path(slime) == "Level/Enemies/Slime");
    CHECK(s.rootOf(slime) == level);
    CHECK(s.findAncestorNamed(slime, "Enemies") == enemies);
    CHECK(s.findAncestorNamed(slime, "Nope") == kInvalidNode);
    CHECK(s.findByName("Slime") == slime);
    CHECK(s.isAncestorOf(level, slime));
    CHECK(!s.isAncestorOf(slime, level));
    CHECK(s.isAncestorOf(slime, slime));
}

NOVA_TEST(Scene, RotationAccumulatesThroughHierarchy) {
    Scene s;
    const NodeId p = s.createNode("p");
    const NodeId c = s.createNode("c", p);
    s.setLocalTransform(p, Transform::from2D({0, 0}, 30.0f, {1, 1}));
    s.setLocalTransform(c, Transform::from2D({0, 0}, 60.0f, {1, 1}));
    CHECK_NEAR(s.worldRotationZ(c), 90.0, 1e-2);

    s.setWorldRotationZ(c, 0.0f);
    // Local becomes 0 - 30 = -30 so the world total is zero.
    CHECK_NEAR(s.worldRotationZ(c), 0.0, 1e-2);
    CHECK_NEAR(s.localTransform(c).euler().z, -30.0, 1e-2);
}

NOVA_TEST(Scene, WorldPositionConvertedIntoParentSpace) {
    Scene s;
    const NodeId p = s.createNode("p");
    const NodeId c = s.createNode("c", p);
    s.setLocalTransform(p, Transform::fromPosition({10, 0, 0}));
    s.setWorldPosition(c, {3, 4, 0});
    // Parent is at x=10, so the child's local x must be -7.
    CHECK_NEAR_VEC3(s.localTransform(c).position, Vec3(-7, 4, 0), 1e-4);
    CHECK_NEAR_VEC3(s.worldPosition(c), Vec3(3, 4, 0), 1e-4);
}

NOVA_TEST(Scene, WorldScaleMultipliesAndGuardsZero) {
    Scene s;
    const NodeId p = s.createNode("p");
    const NodeId c = s.createNode("c", p);

    Transform pt = Transform::fromPosition(Vec3(0, 0, 0));
    pt.scale = Vec3(2, 3, 4);
    s.setLocalTransform(p, pt);

    s.setWorldScale(c, Vec3(6, 6, 6));
    // Local scale must be world scale divided by the parent's.
    CHECK_NEAR_VEC3(s.localTransform(c).scale, Vec3(3, 2, 1.5f), 1e-4);
    CHECK_NEAR_VEC3(s.worldScale(c), Vec3(6, 6, 6), 1e-4);

    // A zeroed parent scale must not produce infinity.
    pt.scale = Vec3(0, 0, 0);
    s.setLocalTransform(p, pt);
    s.setWorldScale(c, Vec3(1, 1, 1));
    CHECK(std::isfinite(s.localTransform(c).scale.x));
    CHECK(std::isfinite(s.localTransform(c).scale.y));
}

NOVA_TEST(Scene, VisibilityAndActiveInherit) {
    Scene s;
    const NodeId p = s.createNode("p");
    const NodeId c = s.createNode("c", p);
    CHECK(s.effectivelyVisible(c));

    s.setFlag(p, NodeFlags::Visible, false);
    CHECK(s.visible(p) == false);
    CHECK(s.effectivelyVisible(c) == false);  // inherited from the parent
    s.setFlag(p, NodeFlags::Visible, true);
    CHECK(s.effectivelyVisible(c));

    s.setFlag(c, NodeFlags::Active, false);
    CHECK(!s.effectivelyActive(c));
    s.setFlag(c, NodeFlags::Active, true);
    CHECK(s.effectivelyActive(c));
}

NOVA_TEST(Scene, Properties) {
    Scene s;
    const NodeId n = s.createNode("n");
    s.setProperty(n, "script", "enemy.lua");
    s.setProperty(n, "hp", "50");
    CHECK(s.property(n, "script") == "enemy.lua");
    CHECK(s.property(n, "missing", "fallback") == "fallback");
    CHECK(s.hasProperty(n, "hp"));
    s.removeProperty(n, "hp");
    CHECK(!s.hasProperty(n, "hp"));
    CHECK(s.properties(n).size() == 1);
}

NOVA_TEST(Scene, NodeSlotRecyclingKeepsTreeConsistent) {
    Scene s;
    const NodeId a = s.createNode("a");
    const NodeId b = s.createNode("b", a);
    s.destroyNode(b);
    CHECK(s.children(a).empty());
    const NodeId c = s.createNode("c", a);
    CHECK(s.parent(c) == a);
    CHECK(s.children(a).size() == 1);
    // The recycled node must not carry the old name.
    CHECK(s.name(c) == "c");
    CHECK(!s.hasProperty(c, "stale"));
}

NOVA_TEST(Scene, NodeIdReuseAfterDeepDestroy) {
    Scene s;
    const NodeId root = s.createNode("root");
    for (int i = 0; i < 100; ++i) s.createNode("leaf", root);
    CHECK(s.nodeCount() == 101);
    s.destroyNode(root);
    CHECK(s.nodeCount() == 0);
    const NodeId fresh = s.createNode("fresh");
    CHECK(s.valid(fresh));
    CHECK(s.name(fresh) == "fresh");
    CHECK(s.roots().size() == 1);
    CHECK(s.children(fresh).empty());
}
