// Host-side tests for the 3D physics world (sphere colliders, gravity, ground).
#include <cmath>
#include <cstdio>

#include "physics/PhysicsWorld3D.h"

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

nova::PhysicsBody3D makeDynamic3D(const char* id, float x, float y, float z, float radius) {
    nova::PhysicsBody3D b;
    b.id = id;
    b.bodyType = 1;
    b.x = x;
    b.y = y;
    b.z = z;
    b.radius = radius;
    return b;
}

void testGravity3D() {
    nova::PhysicsWorld3D world;
    world.hasGround = false;
    world.addBody(makeDynamic3D("ball", 0.0f, 10.0f, 0.0f, 0.5f));
    world.step(1.0f);
    check(world.bodies()[0].y < 10.0f, "3D body falls under gravity");
    check(world.bodies()[0].vy < 0.0f, "3D body gains downward velocity");
}

void testGround3D() {
    nova::PhysicsWorld3D world;
    world.groundY = 0.0f;
    world.addBody(makeDynamic3D("ball", 0.0f, 2.0f, 0.0f, 0.5f));
    for (int i = 0; i < 120; ++i) world.step(1.0f / 60.0f);
    check(nearly(world.bodies()[0].y, 0.5f, 0.05f), "3D body rests on the ground");
    check(world.bodies()[0].grounded, "3D body is grounded");
}

void testSphereSphereSeparation() {
    nova::PhysicsWorld3D world;
    world.hasGround = false;
    world.addBody(makeDynamic3D("a", 0.0f, 0.0f, 0.0f, 0.5f));
    world.addBody(makeDynamic3D("b", 0.6f, 0.0f, 0.0f, 0.5f));
    world.step(1.0f / 60.0f);
    const auto& a = world.bodies()[0];
    const auto& b = world.bodies()[1];
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    check(dist >= 0.99f, "overlapping 3D spheres separate");
}

void testStatic3DDoesNotMove() {
    nova::PhysicsWorld3D world;
    nova::PhysicsBody3D s;
    s.id = "wall";
    s.bodyType = 0;
    s.y = 5.0f;
    world.addBody(s);
    world.step(0.5f);
    check(nearly(world.bodies()[0].y, 5.0f), "static 3D body stays put");
}

} // namespace

int runPhysics3DTests() {
    testGravity3D();
    testGround3D();
    testSphereSphereSeparation();
    testStatic3DDoesNotMove();
    std::printf("physics3d: %d checks, %d failures\n", checks, failures);
    return failures;
}
