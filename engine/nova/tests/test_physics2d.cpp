// 2D physics tests: integration, collision, stacking, queries.
#include "tests/TestHarness.h"

#include <cmath>

#include "physics/Physics2D.h"

using namespace nova;
using namespace nova::physics2d;

namespace {

Body makeBox(float x, float y, float halfW = 0.5f, float halfH = 0.5f, float mass = 1.0f) {
    Body b;
    b.position = {x, y};
    b.polygon = ConvexPolygon::box(halfW, halfH);
    b.mass.mass = mass;
    b.restitution = 0.0f;
    b.friction = 0.4f;
    return b;
}

Body makeCircle(float x, float y, float radius = 0.5f, float mass = 1.0f) {
    Body b;
    b.position = {x, y};
    b.shape = ColliderShape::Circle;
    b.radius = radius;
    b.mass.mass = mass;
    b.restitution = 0.0f;
    b.friction = 0.4f;
    return b;
}

Body makeStaticBox(float x, float y, float halfW = 5.0f, float halfH = 0.5f) {
    Body b = makeBox(x, y, halfW, halfH);
    b.type = BodyType::Static;
    return b;
}

/// Runs the world for [seconds] at 60 Hz.
void run(World& w, float seconds) {
    const int steps = static_cast<int>(std::lround(seconds * 60.0f));
    for (int i = 0; i < steps; ++i) w.step(1.0f / 60.0f);
}

}  // namespace

// ---------------------------------------------------------------------------
// Polygon construction
// ---------------------------------------------------------------------------

NOVA_TEST(Physics2D, PolygonWindingIsNormalised) {
    // Clockwise input must be reversed to counter-clockwise.
    ConvexPolygon cw({{0, 0}, {0, 1}, {1, 1}, {1, 0}});
    double area = 0.0;
    for (size_t i = 0; i < cw.vertices.size(); ++i) {
        const Vec2& p = cw.vertices[i];
        const Vec2& q = cw.vertices[(i + 1) % cw.vertices.size()];
        area += static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y;
    }
    CHECK(area > 0.0);
    // A unit square's centroid is the origin.
    ConvexPolygon box = ConvexPolygon::box(0.5f, 0.5f);
    CHECK_NEAR(box.centroid.x, 0.0, 1e-5);
    CHECK_NEAR(box.centroid.y, 0.0, 1e-5);
    CHECK_NEAR(box.bounds().width(), 1.0, 1e-5);

    // A regular hexagon has the right area and a zero centroid.
    ConvexPolygon hex = ConvexPolygon::regular(1.0f, 6);
    CHECK(hex.vertices.size() == 6);
    CHECK_NEAR(hex.centroid.length(), 0.0, 1e-4);
    // Vertices lie on the circle.
    for (const Vec2& v : hex.vertices) CHECK_NEAR(v.length(), 1.0, 1e-4);
}

NOVA_TEST(Physics2D, SatFindsMinimumAxis) {
    // Two unit boxes overlapping by 0.5 on x.
    ConvexPolygon box = ConvexPolygon::box(0.5f, 0.5f);
    Vec2 n, p;
    float depth = 0.0f;
    CHECK(satTest(box, {0, 0}, 0.0f, box, {0.75f, 0}, 0.0f, n, depth, p));
    CHECK_NEAR(depth, 0.25f, 1e-4);
    CHECK_NEAR(std::fabs(n.x), 1.0, 1e-4);  // axis is horizontal
    CHECK(n.x > 0.0f);                     // points from A to B

    // Disjoint boxes do not collide.
    CHECK(!satTest(box, {0, 0}, 0.0f, box, {5, 0}, 0.0f, n, depth, p));

    // Rotated boxes still find a valid axis (a diagonal overlap).
    CHECK(satTest(box, {0, 0}, 45.0f, box, {0.3f, 0.3f}, 45.0f, n, depth, p));
    CHECK(depth > 0.0f);
}

// ---------------------------------------------------------------------------
// Integration
// ---------------------------------------------------------------------------

