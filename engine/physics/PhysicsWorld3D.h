#pragma once

#include <string>
#include <vector>

namespace nova {

/** 3D rigid body (sphere collider) for the 3D physics simulation. */
struct PhysicsBody3D {
    std::string id;
    int bodyType = 0;            // 0 static, 1 dynamic, 2 kinematic
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    float radius = 0.5f;
    float mass = 1.0f;
    float invMass = 1.0f;        // 0 for static/kinematic
    float gravityScale = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
    bool grounded = false;
};

/**
 * Minimal deterministic 3D physics: gravity + integration + sphere/sphere and
 * sphere/ground-plane collisions. Pure math — fully host-testable (no GL).
 */
class PhysicsWorld3D {
public:
    float gravity = -9.8f;       // m/s^2
    float groundY = 0.0f;        // ground plane height
    bool hasGround = true;

    void clear();
    void addBody(const PhysicsBody3D& body);

    /** Advances the simulation by [dt] seconds (sub-stepped). */
    void step(float dt);

    const std::vector<PhysicsBody3D>& bodies() const { return bodies_; }
    std::vector<PhysicsBody3D>& bodies() { return bodies_; }

private:
    void integrate(float dt);
    void resolveCollisions();
    void resolveGround(PhysicsBody3D& b);

    std::vector<PhysicsBody3D> bodies_;
    static constexpr int kSubSteps = 4;
};

} // namespace nova
