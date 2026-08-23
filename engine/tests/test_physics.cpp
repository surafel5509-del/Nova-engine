// Host-side physics tests: gravity, collision response, ground plane.
#include <cmath>
#include <cstdio>

#include "physics/PhysicsWorld.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* name) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", name);
    }
}

bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) < eps;
}

nova::PhysicsBody makeDynamic(const char* id, float x, float y, float hw, float hh) {
    nova::PhysicsBody b;
    b.id = id;
    b.bodyType = 1;
    b.x = x;
    b.y = y;
    b.halfW = hw;
    b.halfH = hh;
    b.mass = 1.0f;
    return b;
}

void testGravityIntegration() {
    nova::PhysicsWorld world;
    world.gravity = -10.0f;
    world.addBody(makeDynamic("fall", 0.0f, 10.0f, 0.5f, 0.5f));
    world.step(1.0f);
    const auto& b = world.bodies()[0];
    // After 1s at g=-10: v ~= -10, y should have dropped well below 10.
    check(b.vy < -9.0f, "gravity accelerates downward");
    check(b.y < 9.0f, "body falls");
}

void testStaticFloorCollision() {
    nova::PhysicsWorld world;
    world.gravity = -10.0f;
    // Static floor spanning wide area at y=0 (top surface at y=0.5).
    nova::PhysicsBody floor;
    floor.id = "floor";
    floor.bodyType = 0;
    floor.x = 0.0f;
    floor.y = 0.0f;
    floor.halfW = 10.0f;
    floor.halfH = 0.5f;
    world.addBody(floor);
    // Dynamic box starts above the floor and falls onto it.
    world.addBody(makeDynamic("box", 0.0f, 3.0f, 0.5f, 0.5f));

    for (int i = 0; i < 120; ++i) world.step(1.0f / 60.0f);
    const auto& box = world.bodies()[1];
    // Box should rest on top of floor: floor top = 0.5, box halfH = 0.5 -> y = 1.0.
    check(box.y > 0.9f && box.y < 1.2f, "box rests on floor");
    check(nearly(box.vy, 0.0f, 0.5f), "box vertical velocity settles");
    check(box.grounded, "box reports grounded");
}

void testRestitutionBounce() {
    nova::PhysicsWorld world;
    world.gravity = -10.0f;
    world.hasGround = true;
    world.groundY = 0.0f;
    nova::PhysicsBody b = makeDynamic("ball", 0.0f, 2.0f, 0.5f, 0.5f);
    b.restitution = 0.8f;
    world.addBody(b);
    // Let it fall and hit the ground once.
    for (int i = 0; i < 40; ++i) world.step(1.0f / 60.0f);
    const auto& ball = world.bodies()[0];
    // With restitution, after impact the ball should be moving up at some point.
    check(ball.y >= 0.49f, "ball stays above ground");
}

void testStaticBodiesDoNotMove() {
    nova::PhysicsWorld world;
    world.gravity = -10.0f;
    nova::PhysicsBody s;
    s.id = "wall";
    s.bodyType = 0;
    s.x = 5.0f;
    s.y = 5.0f;
    world.addBody(s);
    world.step(1.0f);
    check(nearly(world.bodies()[0].x, 5.0f) && nearly(world.bodies()[0].y, 5.0f),
          "static body unaffected by gravity");
}

void testHorizontalSeparation() {
    nova::PhysicsWorld world;
    world.gravity = 0.0f;
    // Two overlapping dynamic boxes side by side should separate horizontally.
    world.addBody(makeDynamic("a", 0.0f, 0.0f, 0.5f, 0.5f));
    world.addBody(makeDynamic("b", 0.6f, 0.0f, 0.5f, 0.5f));
    world.step(1.0f / 60.0f);
    const auto& a = world.bodies()[0];
    const auto& b = world.bodies()[1];
    const float gap = std::fabs(b.x - a.x);
    check(gap >= 0.99f, "overlapping boxes separate");
}

} // namespace

int runPhysicsTests() {
    testGravityIntegration();
    testStaticFloorCollision();
    testRestitutionBounce();
    testStaticBodiesDoNotMove();
    testHorizontalSeparation();
    std::printf("physics: %d checks, %d failures\n", checks, failures);
    return failures;
}