NOVA_TEST(Physics2D, GravityAcceleratesBodies) {
    World w({0.0f, -10.0f});
    const BodyId b = w.createBody(makeBox(0, 100));
    run(w, 1.0f);

    const Body* body = w.body(b);
    // Semi-implicit Euler over the world's sub-steps: velocity is integrated
    // first, then position is advanced by the *new* velocity, so the fall is
    // slightly short of the exact g*t^2/2. Reproduce the same sub-stepped sum
    // here rather than hard-coding a number.
    const int frames = 60;
    const int sub = std::max(1, w.subSteps);
    const float h = (1.0f / 60.0f) / static_cast<float>(sub);
    float y = 100.0f, v = 0.0f;
    for (int f = 0; f < frames; ++f) {
        for (int k = 0; k < sub; ++k) {
            v += -10.0f * h;
            y += v * h;
        }
    }
    CHECK_NEAR(body->position.y, y, 1e-3);
    CHECK_NEAR(body->linearVelocity.y, -10.0, 0.05);
    CHECK_NEAR(body->mass.mass, 1.0, 1e-4);
    CHECK(body->mass.invMass > 0.0f);
}

NOVA_TEST(Physics2D, StaticBodiesDoNotMove) {
    World w({0.0f, -10.0f});
    const BodyId s = w.createBody(makeStaticBox(0, 0));
    run(w, 2.0f);
    CHECK_NEAR(w.body(s)->position.y, 0.0, 1e-6);
    CHECK_NEAR(w.body(s)->mass.invMass, 0.0, 1e-9);
}

NOVA_TEST(Physics2D, ForcesAndDamping) {
    World w({0, 0});
    const BodyId b = w.createBody(makeBox(0, 0));
    w.body(b)->linearDamping = 2.0f;

    for (int i = 0; i < 60; ++i) {
        w.applyForce(b, {10.0f, 0.0f});
        w.step(1.0f / 60.0f);
    }
    // Drag opposes the applied force, so the speed settles below the
    // undamped value it would reach.
    const float speed = w.body(b)->linearVelocity.x;
    CHECK(speed > 0.0f);
    CHECK(speed < 10.0f / 2.0f + 0.1f);

    // Forces are cleared after each step (they are per-frame, not persistent).
    const float before = w.body(b)->linearVelocity.x;
    w.step(1.0f / 60.0f);
    CHECK(w.body(b)->linearVelocity.x < before);  // only damping acted
}

NOVA_TEST(Physics2D, ImpulseChangesMomentum) {
    World w({0, 0});
    const BodyId b = w.createBody(makeBox(0, 0, 0.5f, 0.5f, 2.0f));
    CHECK_NEAR(w.body(b)->mass.mass, 2.0, 1e-3);
    w.applyImpulse(b, {4.0f, 0.0f});
    // dv = J / m = 4 / 2 = 2
    CHECK_NEAR(w.body(b)->linearVelocity.x, 2.0, 1e-3);
}

NOVA_TEST(Physics2D, OffCentreImpulseSpinsTheBody) {
    World w({0, 0});
    const BodyId b = w.createBody(makeBox(0, 0, 0.5f, 0.5f, 1.0f));
    w.applyImpulseAtPoint(b, {0.0f, 1.0f}, {0.5f, 0.0f});
    CHECK(std::fabs(w.body(b)->angularVelocity) > 1.0f);
    CHECK(w.body(b)->linearVelocity.length() > 0.0f);
}

NOVA_TEST(Physics2D, MassChangePreservesMomentum) {
    World w({0, 0});
    const BodyId b = w.createBody(makeBox(0, 0, 0.5f, 0.5f, 1.0f));
    w.setVelocity(b, {5.0f, 0.0f});
    CHECK_NEAR(w.body(b)->mass.mass, 1.0, 1e-3);
    w.setMass(b, 4.0f);
    CHECK_NEAR(w.body(b)->mass.mass, 4.0, 1e-2);
    // Momentum p = m*v is preserved: 1*5 == 4*1.25
    CHECK_NEAR(w.body(b)->mass.mass * w.body(b)->linearVelocity.x, 5.0, 1e-2);
}

