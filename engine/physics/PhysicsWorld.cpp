#include "physics/PhysicsWorld.h"

#include <algorithm>
#include <cmath>

namespace nova {

void PhysicsWorld::clear() {
    bodies_.clear();
}

void PhysicsWorld::addBody(const PhysicsBody& body) {
    PhysicsBody b = body;
    if (b.bodyType != 1 || b.mass <= 0.0f) {
        b.invMass = 0.0f; // static / kinematic don't respond to impulses
    } else {
        b.invMass = 1.0f / b.mass;
    }
    bodies_.push_back(b);
}

void PhysicsWorld::step(float dt) {
    if (dt <= 0.0f) return;
    const float h = dt / static_cast<float>(kSubSteps);
    for (int i = 0; i < kSubSteps; ++i) {
        integrate(h);
        resolveCollisions();
        if (hasGround) {
            for (auto& b : bodies_) resolveGround(b);
        }
    }
}

void PhysicsWorld::integrate(float dt) {
    for (auto& b : bodies_) {
        if (b.bodyType == 1) { // dynamic
            b.vy += gravity * b.gravityScale * dt;
        }
        if (b.bodyType != 0) { // dynamic + kinematic move; static never does
            b.x += b.vx * dt;
            b.y += b.vy * dt;
        }
        b.grounded = false;
    }
}

void PhysicsWorld::resolveCollisions() {
    const size_t n = bodies_.size();
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            resolvePair(bodies_[i], bodies_[j]);
        }
    }
}

void PhysicsWorld::resolvePair(PhysicsBody& a, PhysicsBody& b) {
    const float dx = b.x - a.x;
    const float px = (a.halfW + b.halfW) - std::fabs(dx);
    if (px <= 0.0f) return;
    const float dy = b.y - a.y;
    const float py = (a.halfH + b.halfH) - std::fabs(dy);
    if (py <= 0.0f) return;

    const float totalInvMass = a.invMass + b.invMass;
    if (totalInvMass <= 0.0f) return; // two immovable bodies

    // Resolve along the axis of least penetration.
    if (px < py) {
        const float nx = dx < 0.0f ? -1.0f : 1.0f;
        const float corr = px / totalInvMass;
        a.x -= nx * corr * a.invMass;
        b.x += nx * corr * b.invMass;
        const float rvn = (b.vx - a.vx) * nx;
        if (rvn < 0.0f) {
            const float e = std::min(a.restitution, b.restitution);
            const float j = -(1.0f + e) * rvn / totalInvMass;
            a.vx -= j * nx * a.invMass;
            b.vx += j * nx * b.invMass;
        }
    } else {
        const float ny = dy < 0.0f ? -1.0f : 1.0f;
        const float corr = py / totalInvMass;
        a.y -= ny * corr * a.invMass;
        b.y += ny * corr * b.invMass;
        const float rvn = (b.vy - a.vy) * ny;
        if (rvn < 0.0f) {
            const float e = std::min(a.restitution, b.restitution);
            const float j = -(1.0f + e) * rvn / totalInvMass;
            a.vy -= j * ny * a.invMass;
            b.vy += j * ny * b.invMass;
            // Simple Coulomb friction on the tangential axis.
            const float mu = std::sqrt(std::max(0.0f, a.friction * b.friction));
            const float rvt = (b.vx - a.vx);
            const float jt = std::clamp(-rvt / totalInvMass, -j * mu, j * mu);
            a.vx -= jt * a.invMass;
            b.vx += jt * b.invMass;
        }
        // Grounded flag: a body resting on top of another (normal pointing up).
        if (ny > 0.0f && b.bodyType == 1) b.grounded = true;
        if (ny < 0.0f && a.bodyType == 1) a.grounded = true;
    }
}

void PhysicsWorld::resolveGround(PhysicsBody& b) {
    if (b.bodyType != 1) return;
    const float bottom = b.y - b.halfH;
    if (bottom < groundY) {
        b.y = groundY + b.halfH;
        if (b.vy < 0.0f) {
            b.vy = -b.vy * b.restitution;
            if (std::fabs(b.vy) < 0.1f) b.vy = 0.0f;
        }
        b.grounded = true;
    }
}

} // namespace nova

namespace nova {
std::string PhysicsWorld::raycast(float x1, float y1, float x2, float y2, float* outT) const {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    std::string bestId;
    float bestT = 2.0f;

    for (const PhysicsBody& b : bodies_) {
        // Slab method on the body AABB.
        float tmin = 0.0f;
        float tmax = 1.0f;
        bool hit = true;

        for (int axis = 0; axis < 2 && hit; ++axis) {
            const float origin = axis == 0 ? x1 : y1;
            const float dir = axis == 0 ? dx : dy;
            const float lo = (axis == 0 ? b.x - b.halfW : b.y - b.halfH);
            const float hi = (axis == 0 ? b.x + b.halfW : b.y + b.halfH);
            if (std::fabs(dir) < 1e-8f) {
                if (origin < lo || origin > hi) hit = false;
            } else {
                float t1 = (lo - origin) / dir;
                float t2 = (hi - origin) / dir;
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax) hit = false;
            }
        }

        if (hit && tmin >= 0.0f && tmin < bestT) {
            bestT = tmin;
            bestId = b.id;
        }
    }

    if (outT) *outT = bestId.empty() ? -1.0f : bestT;
    return bestId;
}

} // namespace nova
