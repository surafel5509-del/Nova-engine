#pragma once

#include <string>
#include <vector>

namespace nova {

/** 2D rigid body for the built-in physics simulation. */
struct PhysicsBody {
    std::string id;
    // 0 = static, 1 = dynamic, 2 = kinematic
    int bodyType = 0;
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float halfW = 0.5f;      // AABB half extents (world units)
    float halfH = 0.5f;
    float mass = 1.0f;
    float invMass = 1.0f;    // 0 for static/kinematic
    float gravityScale = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
    bool grounded = false;
};

/**
 * Minimal, deterministic 2D impulse solver over axis-aligned boxes.
 * Gravity + integration + pairwise collision resolution + ground plane.
 * Pure math — fully host-testable (no GL).
 */
class PhysicsWorld {
public:
    float gravity = -9.8f;         // world units / s^2
    float groundY = -1e9f;         // disabled by default
    bool hasGround = false;

    void clear();
    void addBody(const PhysicsBody& body);

    /** Advances the simulation by [dt] seconds (sub-stepped). */
    void step(float dt);

    /**
     * Casts a ray from (x1,y1) toward (x2,y2) against all body AABBs.
     * Returns the closest hit body id, or empty string on no hit.
     */
    std::string raycast(float x1, float y1, float x2, float y2, float* outT = nullptr) const;

    const std::vector<PhysicsBody>& bodies() const { return bodies_; }
    std::vector<PhysicsBody>& bodies() { return bodies_; }

private:
    void integrate(float dt);
    void resolveCollisions();
    void resolvePair(PhysicsBody& a, PhysicsBody& b);
    void resolveGround(PhysicsBody& b);

    std::vector<PhysicsBody> bodies_;
    static constexpr int kSubSteps = 4;
};

} // namespace nova