// ---------------------------------------------------------------------------
// Collision
// ---------------------------------------------------------------------------

NOVA_TEST(Physics2D, BoxFallsAndRestsOnGround) {
    World w({0.0f, -10.0f});
    w.createBody(makeStaticBox(0, 0, 10.0f, 0.5f));
    const BodyId b = w.createBody(makeBox(0, 3.0f));
    run(w, 3.0f);

    const Body* body = w.body(b);
    // Half height 0.5 + ground half height 0.5 = 1.0, allowing for slop.
    CHECK_NEAR(body->position.y, 1.0, 0.06);
    CHECK(std::fabs(body->linearVelocity.y) < 0.35f);
    CHECK(body->sleeping);
}

NOVA_TEST(Physics2D, StackSettlesWithoutExploding) {
    // A tall stack is the classic solver stress test: a bad solver makes
    // bodies jitter apart or sink through each other.
    World w({0.0f, -10.0f});
    w.velocityIterations = 12;
    w.positionIterations = 6;
    w.createBody(makeStaticBox(0, 0, 10.0f, 0.5f));
    std::vector<BodyId> stack;
    for (int i = 0; i < 6; ++i) stack.push_back(w.createBody(makeBox(0, 1.1f + i * 1.0f)));

    run(w, 5.0f);
    for (size_t i = 0; i < stack.size(); ++i) {
        const Body* b = w.body(stack[i]);
        const float expected = 1.0f + static_cast<float>(i) * 1.0f;
        CHECK_MSG(std::fabs(b->position.y - expected) < 0.15f,
                  "layer " + std::to_string(i) + " at y=" + std::to_string(b->position.y));
        // Every layer must stay above the one below it.
        if (i > 0) {
            const Body* below = w.body(stack[i - 1]);
            CHECK(b->position.y >= below->position.y - 0.02f);
        }
    }
}

NOVA_TEST(Physics2D, BouncingWithRestitution) {
    World w({0.0f, -10.0f});
    Body ground = makeStaticBox(0, 0, 10.0f, 0.5f);
    ground.restitution = 0.8f;
    w.createBody(ground);
    Body ball = makeCircle(0, 5.0f, 0.5f);
    ball.restitution = 0.8f;
    const BodyId b = w.createBody(ball);

    // Track the apex after the first bounce.
    float apexAfterBounce = 0.0f;
    bool bounced = false;
    float prevY = w.body(b)->position.y;
    for (int i = 0; i < 240; ++i) {
        w.step(1.0f / 60.0f);
        const float y = w.body(b)->position.y;
        if (!bounced && y > prevY && prevY < 1.2f) bounced = true;
        if (bounced) apexAfterBounce = std::max(apexAfterBounce, y);
        prevY = y;
    }
    CHECK(bounced);
    // The ball must rise well above the contact point after bouncing.
    CHECK(apexAfterBounce > 1.4f);
    CHECK(apexAfterBounce < 5.0f);  // and lower than the drop height (energy lost)
}

NOVA_TEST(Physics2D, NoEnergyGainFromRestingOverlap) {
    // Bodies that start interpenetrating must be pushed apart gently, not
    // launched: restitution is taken from the approach velocity only.
    World w({0.0f, 0.0f});
    Body g = makeStaticBox(0, 0, 10.0f, 0.5f);
    g.restitution = 1.0f;
    w.createBody(g);
    Body b = makeBox(0, 0.8f);  // 0.2 units of overlap with the ground
    b.restitution = 1.0f;
    const BodyId id = w.createBody(b);

    float maxUpwardSpeed = 0.0f;
    for (int i = 0; i < 120; ++i) {
        w.step(1.0f / 60.0f);
        maxUpwardSpeed = std::max(maxUpwardSpeed, w.body(id)->linearVelocity.y);
    }
    // Depenetration must not turn into a trampoline.
    CHECK(maxUpwardSpeed < 0.6f);
    CHECK(w.body(id)->position.y > 0.9f);
}

