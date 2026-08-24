#include "physics/PhysicsWorld3D.h"

#include <algorithm>
#include <cmath>

namespace nova {

void PhysicsWorld3D::clear() {
    bodies_.clear();
}

void PhysicsWorld3D::addBody(const PhysicsBody3D& body) {
    PhysicsBody3D b = body;
    if (b.bodyType != 1 || b.mass <= 0.0f) {
        b.invMass = 0.0f;
    } else {
        b.invMass = 1.0f / b.mass;
    }
    bodies_.push_back(b);
}

void PhysicsWorld3D::step(float dt) {
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

void PhysicsWorld3D::integrate(float dt) {
    for (auto& b : bodies_) {
        if (b.bodyType == 1) {
            b.vy += gravity * b.gravityScale * dt;
        }
        if (b.bodyType != 0) {
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            b.z += b.vz * dt;
        }
        b.grounded = false;
    }
}

void PhysicsWorld3D::resolveCollisions() {
    for (size_t i = 0; i < bodies_.size(); ++i) {
        for (size_t j = i + 1; j < bodies_.size(); ++j) {
            PhysicsBody3D& a = bodies_[i];
            PhysicsBody3D& b = bodies_[j];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float dz = b.z - a.z;
            const float distSq = dx * dx + dy * dy + dz * dz;
            const float rSum = a.radius + b.radius;
            if (distSq >= rSum * rSum) continue;

            const float dist = std::sqrt(distSq);
            const float nx = dist > 1e-6f ? dx / dist : 0.0f;
            const float ny = dist > 1e-6f ? dy / dist : 1.0f;
            const float nz = dist > 1e-6f ? dz / dist : 0.0f;
            const float overlap = rSum - dist;

            // Positional separation weighted by inverse mass.
            const float invSum = a.invMass + b.invMass;
            if (invSum > 0.0f) {
                const float aShare = a.invMass / invSum;
                const float bShare = b.invMass / invSum;
                a.x -= nx * overlap * aShare;
                a.y -= ny * overlap * aShare;
                a.z -= nz * overlap * aShare;
                b.x += nx * overlap * bShare;
                b.y += ny * overlap * bShare;
                b.z += nz * overlap * bShare;
            }

            // Normal impulse with restitution.
            const float rvx = b.vx - a.vx;
            const float rvy = b.vy - a.vy;
            const float rvz = b.vz - a.vz;
            const float vn = rvx * nx + rvy * ny + rvz * nz;
            if (vn < 0.0f) {
                const float e = std::max(a.restitution, b.restitution);
                const float jimp = -(1.0f + e) * vn / (a.invMass + b.invMass + 1e-6f);
                a.vx -= jimp * nx * a.invMass;
                a.vy -= jimp * ny * a.invMass;
                a.vz -= jimp * nz * a.invMass;
                b.vx += jimp * nx * b.invMass;
                b.vy += jimp * ny * b.invMass;
                b.vz += jimp * nz * b.invMass;
            }
        }
    }
}

void PhysicsWorld3D::resolveGround(PhysicsBody3D& b) {
    const float bottom = b.y - b.radius;
    if (bottom < groundY) {
        b.y = groundY + b.radius;
        if (b.vy < 0.0f) {
            b.vy = -b.vy * b.restitution;
            if (std::fabs(b.vy) < 0.1f) b.vy = 0.0f;
        }
        b.grounded = true;
    }
}

} // namespace nova