NOVA_TEST(Physics2D, CircleFallsOntoBox) {
    World w({0.0f, -10.0f});
    w.createBody(makeStaticBox(0, 0, 10.0f, 0.5f));
    const BodyId c = w.createBody(makeCircle(0, 4.0f, 0.5f));
    run(w, 3.0f);
    CHECK_NEAR(w.body(c)->position.y, 1.0, 0.08);
    CHECK(std::fabs(w.body(c)->linearVelocity.y) < 0.35f);
}

NOVA_TEST(Physics2D, BoxOnCircle) {
    World w({0.0f, -10.0f});
    const BodyId ground = w.createBody(makeCircle(0, 0, 2.0f));
    w.setStatic(ground, true);
    const BodyId b = w.createBody(makeBox(0, 4.0f, 0.5f, 0.5f));
    run(w, 4.0f);
    // Box half height 0.5, circle radius 2.0 -> resting y = 2.5.
    CHECK_NEAR(w.body(b)->position.y, 2.5, 0.12);
}

NOVA_TEST(Physics2D, FrictionStopsSlidingBodies) {
    // Gravity matters here: friction is bounded by the normal impulse, and a
    // box drifting along a surface under no gravity generates no normal load at
    // all, so there is nothing for friction to act against.
    World w({0.0f, -10.0f});
    Body ground = makeStaticBox(0, 0, 20.0f, 0.5f);
    ground.friction = 0.9f;
    w.createBody(ground);
    Body b = makeBox(0, 1.0f);
    b.friction = 0.9f;
    const BodyId id = w.createBody(b);
    w.setVelocity(id, {6.0f, 0.0f});

    run(w, 3.0f);
    const Body* body = w.body(id);
    // High friction must bring it to a halt well short of the 20-unit ground.
    CHECK(std::fabs(body->linearVelocity.x) < 0.25f);
    CHECK(std::fabs(body->position.x) < 12.0f);
    // It should still be resting on the surface, not sunk into it.
    CHECK(body->position.y > 0.9f);
}

NOVA_TEST(Physics2D, CollisionFilteringByGroup) {
    World w({0, 0});
    Body a = makeBox(0, 0);
    a.collisionGroup = 2;
    a.collisionMask = 1;  // will not collide with group 2
    Body b = makeBox(0.1f, 0);
    b.collisionGroup = 2;
    b.collisionMask = 1;
    const BodyId ia = w.createBody(a);
    const BodyId ib = w.createBody(b);
    w.setVelocity(ia, {5.0f, 0.0f});

    run(w, 1.0f);
    // They pass through each other.
    CHECK(w.body(ib)->position.x < 0.5f);
}

NOVA_TEST(Physics2D, SensorsDetectWithoutPushing) {
    World w({0, 0});
    Body sensor = makeBox(0, 0, 1.0f, 1.0f);
    sensor.sensor = true;
    sensor.type = BodyType::Static;
    const BodyId sid = w.createBody(sensor);

    Body mover = makeBox(0, -5.0f, 0.2f, 0.2f);
    mover.restitution = 0.0f;
    mover.friction = 0.0f;
    const BodyId mid = w.createBody(mover);
    w.setVelocity(mid, {0.0f, 5.0f});

    int beginCount = 0;
    int endCount = 0;
    int sensorPushes = 0;
    w.setContactListener(ContactListener{
        [&](const Body& a, const Body& b, const Contact&) {
            if (a.sensor || b.sensor) ++beginCount;
        },
        [&](const Body& a, const Body& b, const Contact&) {
            if (a.sensor || b.sensor) ++endCount;
        },
        nullptr,
        nullptr,
    });
    const Vec2 start = w.body(mid)->position;
    run(w, 3.0f);

    // The sensor is not a solid: the body passes straight through, and the
    // trigger reports one entry and one exit instead of firing every frame it
    // happens to overlap.
    CHECK(beginCount == 1);
    CHECK(endCount == 1);
    CHECK(sensorPushes == 0);
    CHECK(w.body(mid)->position.y > start.y + 2.0f);
}

NOVA_TEST(Physics2D, SleepingBodiesSettleAndWakeOnImpulse) {
    World w({0.0f, -10.0f});
    w.createBody(makeStaticBox(0, 0, 10.0f, 0.5f));
    const BodyId b = w.createBody(makeBox(0, 2.0f));
    run(w, 4.0f);
    CHECK(w.body(b)->sleeping);

    // An impulse must wake it up.
    w.applyImpulse(b, {0.0f, 20.0f});
    w.step(1.0f / 60.0f);
    CHECK(!w.body(b)->sleeping);
    CHECK(w.body(b)->linearVelocity.y > 0.0f);
}

NOVA_TEST(Physics2D, DestroyAndLookupBodies) {
    World w;
    const BodyId a = w.createBody(makeBox(0, 0));
    const BodyId b = w.createBody(makeBox(1, 0));
    CHECK(w.bodyCount() == 2);
    CHECK(w.body(a) != nullptr);
    CHECK(w.body(9999) == nullptr);

    w.destroyBody(a);
    CHECK(w.bodyCount() == 1);
    CHECK(w.body(a) == nullptr);
    CHECK(w.body(b) != nullptr);  // the other handle is still valid
    w.destroyBody(9999);          // no-op

    w.clear();
    CHECK(w.bodyCount() == 0);
}

NOVA_TEST(Physics2D, FindByTag) {
    World w;
    Body a = makeBox(0, 0);
    a.tag = "enemy";
    Body b = makeBox(1, 0);
    b.tag = "enemy";
    Body c = makeBox(2, 0);
    c.tag = "player";
    w.createBody(a);
    w.createBody(b);
    w.createBody(c);

    CHECK(w.findAllByTag("enemy").size() == 2);
    CHECK(w.findByTag("player") != kInvalidBody);
    CHECK(w.findByTag("nope") == kInvalidBody);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

NOVA_TEST(Physics2D, RaycastHitsClosestBody) {
    World w({0, 0});
    Body far = makeBox(5, 0);
    far.tag = "far";
    Body near = makeBox(2, 0);
    near.tag = "near";
    w.createBody(far);
    w.createBody(near);
    w.step(1.0f / 60.0f);  // populate the broadphase grid

    const RayHit hit = w.raycast({-10, 0}, {1, 0}, 100.0f);
    CHECK(hit.hit);
    CHECK(hit.tag == "near");
    CHECK_NEAR(hit.point.x, 1.5, 1e-3);  // box spans 1.5..2.5
    CHECK_NEAR(hit.distance, 11.5, 1e-3);
    // Surface normal points back along the ray.
    CHECK_NEAR(hit.normal.x, -1.0, 1e-3);
}

NOVA_TEST(Physics2D, RaycastMissAndRange) {
    World w({0, 0});
    w.createBody(makeBox(5, 0));
    w.step(1.0f / 60.0f);

    CHECK(!w.raycast({-10, 0}, {1, 0}, 5.0f).hit);   // stops short
    CHECK(w.raycast({-10, 0}, {1, 0}, 100.0f).hit);
    CHECK(!w.raycast({-10, 50}, {1, 0}, 100.0f).hit);  // passes above
    // Degenerate directions and ranges are safe.
    CHECK(!w.raycast({0, 0}, {0, 0}, 100.0f).hit);
    CHECK(!w.raycast({0, 0}, {1, 0}, -1.0f).hit);
}

NOVA_TEST(Physics2D, RaycastCircle) {
    World w({0, 0});
    w.createBody(makeCircle(3, 0, 1.0f));
    w.step(1.0f / 60.0f);
    const RayHit hit = w.raycast({-10, 0}, {1, 0}, 100.0f);
    CHECK(hit.hit);
    CHECK_NEAR(hit.point.x, 2.0, 1e-3);  // circle centred at 3 with radius 1
}

NOVA_TEST(Physics2D, RaycastAllIsSorted) {
    World w({0, 0});
    for (int i = 1; i <= 4; ++i) w.createBody(makeBox(i * 2.0f, 0));
    w.step(1.0f / 60.0f);
    const auto hits = w.raycastAll({-10, 0}, {1, 0}, 100.0f);
    CHECK(hits.size() == 4);
    for (size_t i = 1; i < hits.size(); ++i) {
        CHECK(hits[i].distance > hits[i - 1].distance);
    }
}

NOVA_TEST(Physics2D, OverlapQueries) {
    World w({0, 0});
    Body a = makeBox(0, 0);
    a.tag = "a";
    Body b = makeBox(3, 0);
    b.tag = "b";
    w.createBody(a);
    w.createBody(b);
    w.step(1.0f / 60.0f);

    const auto atOrigin = w.overlapPoint({0, 0});
    CHECK(atOrigin.size() == 1);
    CHECK(atOrigin[0].tag == "a");

    const auto near = w.overlapAabb(Aabb2{{-1, -1}, {1, 1}});
    CHECK(near.size() == 1);

    const auto wide = w.overlapAabb(Aabb2{{-1, -1}, {4, 1}});
    CHECK(wide.size() == 2);

    // Circle overlap refines the AABB result with an exact test.
    const auto circle = w.overlapCircle({1.5f, 0}, 0.4f);
    CHECK(circle.size() == 0);  // squarely in the gap between the two boxes
    const auto circle2 = w.overlapCircle({0.4f, 0}, 0.2f);
    CHECK(circle2.size() == 1);
    CHECK(circle2[0].tag == "a");
}

NOVA_TEST(Physics2D, ContactListenerFires) {
    World w({0.0f, -10.0f});
    w.createBody(makeStaticBox(0, 0, 10.0f, 0.5f));
    const BodyId b = w.createBody(makeBox(0, 2.0f));

    int broadphase = 0, solves = 0;
    Vec2 firstNormal{0, 0};
    w.setContactListener(ContactListener{
        [&](const Body&, const Body&, const Contact&) {},
        [&](const Body&, const Body&, const Contact&) {},
        [&](const Body&, const Body&, const Contact& c) {
            ++solves;
            if (solves == 1) firstNormal = c.normal;
        },
        [&](const Body&, const Body&) { ++broadphase; },
    });

    run(w, 2.0f);
    CHECK(broadphase > 0);
    CHECK(solves > 0);
    // The normal between a box and the floor it rests on points up (+y).
    CHECK_NEAR(firstNormal.y, 1.0, 0.1);
}

NOVA_TEST(Physics2D, BroadphaseScalesSublinearly) {
    // 200 bodies spread out must not generate ~20 000 pairs: that is what the
    // spatial hash is for.
    World w({0, 0});
    w.createBody(makeStaticBox(0, 0, 500.0f, 0.5f));
    for (int i = 0; i < 200; ++i) {
        w.createBody(makeBox(static_cast<float>(i) * 2.0f, 1.0f));
    }
    w.step(1.0f / 60.0f);
    const WorldStats s = w.stats();
    CHECK(s.bodies == 201);
    // Each box only overlaps its immediate neighbours.
    CHECK_MSG(s.broadphasePairs < 400,
              "pairs=" + std::to_string(s.broadphasePairs));
    CHECK(s.contacts <= 400);
}

NOVA_TEST(Physics2D, DeterminismAcrossIdenticalRuns) {
    auto simulate = []() {
        World w({0.0f, -9.81f});
        w.createBody(makeStaticBox(0, 0, 50.0f, 0.5f));
        for (int i = 0; i < 12; ++i) {
            w.createBody(makeBox(static_cast<float>(i % 4) * 1.1f,
                                 1.2f + static_cast<float>(i / 4) * 1.1f));
        }
        run(w, 3.0f);
        double checksum = 0.0;
        for (BodyId id : w.allBodies()) {
            checksum += w.body(id)->position.x * 7.3 + w.body(id)->position.y * 3.1 +
                        w.body(id)->rotationDeg * 0.7;
        }
        return checksum;
    };
    CHECK_NEAR(simulate(), simulate(), 1e-9);
}

NOVA_TEST(Physics2D, RotatedBoxRestsFlatOnGround) {
    World w({0.0f, -10.0f});
    w.createBody(makeStaticBox(0, 0, 10.0f, 0.5f));
    Body b = makeBox(0, 3.0f, 0.8f, 0.2f);
b.rotationDeg = 20.0f;
const BodyId id = w.createBody(b);

    run(w, 5.0f);
    const Body* body = w.body(id);
    // Angles are stored wrapped into [0, 360), so "how far from flat" has to be
    // measured against the nearest full turn rather than |rotationDeg|.
    const float tilt = std::remainder(body->rotationDeg, 360.0f);
    // A 20-degree tilted plate rocks flat: it ends up nearly horizontal.
    CHECK_MSG(std::fabs(tilt) < 8.0f,
              "rotation=" + std::to_string(body->rotationDeg));
    // Resting height is the ground top (0.5) plus the plate half-thickness.
    CHECK_NEAR(body->position.y, 0.7, 0.1);
}

NOVA_TEST(Physics2D, KinematicBodyMovesAtItsOwnVelocity) {
    World w({0.0f, -10.0f});
    Body k = makeBox(0, 0, 0.5f, 0.5f);
    k.type = BodyType::Kinematic;
    const BodyId id = w.createBody(k);
    w.setVelocity(id, {2.0f, 0.0f});
    run(w, 1.0f);
    CHECK_NEAR(w.body(id)->position.x, 2.0, 0.1);
    // Gravity does not affect kinematic bodies.
    CHECK_NEAR(w.body(id)->position.y, 0.0, 1e-3);
}

NOVA_TEST(Physics2D, FastBodyDoesNotTunnelThroughThinWall) {
    // 400 units/s against a 0.2-unit wall: without CCD this escapes.
    World w({0, 0});
    w.createBody(makeStaticBox(10, 0, 0.1f, 20.0f));
    const BodyId b = w.createBody(makeCircle(0, 0, 0.2f));
    w.setVelocity(b, {400.0f, 0.0f});
    run(w, 1.0f);
    // It must be stopped on the near side of the wall, not past it.
    CHECK(w.body(b)->position.x < 9.8f);
}

// --- rolling and friction regressions ---------------------------------------
//
// These lock in three defects that only show up once a body is resting and
// rotating together, which is why they went unnoticed: a speculative margin
// folded into the contact anchors, a warm start replayed once per sub-step,
// and a contact lever arm that drifted as the body moved between sub-steps.
// Together they made a ball rolling at a steady speed quietly accelerate.

namespace {

/// A ball resting on a wide floor, built fresh each time.
physics2d::BodyId rollingBall(physics2d::World& world, Vec2 velocity, float spinDegPerSec,
                              float mass = 1.6f, float radius = 0.45f) {
    physics2d::Body ground;
    ground.type = physics2d::BodyType::Static;
    ground.position = Vec2(0, -0.5f);
    ground.shape = physics2d::ColliderShape::Box;
    ground.polygon = physics2d::ConvexPolygon::box(60.0f, 0.5f);
    ground.friction = 0.7f;
    ground.tag = "Ground";
    world.createBody(ground);

    physics2d::Body ball;
    ball.type = physics2d::BodyType::Dynamic;
    ball.shape = physics2d::ColliderShape::Circle;
    ball.radius = radius;
    ball.position = Vec2(0, radius);
    ball.mass.mass = mass;
    ball.friction = 0.55f;
    ball.restitution = 0.0f;
    ball.linearVelocity = velocity;
    ball.angularVelocity = spinDegPerSec;
    ball.tag = "Ball";
    const physics2d::BodyId id = world.createBody(ball);
    world.updateMassFromShape(id);
    return id;
}

/// The spin a ball of this speed would have if it were rolling without slipping.
float rollingSpin(float speed, float radius) {
    return -speed / radius * 57.29578f;
}

}  // namespace

NOVA_TEST(Physics2D, ARollingBallKeepsItsSpeed) {
    physics2d::World world(Vec2(0.0f, -9.81f));
    world.enableContinuous = false;
    const physics2d::BodyId ball = rollingBall(world, Vec2(0.14f, 0.0f), rollingSpin(0.14f, 0.45f));

    for (int i = 0; i < 600; ++i) world.step(1.0f / 60.0f);

    const physics2d::Body* b = world.body(ball);
    // Every impulse here passes through the contact, so angular momentum about
    // the contact point is conserved and the speed must not move. Before the
    // fix it went 0.14 -> 0.32 over the same ten seconds.
    CHECK_MSG(std::fabs(b->linearVelocity.x - 0.14f) < 0.005f,
              "vx=" + std::to_string(b->linearVelocity.x));
    CHECK_MSG(std::fabs(b->angularVelocity - rollingSpin(0.14f, 0.45f)) < 0.5f,
              "w=" + std::to_string(b->angularVelocity));
    CHECK(b->position.y > 0.3f);
    CHECK(b->position.y < 0.46f);
}

NOVA_TEST(Physics2D, ASlidingBallReachesTheAnalyticRollingSpeed) {
    physics2d::World world(Vec2(0.0f, -9.81f));
    world.enableContinuous = false;
    const float radius = 0.45f;
    const float v0 = 0.14f;
    const physics2d::BodyId ball = rollingBall(world, Vec2(v0, 0.0f), 0.0f);

    for (int i = 0; i < 600; ++i) world.step(1.0f / 60.0f);

    // Friction alone changes the state, so angular momentum about the contact
    // point is conserved. Starting from w = 0:
    //     0 + m*r*v0 = I*w - m*r*v,  with w = -v/r
    // which gives v = v0 * m*r^2 / (I + m*r^2). A 2D disc has I = m*r^2/2, so
    // the answer is two thirds of the initial speed. (A solid *sphere* would be
    // I = 2/5 m*r^2 and give v0/3; this engine simulates discs.)
    const float expected = v0 * (2.0f / 3.0f);
    const physics2d::Body* b = world.body(ball);
    CHECK_MSG(std::fabs(b->linearVelocity.x - expected) < 0.004f,
              "vx=" + std::to_string(b->linearVelocity.x));
    CHECK_MSG(std::fabs(b->angularVelocity - rollingSpin(expected, radius)) < 0.5f,
              "w=" + std::to_string(b->angularVelocity));
}

NOVA_TEST(Physics2D, RollingResistanceBringsARollingBallToRest) {
    physics2d::World world(Vec2(0.0f, -9.81f));
    world.enableContinuous = false;
    world.rollingFriction = 0.35f;
    const physics2d::BodyId ball = rollingBall(world, Vec2(0.6f, 0.0f), rollingSpin(0.6f, 0.45f));

    for (int i = 0; i < 600; ++i) world.step(1.0f / 60.0f);

    const physics2d::Body* b = world.body(ball);
    CHECK_MSG(std::fabs(b->linearVelocity.x) < 0.02f,
              "vx=" + std::to_string(b->linearVelocity.x));
    CHECK(std::fabs(b->angularVelocity) < 1.0f);
    // It must not sink while doing it.
    CHECK(b->position.y > 0.4f);
}

NOVA_TEST(Physics2D, RollingResistanceIsOffByDefault) {
    physics2d::World world(Vec2(0.0f, -9.81f));
    CHECK(world.rollingFriction == 0.0f);
    world.enableContinuous = false;
    const physics2d::BodyId ball = rollingBall(world, Vec2(0.3f, 0.0f), rollingSpin(0.3f, 0.45f));
    for (int i = 0; i < 300; ++i) world.step(1.0f / 60.0f);
    const physics2d::Body* b = world.body(ball);
    CHECK_MSG(std::fabs(b->linearVelocity.x - 0.3f) < 0.01f,
              "vx=" + std::to_string(b->linearVelocity.x));
}
