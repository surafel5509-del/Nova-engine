#include "physics/Physics2D.h"

#include <algorithm>
#include <cmath>

#include "base/Time.h"

namespace nova {
namespace physics2d {

// ---------------------------------------------------------------------------
// ConvexPolygon
// ---------------------------------------------------------------------------

void ConvexPolygon::computeSignedArea(double& area) const {
    area = 0.0;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const Vec2& p = vertices[i];
        const Vec2& q = vertices[(i + 1) % vertices.size()];
        area += static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y;
    }
    area *= 0.5;
}

void ConvexPolygon::fixWinding() {
    if (vertices.size() < 3) { centroid = {0, 0}; return; }
    double area = 0.0;
    computeSignedArea(area);
    // The SAT implementation assumes counter-clockwise winding.
    if (area < 0.0) std::reverse(vertices.begin(), vertices.end());
    computeCentroid();
}

void ConvexPolygon::computeCentroid() {
    if (vertices.empty()) { centroid = {0, 0}; return; }
    if (vertices.size() < 3) {
        centroid = vertices[0];
        return;
    }
    double area = 0.0;
    computeSignedArea(area);
    if (std::fabs(area) < 1e-9) {
        centroid = {0, 0};
        for (const Vec2& v : vertices) centroid += v;
        centroid = centroid / static_cast<float>(vertices.size());
        return;
    }
    Vec2 c{0, 0};
    for (size_t i = 0; i < vertices.size(); ++i) {
        const Vec2& p = vertices[i];
        const Vec2& q = vertices[(i + 1) % vertices.size()];
        const float cross = p.x * q.y - q.x * p.y;
        c += (p + q) * (cross / 6.0f);
    }
    centroid = c / static_cast<float>(area);
}

Aabb2 ConvexPolygon::bounds() const {
    if (vertices.empty()) return {};
    return Aabb2::fromPoints(vertices.data(), static_cast<int>(vertices.size()));
}

ConvexPolygon ConvexPolygon::regular(float radius, int sides, float rotationDeg) {
    sides = std::max(3, sides);
    std::vector<Vec2> v;
    v.reserve(static_cast<size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        const float a = degToRad(rotationDeg + 360.0f * i / sides);
        v.push_back({std::cos(a) * radius, std::sin(a) * radius});
    }
    return ConvexPolygon(std::move(v));
}

// ---------------------------------------------------------------------------
// SAT
// ---------------------------------------------------------------------------

namespace {

/// Support point of a convex polygon in world space for the given direction.
Vec2 supportPoint(const ConvexPolygon& poly, const Vec2& pos, float rotDeg, const Vec2& dir) {
    if (poly.vertices.empty()) return pos;
    const float r = degToRad(rotDeg), c = std::cos(r), s = std::sin(r);
    const Vec2 localDir{c * dir.x + s * dir.y, -s * dir.x + c * dir.y};
    size_t best = 0;
    float bestProj = -std::numeric_limits<float>::max();
    for (size_t i = 0; i < poly.vertices.size(); ++i) {
        const float proj = poly.vertices[i].dot(localDir);
        if (proj > bestProj) { bestProj = proj; best = i; }
    }
    const Vec2& lv = poly.vertices[best];
    return pos + Vec2{lv.x * c - lv.y * s, lv.x * s + lv.y * c};
}

}  // namespace

bool satTest(const ConvexPolygon& a, const Vec2& posA, float rotA,
             const ConvexPolygon& b, const Vec2& posB, float rotB,
             Vec2& outNormal, float& outPenetration, Vec2& outPoint) {
    if (a.vertices.empty() || b.vertices.empty()) return false;

    auto toWorld = [](const Vec2& p, const Vec2& origin, float c, float s) {
        return Vec2{origin.x + p.x * c - p.y * s, origin.y + p.x * s + p.y * c};
    };

    const float ra = degToRad(rotA), ca = std::cos(ra), sa = std::sin(ra);
    const float rb = degToRad(rotB), cb = std::cos(rb), sb = std::sin(rb);

    // Candidate axes: face normals of both polygons. This is exact for
    // polygons (unlike the edge cross products some 2D engines use).
    struct Axis { Vec2 n; float depth; };
    std::vector<Axis> axes;
    axes.reserve(a.vertices.size() + b.vertices.size());
    bool separated = false;

    auto addAxes = [&](const ConvexPolygon& p, const Vec2& otherPos, float c, float s) {
        (void)otherPos;
        for (size_t i = 0; i < p.vertices.size(); ++i) {
            const Vec2& v0 = p.vertices[i];
            const Vec2& v1 = p.vertices[(i + 1) % p.vertices.size()];
            Vec2 edge{v1.x - v0.x, v1.y - v0.y};
            if (edge.lengthSq() < 1e-12f) continue;
            edge = edge.normalized();
            // Outward normal of a CCW polygon edge.
            const Vec2 localNormal{edge.y, -edge.x};
            const Vec2 n{c * localNormal.x - s * localNormal.y,
                         s * localNormal.x + c * localNormal.y};
            float minA = std::numeric_limits<float>::max();
            float maxA = -std::numeric_limits<float>::max();
            float minB = std::numeric_limits<float>::max();
            float maxB = -std::numeric_limits<float>::max();
            for (const Vec2& v : a.vertices) {
                const float d = toWorld(v, posA, ca, sa).dot(n);
                minA = std::min(minA, d);
                maxA = std::max(maxA, d);
            }
            for (const Vec2& v : b.vertices) {
                const float d = toWorld(v, posB, cb, sb).dot(n);
                minB = std::min(minB, d);
                maxB = std::max(maxB, d);
            }
            const float overlap = std::min(maxA, maxB) - std::max(minA, minB);
            if (overlap <= 0.0f) {
                // Separating axis: the shapes are disjoint on this axis. The
                // flag must survive the loop, otherwise the caller would see a
                // partial axis set and report a false collision.
                separated = true;
                return;
            }
            axes.push_back({n, overlap});
        }
    };

    addAxes(a, posB, ca, sa);
    if (!separated) addAxes(b, posA, cb, sb);
    if (separated || axes.empty()) return false;

    // Minimum overlap axis is the collision normal.
    size_t best = 0;
    for (size_t i = 1; i < axes.size(); ++i) {
        if (axes[i].depth < axes[best].depth) best = i;
    }
    Vec2 normal = axes[best].n;
    const float depth = axes[best].depth;

    // Orient the normal from A towards B.
    const Vec2 centerDelta = (posB + b.centroid) - (posA + a.centroid);
    if (normal.dot(centerDelta) < 0.0f) normal = -normal;

    outNormal = normal;
    outPenetration = depth;

    // Contact point.
    //
    // The normal points from A towards B, so the contact lies on A's face in
    // the +normal direction and on B's face in the -normal direction. (Using
    // the opposite support directions puts the point on the *far* side of both
    // bodies, which produces a huge bogus lever arm and makes contacts behave
    // like glue.)
    const Vec2 faceA = supportPoint(a, posA, rotA, normal);
    // (B's face is not needed: the contact sits on A's face.)
    (void)supportPoint(b, posB, rotB, -normal);

    // Along the tangent, use the centre of the interval where the two shapes
    // actually overlap. Picking a single corner instead biases the contact to
    // one edge, so flat-on-flat contacts tip and buzz instead of settling.
    const Vec2 tangent = normal.perpendicular();
    auto tangentRange = [&](const ConvexPolygon& p, const Vec2& origin, float c, float s,
                            float& outMin, float& outMax) {
        outMin = std::numeric_limits<float>::max();
        outMax = -std::numeric_limits<float>::max();
        for (const Vec2& v : p.vertices) {
            const float t = toWorld(v, origin, c, s).dot(tangent);
            outMin = std::min(outMin, t);
            outMax = std::max(outMax, t);
        }
    };
    float aMin, aMax, bMin, bMax;
    tangentRange(a, posA, ca, sa, aMin, aMax);
    tangentRange(b, posB, cb, sb, bMin, bMax);
    // Intersect the two spans, then take the middle of the intersection.
    const float lo = std::max(aMin, bMin);
    const float hi = std::min(aMax, bMax);
    const float t = (lo <= hi) ? (lo + hi) * 0.5f : faceA.dot(tangent);

    // Sit on A's face: that keeps the lever arm to B equal to the true
    // penetration distance, which is what the solver needs.
    const float surface = faceA.dot(normal);
    outPoint = tangent * t + normal * surface;
    return true;
}

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------

World::World(Vec2 g) : gravity(g) {}

BodyId World::createBody(const Body& desc) {
    Body b = desc;
    b.id = nextId_++;
    b.previousPosition = b.position;
    b.previousRotation = b.rotationDeg;
    b.enabled = true;

    const size_t slot = bodies_.size();
    bodies_.push_back(std::move(b));
    index_[bodies_[slot].id] = slot;

    if (bodies_[slot].type != BodyType::Static) {
        updateMassFromShape(bodies_[slot].id);
    } else {
        // A static body keeps the shape bounds but never integrates a mass.
        updateMassFromShape(bodies_[slot].id);
        bodies_[slot].mass.mass = 0.0f;
        bodies_[slot].mass.invMass = 0.0f;
        bodies_[slot].mass.inertia = 0.0f;
        bodies_[slot].mass.invInertia = 0.0f;
    }
    refreshBounds(bodies_[slot]);
    return bodies_[slot].id;
}

Body* World::body(BodyId id) {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &bodies_[it->second];
}

const Body* World::body(BodyId id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &bodies_[it->second];
}

void World::destroyBody(BodyId id) {
    const auto it = index_.find(id);
    if (it == index_.end()) return;
    const size_t slot = it->second;
    bodies_.erase(bodies_.begin() + static_cast<long>(slot));
    index_.clear();  // slots shift on erase; rebuild (bodies are cheap)
    for (size_t i = 0; i < bodies_.size(); ++i) index_[bodies_[i].id] = i;
    sensorOverlaps_.erase(id);
    for (auto& kv : sensorOverlaps_) {
        auto& v = kv.second;
        v.erase(std::remove(v.begin(), v.end(), id), v.end());
    }
    // A body that vanished cannot report its own exit, so retire the pair here
    // instead of letting the listener wait forever for an end that never comes.
    persistentSensors_.erase(
        std::remove_if(persistentSensors_.begin(), persistentSensors_.end(),
                       [id](const std::pair<BodyId, BodyId>& pr) {
                           return pr.first == id || pr.second == id;
                       }),
        persistentSensors_.end());
}

void World::clear() {
    bodies_.clear();
    index_.clear();
    contacts_.clear();
    persistent_.clear();
    grid_.clear();
    sensorOverlaps_.clear();
    persistentSensors_.clear();
    sensorPairs_.clear();
    nextId_ = 1;
}

std::vector<BodyId> World::allBodies() const {
    std::vector<BodyId> out;
    out.reserve(bodies_.size());
    for (const Body& b : bodies_) out.push_back(b.id);
    return out;
}

BodyId World::findByTag(const std::string& tag) const {
    for (const Body& b : bodies_) {
        if (b.tag == tag) return b.id;
    }
    return kInvalidBody;
}

std::vector<BodyId> World::findAllByTag(const std::string& tag) const {
    std::vector<BodyId> out;
    for (const Body& b : bodies_) {
        if (b.tag == tag) out.push_back(b.id);
    }
    return out;
}

void World::applyForce(BodyId id, const Vec2& force) {
    if (Body* b = body(id)) b->force += force;
}

void World::applyForceAtPoint(BodyId id, const Vec2& force, const Vec2& worldPoint) {
    Body* b = body(id);
    if (!b) return;
    b->force += force;
    const Vec2 r = worldPoint - b->position;
    b->torque += r.cross(force);
}

void World::applyImpulse(BodyId id, const Vec2& impulse) {
    Body* b = body(id);
    if (!b || b->type != BodyType::Dynamic) return;
    b->linearVelocity += impulse * b->mass.invMass;
    // No angular change: applied at the centre of mass.
}

void World::applyImpulseAtPoint(BodyId id, const Vec2& impulse, const Vec2& worldPoint) {
    Body* b = body(id);
    if (!b || b->type != BodyType::Dynamic) return;
    b->linearVelocity += impulse * b->mass.invMass;
    const Vec2 r = worldPoint - b->position;
    b->angularVelocity += r.cross(impulse) * b->mass.invInertia * kRadToDeg;
}

void World::applyTorque(BodyId id, float torque) {
    if (Body* b = body(id)) b->torque += torque;
}

void World::setVelocity(BodyId id, const Vec2& v, float angularDegPerSec) {
    Body* b = body(id);
    if (!b) return;
    b->linearVelocity = v;
    b->angularVelocity = angularDegPerSec;
    b->previousPosition = b->position;
    b->previousRotation = b->rotationDeg;
}

void World::setMass(BodyId id, float mass) {
    Body* b = body(id);
    if (!b) return;
    applyMassChange(id, mass);
}

void World::setStatic(BodyId id, bool isStatic) {
    Body* b = body(id);
    if (!b) return;
    b->type = isStatic ? BodyType::Static : BodyType::Dynamic;
    if (isStatic) {
        b->linearVelocity = {0, 0};
        b->angularVelocity = 0.0f;
        b->mass.invMass = 0.0f;
        b->mass.invInertia = 0.0f;
    } else {
        updateMassFromShape(id);
    }
}

void World::updateMassFromShape(BodyId id) {
    Body* b = body(id);
    if (!b) return;
    MassData& m = b->mass;

    // Smallest circle about the centre that covers the whole shape. Continuous
    // collision and the swept broadphase both need this bound, and a static
    // body still needs it even though it never integrates a mass.
    float reach = 0.0f;
    if (b->shape == ColliderShape::Circle) {
        reach = b->radius;
    } else {
        for (const Vec2& v : b->polygon.vertices) reach = std::max(reach, v.length());
    }
    b->boundingRadius = std::max(1e-4f, reach);

    if (b->type == BodyType::Static) {
        m.mass = 0.0f;
        m.invMass = 0.0f;
        m.inertia = 0.0f;
        m.invInertia = 0.0f;
        return;
    }
    if (b->shape == ColliderShape::Circle) {
        m.mass = std::max(0.0001f, b->mass.mass);
        m.inertia = 0.5f * m.mass * b->radius * b->radius;
    } else {
        // Polygon: standard area/second-moment formulas.
        const ConvexPolygon& poly = b->polygon;
        double area = 0.0, inertia = 0.0;
        for (size_t i = 0; i < poly.vertices.size(); ++i) {
            const Vec2& p = poly.vertices[i];
            const Vec2& q = poly.vertices[(i + 1) % poly.vertices.size()];
            const double cross = static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y;
            area += cross * 0.5;
            const double intx2 = static_cast<double>(p.x) * p.x + static_cast<double>(q.x) * p.x +
                                 static_cast<double>(q.x) * q.x;
            const double inty2 = static_cast<double>(p.y) * p.y + static_cast<double>(q.y) * p.y +
                                 static_cast<double>(q.y) * q.y;
            inertia += (cross / 12.0) * (intx2 + inty2);
        }
        area = std::fabs(area);
        inertia = std::fabs(inertia);
        // User mass is a density-like value; derive a density from the area.
        const float density = area > 1e-6 ? m.mass / static_cast<float>(area) : 1.0f;
        m.mass = std::max(1e-4f, static_cast<float>(area) * density);
        m.inertia = std::max(1e-6f, static_cast<float>(inertia) * density);
    }
    m.invMass = m.mass > 0.0f ? 1.0f / m.mass : 0.0f;
    m.invInertia = m.inertia > 0.0f ? 1.0f / m.inertia : 0.0f;
}

void World::applyMassChange(BodyId id, float newMass) {
    Body* b = body(id);
    if (!b) return;
    const float oldInvMass = b->mass.invMass;
    b->mass.mass = std::max(0.0001f, newMass);
    updateMassFromShape(id);
    // Preserve momentum across the change.
    if (oldInvMass > 0.0f && b->mass.invMass > 0.0f && b->type == BodyType::Dynamic) {
        // p = m*v is invariant, so v scales by newMass/oldMass, which is
        // newInvMass/oldInvMass. The inverse of this scales v *up* and injects
        // energy every time an object's mass changes.
        b->linearVelocity *= b->mass.invMass / oldInvMass;
    }
}

// --- simulation ------------------------------------------------------------

void World::step(float dt) { stepFixed(dt, 1); }

void World::stepFixed(float dt, int steps) {
    const double t0 = nowSeconds();
    for (int i = 0; i < steps; ++i) {
        ++frame_;
        // Collision detection runs once per frame; the solver then integrates
        // and re-solves over several sub-steps against the same manifold set.
        broadphase();
        narrowphase();
        const int sub = std::max(1, subSteps);
        const float h = dt / static_cast<float>(sub);
        for (int k = 0; k < sub; ++k) {
            const bool last = (k + 1 == sub);
            integrateForces(h);
            // The biased pass pushes overlapping bodies apart, which also
            // injects velocity; the relax pass immediately below takes that
            // velocity back out while still holding the contacts closed.
            // Without it a tall stack gains energy every step until it shears.
            solveVelocityConstraints(h, velocityIterations, /*useBias=*/true,
                                    /*warmStart=*/k == 0);
            integratePositions(h);
            if (enableContinuous) solveContinuous(h);
            if (relaxIterations > 0) {
                solveVelocityConstraints(h, relaxIterations, /*useBias=*/false,
                                        /*warmStart=*/false);
            }
            if (last) applyRestitution();
            persistent_ = contacts_;
        }
        persistentSensors_ = sensorPairs_;
        if (rollingFriction > 0.0f) applyRollingResistance(dt);
        if (positionIterations > 0) solvePositionConstraints();
        updateSleepState(dt);
        refreshAllBoundsAndAabbs();
    }
    stats_.stepMs = (nowSeconds() - t0) * 1000.0;
}

void World::integrateForces(float dt) {
    for (Body& b : bodies_) {
        if (b.type != BodyType::Dynamic || b.frozen || !b.enabled || b.sleeping) {
            b.force = {0, 0};
            b.torque = 0.0f;
            continue;
        }
        // Semi-implicit: velocity is updated from the force, then position is
        // integrated from the *new* velocity. This is stable under gravity.
        b.linearVelocity += (gravity + b.force * b.mass.invMass) * dt;
        b.angularVelocity += b.torque * b.mass.invInertia * kRadToDeg * dt;

        const float ld = 1.0f / (1.0f + dt * b.linearDamping);
        const float ad = 1.0f / (1.0f + dt * b.angularDamping);
        b.linearVelocity *= ld;
        b.angularVelocity *= ad;

        // Terminal velocity clamp: keeps tunnelling bounded on slow devices.
        const float maxSpeed = 250.0f;
        b.linearVelocity = b.linearVelocity.clamped(maxSpeed);
        b.angularVelocity = clampf(b.angularVelocity, -1440.0f, 1440.0f);

        // Forces are per-frame, like a real actuator: apply them once, then
        // clear. Leaving them set compounds every step and the body
        // accelerates without bound.
        b.force = {0, 0};
        b.torque = 0.0f;
    }
}

void World::integratePositions(float dt) {
    for (Body& b : bodies_) {
        if (b.frozen || !b.enabled || b.sleeping) continue;
        if (b.type != BodyType::Dynamic) {
            // Kinematic bodies move at their own velocity, ignoring forces.
            b.position += b.linearVelocity * dt;
            b.rotationDeg = wrapAngle(b.rotationDeg + b.angularVelocity * dt);
            continue;
        }
        b.previousPosition = b.position;
        b.previousRotation = b.rotationDeg;
        b.position += b.linearVelocity * dt;
        b.rotationDeg = wrapAngle(b.rotationDeg + b.angularVelocity * dt);
    }
}

void World::applyRollingResistance(float dt) {
    // Which bodies are actually touching something: a body in free fall has no
    // floor to press against and must not be slowed by a floor it never meets.
    std::vector<BodyId> grounded;
    grounded.reserve(bodies_.size());
    for (const Contact& c : contacts_) {
        if (!c.touching) continue;
        if (grounded.empty() || grounded.back() != c.a) grounded.push_back(c.a);
        if (grounded.back() != c.b) grounded.push_back(c.b);
    }
    if (grounded.empty()) return;

    for (BodyId id : grounded) {
        Body* b = body(id);
        if (!b || b->type != BodyType::Dynamic || b->frozen || b->sleeping) continue;
        if (b->angularVelocity == 0.0f || b->mass.invInertia == 0.0f) continue;
        // Torque = mu_roll * m * g * r, as a change in angular velocity.
        const float dw = rollingFriction * 9.81f * b->boundingRadius * dt * kRadToDeg;
        b->angularVelocity -= std::copysign(std::min(std::fabs(b->angularVelocity), dw),
                                           b->angularVelocity);
    }
}

void World::updateSleepState(float dt) {
    if (!allowSleep) return;
    const size_t count = bodies_.size();
    sleepCandidate_.assign(count, 0);

    for (size_t i = 0; i < count; ++i) {
        Body& b = bodies_[i];
        if (b.type != BodyType::Dynamic || b.frozen || !b.enabled) {
            b.sleepTimer = 0.0f;
            continue;
        }
        const bool slow = b.linearVelocity.lengthSq() < sleepLinearThreshold * sleepLinearThreshold &&
                          std::fabs(b.angularVelocity) < sleepAngularThreshold;
        if (slow) {
            b.sleepTimer += dt;
            if (b.sleeping || b.sleepTimer >= sleepTimeRequired) sleepCandidate_[i] = 1;
        } else {
            b.sleepTimer = 0.0f;
        }
    }

    // Sleeping has to be a property of a whole *island*, not of one body. If the
    // bottom of a stack dozes off while the boxes above it are still awake, the
    // two halves of the column stop being solved as one system: the sleeper is
    // pinned, the awake boxes keep accumulating the load they should have
    // shared, and the whole stack eventually tips as a rigid body. So a body may
    // only sleep if everything it is touching can sleep too. One pass is not
    // enough - a chain has to be resolved from both ends - so relax until the
    // candidate set stops shrinking.
    auto slotOf = [this, count](BodyId id) -> size_t {
        const auto it = index_.find(id);
        if (it == index_.end() || it->second >= count) return count;
        return it->second;
    };
    bool changed = true;
    while (changed) {
        changed = false;
        for (const Contact& c : contacts_) {
            if (bodies_[slotOf(c.a)].type != BodyType::Dynamic) continue;
            if (bodies_[slotOf(c.b)].type != BodyType::Dynamic) continue;
            const bool ca = sleepCandidate_[slotOf(c.a)] != 0;
            const bool cb = sleepCandidate_[slotOf(c.b)] != 0;
            if (ca && !cb) { sleepCandidate_[slotOf(c.a)] = 0; changed = true; }
            if (cb && !ca) { sleepCandidate_[slotOf(c.b)] = 0; changed = true; }
        }
    }

    for (size_t i = 0; i < count; ++i) {
        Body& b = bodies_[i];
        if (b.type != BodyType::Dynamic) continue;
        if (sleepCandidate_[i]) {
            b.sleeping = true;
            b.linearVelocity = {0, 0};
            b.angularVelocity = 0.0f;
        } else {
            b.sleeping = false;
        }
    }
}

void World::refreshBounds(Body& b) const {
    switch (b.shape) {
        case ColliderShape::Circle:
            b.aabb = Aabb2::fromCenterSize(b.position, Vec2(b.radius * 2.0f, b.radius * 2.0f));
            break;
        case ColliderShape::Capsule: {
            // AABB of the segment expanded by the radius.
            const float c = std::cos(degToRad(b.rotationDeg)), s = std::sin(degToRad(b.rotationDeg));
            const Vec2 h{-s * b.radius * 0.0f, 0.0f};
            (void)h;
            const Aabb2 local = b.polygon.bounds().expanded(b.radius);
            // Rotate the four corners into world space, then re-fit.
            Vec2 corners[4] = {{local.min.x, local.min.y}, {local.max.x, local.min.y},
                                {local.max.x, local.max.y}, {local.min.x, local.max.y}};
            Aabb2 out = Aabb2::fromCenterSize(b.position, {0, 0});
            for (int i = 0; i < 4; ++i) {
                const Vec2 r{corners[i].x * c - corners[i].y * s,
                             corners[i].x * s + corners[i].y * c};
                out = Aabb2::fromPoints(&b.position, 1).merged({r + b.position, r + b.position});
            }
            b.aabb = out;
            break;
        }
        case ColliderShape::Box:
        default: {
            // Transform the local bounds, then re-fit: conservative for any angle.
            if (b.polygon.vertices.empty()) {
                b.aabb = Aabb2::fromCenterSize(b.position, {0, 0});
                break;
            }
            const Aabb2 local = b.polygon.bounds();
            Vec2 pts[4] = {{local.min.x, local.min.y}, {local.max.x, local.min.y},
                           {local.max.x, local.max.y}, {local.min.x, local.max.y}};
            const float r = degToRad(b.rotationDeg), c = std::cos(r), s = std::sin(r);
            Aabb2 world = Aabb2::fromPoints(&b.position, 1);
            for (const Vec2& p : pts) {
                const Vec2 w{b.position.x + p.x * c - p.y * s, b.position.y + p.x * s + p.y * c};
                world = world.merged({w, w});
            }
            b.aabb = world;
            break;
        }
    }
}

void World::refreshAllBoundsAndAabbs() {
    for (Body& b : bodies_) refreshBounds(b);
}

bool World::shouldCollide(const Body& a, const Body& b, bool requireOverlap) const {
    if (!a.enabled || !b.enabled) return false;
    if (a.frozen && b.frozen) return false;
    if (a.type != BodyType::Dynamic && b.type != BodyType::Dynamic) return false;
    if (a.sensor && b.sensor) return false;
    // Layer filtering: two bodies collide unless at least one opts out.
    if (a.collisionGroup != 0 && a.collisionGroup == b.collisionGroup) return false;
    if ((a.collisionMask & (1 << b.collisionGroup)) == 0) return false;
    if ((b.collisionMask & (1 << a.collisionGroup)) == 0) return false;
    if (requireOverlap && !a.aabb.intersects(b.aabb)) return false;
    return true;
}

void World::broadphase() {
    grid_.clear();
    stats_.broadphasePairs = 0;
    stats_.bodies = static_cast<int>(bodies_.size());

    // Insert into the spatial hash. A body spanning several cells is inserted
    // into each, which keeps the query conservative.
    for (Body& b : bodies_) {
        if (!b.enabled) continue;
        const int x0 = static_cast<int>(std::floor(b.aabb.min.x / cellSize_));
        const int x1 = static_cast<int>(std::floor(b.aabb.max.x / cellSize_));
        const int y0 = static_cast<int>(std::floor(b.aabb.min.y / cellSize_));
        const int y1 = static_cast<int>(std::floor(b.aabb.max.y / cellSize_));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                grid_[GridKey{x, y}].push_back(b.id);
            }
        }
    }

    // Collect the candidate pairs once. The narrowphase reuses this list, so
    // the grid is walked one time per step instead of two.
    candidatePairs_.clear();
    for (const auto& cell : grid_) {
        const std::vector<BodyId>& ids = cell.second;
        for (size_t i = 0; i < ids.size(); ++i) {
            for (size_t j = i + 1; j < ids.size(); ++j) {
                if (ids[i] == ids[j]) continue;
                // Canonical ordering stops A/B and B/A both being emitted.
                candidatePairs_.emplace_back(std::min(ids[i], ids[j]), std::max(ids[i], ids[j]));
            }
        }
    }
    std::sort(candidatePairs_.begin(), candidatePairs_.end());
    candidatePairs_.erase(std::unique(candidatePairs_.begin(), candidatePairs_.end()),
                          candidatePairs_.end());
    stats_.broadphasePairs = static_cast<int>(candidatePairs_.size());
}

// ---------------------------------------------------------------------------
// Polygon face clipping (manifold generation)
// ---------------------------------------------------------------------------

namespace {

/// A convex polygon flattened into world space.
struct WorldPoly {
    std::vector<Vec2> v;
    std::vector<Vec2> normals;  ///< outward, one per edge
};

WorldPoly toWorld(const ConvexPolygon& p, const Vec2& pos, float rotDeg) {
    WorldPoly out;
    const float r = degToRad(rotDeg), c = std::cos(r), s = std::sin(r);
    out.v.reserve(p.vertices.size());
    out.normals.reserve(p.vertices.size());
    for (const Vec2& lv : p.vertices) {
        out.v.push_back({pos.x + lv.x * c - lv.y * s, pos.y + lv.x * s + lv.y * c});
    }
    for (size_t i = 0; i < out.v.size(); ++i) {
        const Vec2& p0 = out.v[i];
        const Vec2& p1 = out.v[(i + 1) % out.v.size()];
        Vec2 e{p1.x - p0.x, p1.y - p0.y};
        e = e.lengthSq() < 1e-12f ? Vec2(1, 0) : e.normalized();
        out.normals.push_back({e.y, -e.x});  // outward for CCW winding
    }
    return out;
}

/// Largest separation of [b] from any face plane of [a] (negative = touching).
/// Returns the face index with the best (least negative) separation.
float maxSeparation(const WorldPoly& a, const WorldPoly& b, int& bestIndex) {
    float best = -std::numeric_limits<float>::max();
    bestIndex = 0;
    for (size_t i = 0; i < a.normals.size(); ++i) {
        const Vec2& n = a.normals[i];
        float minSep = std::numeric_limits<float>::max();
        for (const Vec2& p : b.v) minSep = std::min(minSep, (p - a.v[i]).dot(n));
        if (minSep > best) { best = minSep; bestIndex = static_cast<int>(i); }
    }
    return best;
}

/// The face of [p] most anti-parallel to [dir] (the "incident" face).
int incidentFace(const WorldPoly& p, const Vec2& dir) {
    int best = 0;
    float bestDot = std::numeric_limits<float>::max();
    for (size_t i = 0; i < p.normals.size(); ++i) {
        const float d = p.normals[i].dot(dir);
        if (d < bestDot) { bestDot = d; best = static_cast<int>(i); }
    }
    return best;
}

/// Clips segment [s0, s1] to the half-plane { (p - o) . n <= 0 }.
/// Returns how many of the two output points survive (0, 1 or 2); the
/// surviving points are written back to [s0]/[s1] with their feature ids.
int clipSegment(Vec2& s0, Vec2& s1, uint32_t& id0, uint32_t& id1, const Vec2& n,
                const Vec2& o, uint32_t clipId) {
    const float d0 = (s0 - o).dot(n);
    const float d1 = (s1 - o).dot(n);
    int out = 0;
    if (d0 <= 0.0f) ++out;                       // s0 already inside
    if (d1 <= 0.0f) {                            // s1 inside
        if (out == 0) { s0 = s1; id0 = id1; }    // s0 becomes s1
        ++out;
    }
    if (d0 * d1 < 0.0f) {
        // The segment crosses the plane, so the crossing point is a new vertex.
        const float t = d0 / (d0 - d1);
        const Vec2 mid = s0 + (s1 - s0) * t;
        if (out == 0) { s0 = mid; id0 = clipId; }
        else          { s1 = mid; id1 = clipId; }
        ++out;
    }
    return out;
}

}  // namespace

bool World::clipPolygons(const Body& a, const Body& b, Manifold& out, float margin) {
    if (a.polygon.vertices.size() < 3 || b.polygon.vertices.size() < 3) return false;
    const WorldPoly pa = toWorld(a.polygon, a.position, a.rotationDeg);
    const WorldPoly pb = toWorld(b.polygon, b.position, b.rotationDeg);

    int faceA = 0, faceB = 0;
    // A gap inside [margin] still produces a contact. The solver then limits
    // the approach speed to exactly the gap, which is what stops a fast body
    // from passing straight through a thin wall between two frames.
    const float sepA = maxSeparation(pa, pb, faceA);
    if (sepA > margin) return false;  // a clear separating axis
    const float sepB = maxSeparation(pb, pa, faceB);
    if (sepB > margin) return false;

    // Pick the reference face: the one whose plane the other shape is furthest
    // from. Using the less-overlapped face keeps the manifold on the "flatter"
    // surface, which is what makes the result stable for unequal shapes.
    const WorldPoly* ref = &pa;
    const WorldPoly* inc = &pb;
    int refFace = faceA;
    uint32_t refId = 1, incId = 0;
    if (sepB > sepA + 0.1f * std::fabs(sepA) + 1e-3f) {
        std::swap(ref, inc);
        refFace = faceB;
        refId = 0;
        incId = 1;
    }
    if (ref->normals.size() < 3) return false;

    const Vec2 refNormal = ref->normals[refFace];
    const Vec2 refV1 = ref->v[refFace];
    const Vec2 refV2 = ref->v[(refFace + 1) % ref->v.size()];

    const int incFace = incidentFace(*inc, refNormal);
    Vec2 seg0 = inc->v[incFace];
    Vec2 seg1 = inc->v[(incFace + 1) % inc->v.size()];
    uint32_t id0 = (incId << 16) | static_cast<uint32_t>(incFace);
    uint32_t id1 = (incId << 16) | static_cast<uint32_t>(incFace + 1);

    // Clip the incident segment against the reference face's two side planes.
    const Vec2 side = (refV2 - refV1).normalized();
    if (clipSegment(seg0, seg1, id0, id1, -side, refV1,
                    (refId << 16) | 0x8001u) < 2) {
        return false;
    }
    if (clipSegment(seg0, seg1, id0, id1, side, refV2,
                    (refId << 16) | 0x8002u) < 2) {
        return false;
    }

    out.count = 0;
    for (int k = 0; k < 2; ++k) {
        const Vec2& p = k == 0 ? seg0 : seg1;
        const float sep = (p - refV1).dot(refNormal);
        if (sep > margin) continue;  // outside the reference face and too far
        if (out.count >= kMaxManifoldPoints) break;
        out.points[out.count] = p;
        out.separations[out.count] = sep;
        out.featureIds[out.count] = k == 0 ? id0 : id1;
        ++out.count;
    }
    if (out.count == 0) return false;

    // The position solver needs *two* anchors, one on each surface. Using the
    // same point for both would make anchorB - anchorA equal to the vector
    // between the body centres, not the gap between their faces.
    const bool refIsA = (ref == &pa);
    for (int k = 0; k < out.count; ++k) {
        const Vec2 p = out.points[k];
        const float sep = out.separations[k];
        if (refIsA) {
            out.faceA[k] = p - refNormal * sep;  // on A's reference face
            out.faceB[k] = p;                    // on B's incident face
        } else {
            out.faceA[k] = p;
            out.faceB[k] = p - refNormal * sep;
        }
    }

    out.normal = refNormal;
    // Orient A -> B.
    const Vec2 delta = b.position - a.position;
    if (out.normal.dot(delta) < 0.0f) out.normal = -out.normal;
    return true;
}

bool World::collideManifold(const Body& a, const Body& b, Manifold& out, float margin) {
    out.count = 0;
    if (a.shape == ColliderShape::Circle && b.shape == ColliderShape::Circle) {
        Contact c;
        if (!collideCircleCircle(a, b, c, margin)) return false;
        out.count = 1;
        out.normal = c.normal;
        out.points[0] = c.point;
        // [point] sits on A's surface; B's matching surface point is one
        // penetration further along the normal.
        out.faceA[0] = c.point;
        out.faceB[0] = c.point - c.normal * c.penetration;
        out.separations[0] = -c.penetration;
        out.featureIds[0] = 0;
        return true;
    }
    if (a.shape != ColliderShape::Circle && b.shape != ColliderShape::Circle) {
        return clipPolygons(a, b, out, margin);
    }
    // Mixed case: one circle, one polygon -> a single point.
    Contact c;
    Vec2 circlePoint{0, 0}, polyPoint{0, 0};
    bool hit = false;
    const bool aIsCircle = (a.shape == ColliderShape::Circle);
    if (aIsCircle) {
        hit = collidePolygonCircle(b, a, c, circlePoint, polyPoint, /*flip=*/true, margin);
    } else {
        hit = collidePolygonCircle(a, b, c, circlePoint, polyPoint, /*flip=*/false, margin);
    }
    if (!hit) return false;
    out.count = 1;
    out.normal = c.normal;
    out.points[0] = c.point;
    out.faceA[0] = aIsCircle ? circlePoint : polyPoint;
    out.faceB[0] = aIsCircle ? polyPoint : circlePoint;
    out.separations[0] = -c.penetration;
    out.featureIds[0] = 0;
    return true;
}

bool World::collideCircleCircle(const Body& a, const Body& b, Contact& out, float margin) {
    const Vec2 delta = b.position - a.position;
    const float distSq = delta.lengthSq();
    const float rsum = a.radius + b.radius + margin;
    if (distSq >= rsum * rsum) return false;
    const float dist = std::sqrt(std::max(distSq, 1e-12f));
    out.normal = dist > 1e-6f ? delta / dist : Vec2(0, 1);
    out.penetration = (a.radius + b.radius) - dist;
    // The anchor is the point on A's real surface. The margin widens *which*
    // contacts exist, never where they are: putting the anchor a margin outside
    // the surface lengthens the lever arm, and every impulse through it then
    // turns a little too much spin into translation (and the reverse). The
    // result is a ball that picks up speed from nothing while it rolls.
    out.point = a.position + out.normal * a.radius;
    return true;
}

bool World::collidePolygonCircle(const Body& poly, const Body& circ, Contact& out,
                                 Vec2& circlePoint, Vec2& polyPoint, bool flip,
                                 float margin) {
    // Work in the polygon's local space.
    const float r = degToRad(poly.rotationDeg), c = std::cos(r), s = std::sin(r);
    const Vec2 d = circ.position - poly.position;
    const Vec2 local{c * d.x + s * d.y, -s * d.x + c * d.y};

    const ConvexPolygon& p = poly.polygon;
    if (p.vertices.empty()) return false;

    const size_t count = p.vertices.size();

    // Separating axis over the face normals. [separation] is how far the centre
    // sits outside the nearest face, so anything beyond the radius misses.
    float separation = -std::numeric_limits<float>::max();
    size_t face = 0;
    for (size_t i = 0; i < count; ++i) {
        const Vec2& v0 = p.vertices[i];
        const Vec2& v1 = p.vertices[(i + 1) % count];
        Vec2 edge{v1.x - v0.x, v1.y - v0.y};
        if (edge.lengthSq() < 1e-12f) continue;
        edge = edge.normalized();
        const Vec2 n{edge.y, -edge.x};  // outward for CCW
        const float dist = (local - v0).dot(n);
        if (dist > separation) {
            separation = dist;
            face = i;
        }
    }
    if (separation > circ.radius + margin) return false;

    Vec2 bestNormal{0, 1};
    Vec2 bestPoint{0, 0};
    float depth = 0.0f;
    if (separation <= 0.0f) {
        // Centre is inside the polygon. The face it is nearest to is the one
        // that can push it out the shortest way, and the depth is whatever
        // radius is left over.
        const Vec2& v0 = p.vertices[face];
        const Vec2& v1 = p.vertices[(face + 1) % count];
        const Vec2 edge = (v1 - v0).normalized();
        bestNormal = Vec2{edge.y, -edge.x};
        bestPoint = v0;
        depth = circ.radius - separation;
    } else {
        // Centre is outside: only the nearest point on that face decides, and
        // the nearest point is either inside the face span or past one of its
        // two corners. Testing every vertex instead would miss a ball resting
        // on the middle of a long edge.
        const Vec2& v0 = p.vertices[face];
        const Vec2& v1 = p.vertices[(face + 1) % count];
        const Vec2 edge = v1 - v0;
        const float edgeLen = edge.length();
        const Vec2 axis = edge / edgeLen;
        const float along = (local - v0).dot(axis);
        Vec2 nearest;
        if (along <= 0.0f) {
            nearest = v0;
        } else if (along >= edgeLen) {
            nearest = v1;
        } else {
            nearest = v0 + axis * along;
        }
        const Vec2 delta = local - nearest;
        const float dist = delta.length();
        if (dist > circ.radius + margin) return false;
        bestNormal = dist > 1e-6f ? delta / dist : Vec2{axis.y, -axis.x};
        bestPoint = nearest;
        depth = circ.radius - dist;
    }

    // Back to world space.
    const Vec2 worldNormal{c * bestNormal.x - s * bestNormal.y, s * bestNormal.x + c * bestNormal.y};
    const Vec2 worldCorner = poly.position + Vec2{c * bestPoint.x - s * bestPoint.y,
                                                  s * bestPoint.x + c * bestPoint.y};
    out.penetration = depth;
    // Normal must point from the polygon towards the circle.
    out.normal = flip ? -worldNormal : worldNormal;

    // The deepest point of the circle sits one radius back along the normal;
    // the matching point of the polygon is [penetration] further along again.
    // Both are on the real surfaces -- [margin] only decides whether this
    // contact exists at all, and folding it into the anchors would stretch the
    // lever arm and make friction drive the wrong contact-point velocity.
    circlePoint = circ.position - worldNormal * circ.radius;
    polyPoint = circlePoint + worldNormal * depth;
    out.point = (circlePoint + polyPoint) * 0.5f;
    return true;
}

bool World::collidePolygonPolygonLegacy(const Body& a, const Body& b, Contact& out) {
    if (a.polygon.vertices.empty() || b.polygon.vertices.empty()) return false;
    return satTest(a.polygon, a.position, a.rotationDeg, b.polygon, b.position, b.rotationDeg,
                   out.normal, out.penetration, out.point);
}

void World::narrowphase() {
    // Reuse the pair list the broadphase already built.
    const std::vector<std::pair<BodyId, BodyId>>& candidates = candidatePairs_;

    contacts_.clear();
    sensorOverlaps_.clear();
    std::vector<BodyId> activeSensors;

    for (const auto& pr : candidates) {
        Body* pa = body(pr.first);
        Body* pb = body(pr.second);
        if (!pa || !pb) continue;
        if (!shouldCollide(*pa, *pb)) continue;

        if (listener_.broadphase) listener_.broadphase(*pa, *pb);

        Manifold manifold;
        if (!collideManifold(*pa, *pb, manifold, speculativeMargin)) continue;

        if (pa->sensor || pb->sensor) {
            const BodyId sensor = pa->sensor ? pa->id : pb->id;
            const BodyId other = pa->sensor ? pb->id : pa->id;
            sensorOverlaps_[sensor].push_back(other);
            if (pa->sensor) activeSensors.push_back(pa->id);
            if (pb->sensor) activeSensors.push_back(pb->id);
            continue;  // sensors never push a solver contact
        }
        if (pa->sleeping && pb->sleeping) continue;

        // Each manifold point becomes its own contact so the solver can apply
        // impulses independently at both ends of the shared face.
        const int manifoldStart = static_cast<int>(contacts_.size());
        for (int k = 0; k < manifold.count; ++k) {
            Contact c;
            c.a = pa->id;
            c.b = pb->id;
            c.featureId = manifold.featureIds[k];
            c.manifoldStart = manifoldStart;
            c.manifoldCount = manifold.count;
            c.point = manifold.points[k];
            c.normal = manifold.normal;
            c.penetration = -manifold.separations[k];

            // Anchors for the position solver: one point on each surface,
            // relative to that body's centre, plus the centres themselves at
            // the moment of creation.
            c.anchorA = manifold.faceA[k] - pa->position;
            c.anchorB = manifold.faceB[k] - pb->position;
            c.posA = pa->position;
            c.posB = pb->position;
            c.separation = manifold.separations[k];
            c.touching = c.separation <= 0.0f;
            c.adjustedSeparation =
                c.separation - (c.anchorB - c.anchorA).dot(c.normal);

            // Approach speed at the moment of touch. Restitution is applied
            // against this rather than against the live velocity, so a body
            // that has already been slowed by the push-out does not bounce twice.
            const Vec2 va = pa->linearVelocity;
            const Vec2 vb = pb->linearVelocity;
            c.approachVelocity = (vb - va).dot(c.normal);

            // Warm starting seed: inherit the impulse the solver converged on
            // last frame for this same feature.
            for (const Contact& old : persistent_) {
                if (old.a == c.a && old.b == c.b && old.featureId == c.featureId) {
                    c.normalImpulse = old.normalImpulse;
                    c.tangentImpulse = old.tangentImpulse;
                    break;
                }
            }
            contacts_.push_back(c);
        }
    }

    // Flatten the sensor overlaps into an ordered, unique pair list. The
    // listener diffs this against last frame: a pair that just appeared is a
    // begin, a pair that just vanished is an end. Without that bookkeeping a
    // trigger re-fires on every single frame it is being overlapped.
    std::vector<std::pair<BodyId, BodyId>> sensorPairs;
    sensorPairs.reserve(sensorOverlaps_.size());
    for (const auto& entry : sensorOverlaps_) {
        for (BodyId other : entry.second) {
            sensorPairs.emplace_back(std::min(entry.first, other), std::max(entry.first, other));
        }
    }
    std::sort(sensorPairs.begin(), sensorPairs.end());
    sensorPairs.erase(std::unique(sensorPairs.begin(), sensorPairs.end()), sensorPairs.end());

    if (listener_.begin || listener_.end) {
        for (const auto& pr : sensorPairs) {
            if (std::find(persistentSensors_.begin(), persistentSensors_.end(), pr) !=
                persistentSensors_.end())
                continue;
            if (!listener_.begin) break;
            const Body* a = body(pr.first);
            const Body* b = body(pr.second);
            if (!a || !b) continue;
            Contact c;
            c.a = pr.first;
            c.b = pr.second;
            listener_.begin(*a, *b, c);
        }
        for (const auto& pr : persistentSensors_) {
            if (std::find(sensorPairs.begin(), sensorPairs.end(), pr) != sensorPairs.end()) continue;
            if (!listener_.end) break;
            const Body* a = body(pr.first);
            const Body* b = body(pr.second);
            if (!a || !b) continue;
            Contact c;
            c.a = pr.first;
            c.b = pr.second;
            listener_.end(*a, *b, c);
        }
    }
    sensorPairs_ = sensorPairs;

    stats_.contacts = static_cast<int>(contacts_.size());
}

// --- contact solving ------------------------------------------------------

namespace {

/// Vector from a body's centre of mass to the contact point.
///
/// A circle's is exactly [-normal * radius]: the contact is always one radius
/// back along the normal, whatever the body has done since the manifold was
/// built. Taking it from the stored contact point instead is subtly wrong,
/// because a body integrates at every sub-step while the manifold is only
/// rebuilt once per frame -- the arm drifts sideways by v*h, and that sideways
/// drift shows up as a phantom tangential velocity w*rb.x. Friction then
/// chases a slip that does not exist, and a ball rolling at a steady speed
/// quietly accelerates: 0.14 u/s became 0.32 u/s over ten seconds before this
/// was fixed.
Vec2 leverArm(const Body& body, const Vec2& point, const Vec2& normal, int side) {
    if (body.shape == ColliderShape::Circle) return normal * (body.radius * side);
    return point - body.position;
}

/// Relative velocity of B with respect to A at a world contact point,
/// including the angular term. Angular velocity is stored in degrees/second.
Vec2 contactVelocity(const Body& a, const Body& b, const Vec2& ra, const Vec2& rb) {
    const float wa = a.angularVelocity * kDegToRad;
    const float wb = b.angularVelocity * kDegToRad;
    const Vec2 va = a.linearVelocity + Vec2(-wa * ra.y, wa * ra.x);
    const Vec2 vb = b.linearVelocity + Vec2(-wb * rb.y, wb * rb.x);
    return vb - va;
}

/// Applies a raw linear+angular impulse pair to both bodies.
void applyContactImpulse(Body& a, Body& b, const Vec2& ra, const Vec2& rb, const Vec2& impulse) {
    a.linearVelocity -= impulse * a.mass.invMass;
    b.linearVelocity += impulse * b.mass.invMass;
    // cross(r, impulse) is the angular impulse in radians; convert on the way
    // into the degree/second the Body stores.
    a.angularVelocity -= ra.cross(impulse) * a.mass.invInertia * kRadToDeg;
    b.angularVelocity += rb.cross(impulse) * b.mass.invInertia * kRadToDeg;
}

/// Current signed gap between the two shapes along the contact normal, replayed
/// from the anchors captured at contact time so it stays correct as the solver
/// moves bodies around.
float contactSeparation(const Contact& c, const Body& a, const Body& b) {
    const Vec2 ra = c.anchorA + (a.position - c.posA);
    const Vec2 rb = c.anchorB + (b.position - c.posB);
    return (rb - ra).dot(c.normal) + c.adjustedSeparation;
}

/// Parameters of the soft contact constraint for one step, derived from the
/// requested spring frequency and damping ratio.
///
/// A rigid ("never overlap") constraint is what makes a naive sequential-impulse
/// solver need one sweep per body in a stack: push the bottom contact and the
/// one above it does not know about it yet. Treating the contact as a heavily
/// damped spring instead lets a little overlap stand in for that propagation,
/// and the impulse decay (`impulseScale`) quietly removes the energy the
/// push-out adds. A twenty-box stack then stands on four sweeps instead of
/// fifty.
struct SoftContact {
    float biasRate = 0.0f;     ///< how hard overlap is pushed out
    float massScale = 1.0f;   ///< under-relaxation of the effective mass
    float impulseScale = 0.0f;///< per-step decay of the accumulated impulse
    bool soft = false;
};

SoftContact makeSoftContact(float hertz, float zeta, float h) {
    SoftContact sc;
    if (hertz <= 0.0f || h <= 0.0f) return sc;  // rigid contacts
    // Past a quarter of the step rate the spring is stiffer than the integrator
    // can represent, so cap it there.
    const float rate = std::min(hertz, 0.25f / h);
    const float omega = 2.0f * kPi * rate;
    const float denom = 2.0f * zeta + h * omega;
    sc.biasRate = omega / denom;
    const float c = h * omega * denom;
    sc.massScale = c / (1.0f + c);
    sc.impulseScale = 1.0f / (1.0f + c);
    sc.soft = true;
    return sc;
}

/// Velocity the two bodies are allowed to keep approaching each other with.
///
/// Three regimes, and getting them mixed up is what makes contacts either
/// sticky or useless:
///   * a real gap -- speculative. The bodies may close it during this step and
///     not a hair more, which is exactly what catches something moving fast
///     enough to jump a thin wall between two narrowphase runs;
///   * an overlap -- the soft spring pushes them apart at a rate proportional
///     to how deep they are, damped by [SoftContact];
///   * the relax pass -- no bias at all, because its whole job is to take back
///     the separating velocity the biased pass just added.
float approachTarget(float separation, float dt, const SoftContact& sc, float slop,
                     bool useBias) {
    // A gap permits closing at exactly that rate -- which is a *negative*
    // target, since positive is the direction the solver treats as separating.
    if (separation > 0.0f) return -separation / dt;
    if (!useBias || !sc.soft) return 0.0f;
    return -sc.biasRate * std::min(0.0f, separation + slop);
}


/// Effective-mass matrix of a two-point manifold along [dir].
struct MassMatrix {
    float k11, k12, k22;
};

MassMatrix effectiveMass(const Body& pa, const Body& pb, const Vec2& rA1, const Vec2& rB1,
                         const Vec2& rA2, const Vec2& rB2, const Vec2& dir) {
    const float j1 = rA1.cross(dir), j2 = rA2.cross(dir);
    const float k1 = rB1.cross(dir), k2 = rB2.cross(dir);
    const float m = pa.mass.invMass + pb.mass.invMass;
    return {m + pa.mass.invInertia * j1 * j1 + pb.mass.invInertia * k1 * k1,
            m + pa.mass.invInertia * j1 * j2 + pb.mass.invInertia * k1 * k2,
            m + pa.mass.invInertia * j2 * j2 + pb.mass.invInertia * k2 * k2};
}

/// Solves the coupled 2x2 constraint for both points of a manifold.
/// Points solved independently each assume their body is a point mass, so the
/// solver needs many iterations to discover that loading one corner also lifts
/// the other; solving the pair together gets it in one step, which is what lets
/// deep stacks stand up.
void solveNormalTwoPoints(Body& pa, Body& pb, Contact& c1, Contact& c2, float dt, float slop,
                         const SoftContact& sc, bool useBias) {
    const Vec2& n = c1.normal;
    const Vec2 rA1 = c1.point - pa.position;
    const Vec2 rB1 = c1.point - pb.position;
    const Vec2 rA2 = c2.point - pa.position;
    const Vec2 rB2 = c2.point - pb.position;

    MassMatrix k = effectiveMass(pa, pb, rA1, rB1, rA2, rB2, n);
    if (k.k11 <= 0.0f || k.k22 <= 0.0f) return;

    const float vn1 = contactVelocity(pa, pb, rA1, rB1).dot(n);
    const float vn2 = contactVelocity(pa, pb, rA2, rB2).dot(n);
    // Positive for a gap or an overlap: it is the speed the pair is driven
    // towards, and the solve below drives vn towards it, so it is subtracted
    // from the measured approach speed.
    const float sep1 = contactSeparation(c1, pa, pb);
    const float sep2 = contactSeparation(c2, pa, pb);
    const float bias1 = approachTarget(sep1, dt, sc, slop, useBias);
    const float bias2 = approachTarget(sep2, dt, sc, slop, useBias);
    // The soft spring models a gap as a spring; a purely speculative contact is
    // already an exact inequality, so it is solved rigidly or the allowed
    // closing speed comes out soft and the body still slips through. A manifold
    // that is half gap and half contact stays on the soft scale: mixing the two
    // inside one 2x2 system is what makes a resting body launch itself.
    const bool allSpeculative = sep1 > 0.0f && sep2 > 0.0f;
    const float springScale = sc.soft && !allSpeculative ? sc.massScale : 1.0f;
    const float impulseDecay = sc.soft && !allSpeculative ? sc.impulseScale : 0.0f;

    // Each point restarts from its decayed impulse, so the pair is relaxed
    // rather than locked at last frame's value. A point that is still in the
    // gap starts from nothing: the impulse it held as a solid contact belongs
    // to a different constraint, and replaying it under the new "you may close
    // this gap" rule fires the body off the surface.
    const float seed1 = sep1 > 0.0f ? 0.0f : c1.normalImpulse * (1.0f - impulseDecay);
    const float seed2 = sep2 > 0.0f ? 0.0f : c2.normalImpulse * (1.0f - impulseDecay);
    const float ms = springScale;
    const float k11 = k.k11 * ms, k12 = k.k12 * ms, k22 = k.k22 * ms;

    const float a1 = vn1 - bias1;
    const float a2 = vn2 - bias2;
    const float det = k11 * k22 - k12 * k12;
    float n1 = 0.0f, n2 = 0.0f;
    bool coupled = false;
    if (std::fabs(det) > 1e-12f) {
        n1 = seed1 - (k22 * a1 - k12 * a2) / det;
        n2 = seed2 - (k11 * a2 - k12 * a1) / det;
        coupled = n1 >= 0.0f && n2 >= 0.0f;
    }

    if (coupled) {
        const float d1 = n1 - c1.normalImpulse;
        const float d2 = n2 - c2.normalImpulse;
        c1.normalImpulse = n1;
        c2.normalImpulse = n2;
        if (d1 != 0.0f) applyContactImpulse(pa, pb, rA1, rB1, n * d1);
        if (d2 != 0.0f) applyContactImpulse(pa, pb, rA2, rB2, n * d2);
        return;
    }

    // The coupled step wants to pull, so fall back to relaxing the points one
    // at a time, each against the velocity the other has already been given.
    const float s1 = std::max(0.0f, seed1 - ms * a1 / k.k11);
    const float d1 = s1 - c1.normalImpulse;
    c1.normalImpulse = s1;
    if (d1 != 0.0f) applyContactImpulse(pa, pb, rA1, rB1, n * d1);

    const float a2b = contactVelocity(pa, pb, rA2, rB2).dot(n) - bias2;
    const float s2 = std::max(0.0f, seed2 - ms * a2b / k.k22);
    const float d2 = s2 - c2.normalImpulse;
    c2.normalImpulse = s2;
    if (d2 != 0.0f) applyContactImpulse(pa, pb, rA2, rB2, n * d2);
}

void solveTangentTwoPoints(Body& pa, Body& pb, Contact& c1, Contact& c2) {
    const Vec2 t = c1.normal.perpendicular();
    const Vec2 rA1 = leverArm(pa, c1.point, c1.normal, +1);
    const Vec2 rB1 = leverArm(pb, c1.point, c1.normal, -1);
    const Vec2 rA2 = leverArm(pa, c2.point, c1.normal, +1);
    const Vec2 rB2 = leverArm(pb, c2.point, c1.normal, -1);

    const MassMatrix k = effectiveMass(pa, pb, rA1, rB1, rA2, rB2, t);
    if (k.k11 <= 0.0f || k.k22 <= 0.0f) return;

    const float vt1 = contactVelocity(pa, pb, rA1, rB1).dot(t);
    const float vt2 = contactVelocity(pa, pb, rA2, rB2).dot(t);

    // Coulomb cone, anchored on the normal impulse solved this iteration.
    const float maxFriction =
        std::max(0.0f, std::sqrt(pa.friction * pb.friction) *
                           std::min(c1.normalImpulse, c2.normalImpulse));

    if (maxFriction <= 0.0f) {
        const float d1 = -c1.tangentImpulse;
        const float d2 = -c2.tangentImpulse;
        c1.tangentImpulse = 0.0f;
        c2.tangentImpulse = 0.0f;
        if (d1 != 0.0f) applyContactImpulse(pa, pb, rA1, rB1, t * d1);
        if (d2 != 0.0f) applyContactImpulse(pa, pb, rA2, rB2, t * d2);
        return;
    }

    const float det = k.k11 * k.k22 - k.k12 * k.k12;
    if (std::fabs(det) > 1e-12f) {
        const float l1 = -(k.k22 * vt1 - k.k12 * vt2) / det;
        const float l2 = -(k.k11 * vt2 - k.k12 * vt1) / det;
        const float x1 = c1.tangentImpulse + l1;
        const float x2 = c2.tangentImpulse + l2;
        if (std::fabs(x1) <= maxFriction && std::fabs(x2) <= maxFriction) {
            const float d1 = x1 - c1.tangentImpulse;
            const float d2 = x2 - c2.tangentImpulse;
            c1.tangentImpulse = x1;
            c2.tangentImpulse = x2;
            if (d1 != 0.0f) applyContactImpulse(pa, pb, rA1, rB1, t * d1);
            if (d2 != 0.0f) applyContactImpulse(pa, pb, rA2, rB2, t * d2);
            return;
        }
    }

    // The cone binds: relax the points one at a time, each against the
    // velocity the other has already been given.
    const float s1 = clampf(c1.tangentImpulse - vt1 / k.k11, -maxFriction, maxFriction);
    const float d1 = s1 - c1.tangentImpulse;
    c1.tangentImpulse = s1;
    if (d1 != 0.0f) applyContactImpulse(pa, pb, rA1, rB1, t * d1);

    const float vt2b = contactVelocity(pa, pb, rA2, rB2).dot(t);
    const float s2 = clampf(c2.tangentImpulse - vt2b / k.k22, -maxFriction, maxFriction);
    const float d2 = s2 - c2.tangentImpulse;
    c2.tangentImpulse = s2;
    if (d2 != 0.0f) applyContactImpulse(pa, pb, rA2, rB2, t * d2);
}

}  // namespace

void World::solveVelocityConstraints(float dt, int iterations, bool useBias,
                                     bool warmStart) {
    SoftContact sc = makeSoftContact(contactHertz, contactDampingRatio, dt);
    if (!useBias) {
        // The relax pass exists to take back the separating velocity the push-out
        // added. Leaving the bias on would just re-add it and a body recovering
        // from an overlap would sail away instead of settling.
        sc.biasRate = 0.0f;
    }

    // Warm start: replay the impulse each contact converged on last frame, so
    // the solver only has to correct the *change* rather than rediscover the
    // whole solution from scratch every step.
    for (Contact& c : contacts_) {
        if (!useBias || !warmStart) break;  // the relax pass starts from the state above
        Body* a = body(c.a);
        Body* b = body(c.b);
        if (!a || !b) continue;
        const Vec2 p = c.normal * (c.normalImpulse * warmStartScale) +
                       c.normal.perpendicular() * (c.tangentImpulse * warmStartScale);
        applyContactImpulse(*a, *b, c.point - a->position, c.point - b->position, p);
    }

    // One entry per manifold, in contact order, so the sweeps below visit each
    // manifold exactly once (its points are solved together, not separately).
    manifoldStart_.clear();
    for (size_t i = 0; i < contacts_.size(); ++i) {
        if (contacts_[i].manifoldStart == static_cast<int>(i)) manifoldStart_.push_back(i);
    }

    for (int iter = 0; iter < iterations; ++iter) {
        // Alternate the sweep direction every iteration. A one-way Gauss-Seidel
        // sweep always favours the manifolds it reaches first, so a tall stack
        // accumulates a systematic lean that eventually shears it apart;
        // sweeping both ways removes that bias.
        const bool reverse = (iter & 1) != 0;
        for (size_t mi = 0; mi < manifoldStart_.size(); ++mi) {
            const size_t base = manifoldStart_[reverse ? manifoldStart_.size() - 1 - mi : mi];
            Contact& c = contacts_[base];
            const int count = c.manifoldCount;
            Body* a = body(c.a);
            Body* b = body(c.b);
            if (!a || !b) continue;
            if (a->sleeping && b->sleeping) continue;

            if (count == 2) {
                solveNormalTwoPoints(*a, *b, c, contacts_[base + 1], dt, slop, sc,
                                     useBias);
                continue;
            }

            const float invMassSum = a->mass.invMass + b->mass.invMass;
            if (invMassSum <= 0.0f) continue;

            // Position of the contact relative to each centre of mass.
            const Vec2 ra = leverArm(*a, c.point, c.normal, +1);
            const Vec2 rb = leverArm(*b, c.point, c.normal, -1);
            // Angular contribution to the effective mass. This is the standard
            // constraint Jacobian term cross(r, n) * invI; it is unit-consistent
            // in radians, so no deg<->rad conversion belongs here (doing so
            // inflates the term by ~3000x and makes contacts behave like glue).
            const float crossA = ra.cross(c.normal) * a->mass.invInertia;
            const float crossB = rb.cross(c.normal) * b->mass.invInertia;
            const float ms = sc.soft ? sc.massScale : 1.0f;
            const float normalMass = ms / (invMassSum + crossA * crossA + crossB * crossB);
            c.normalMass = normalMass;

            const float vn = contactVelocity(*a, *b, ra, rb).dot(c.normal);
            const float sep = contactSeparation(c, *a, *b);
            const float bias = approachTarget(sep, dt, sc, slop, useBias);
            const bool speculative = sep > 0.0f;
            // A speculative contact starts from zero for the same reason: the
            // impulse it held as a solid contact is meaningless once the rule
            // changes to "you may approach this fast".
            const float seed = speculative
                                   ? 0.0f
                                   : c.normalImpulse *
                                         (1.0f - (sc.soft ? sc.impulseScale : 0.0f));
            // A contact can push but never pull, so the accumulated impulse is
            // clamped at zero.
            const float mass = speculative ? 1.0f / (invMassSum + crossA * crossA + crossB * crossB)
                                            : normalMass;
            const float newImpulse = std::max(0.0f, seed - mass * (vn - bias));
            const float applied = newImpulse - c.normalImpulse;
            c.normalImpulse = newImpulse;
            if (applied != 0.0f) applyContactImpulse(*a, *b, ra, rb, c.normal * applied);
        }

        // Coulomb friction along the tangent.
        for (size_t mi = 0; mi < manifoldStart_.size(); ++mi) {
            const size_t base = manifoldStart_[reverse ? manifoldStart_.size() - 1 - mi : mi];
            Contact& c = contacts_[base];
            const int count = c.manifoldCount;
            Body* a = body(c.a);
            Body* b = body(c.b);
            if (!a || !b) continue;
            if (a->sleeping && b->sleeping) continue;

            if (count == 2) {
                solveTangentTwoPoints(*a, *b, c, contacts_[base + 1]);
                continue;
            }

            const Vec2 tangent = c.normal.perpendicular();
            const Vec2 ra = leverArm(*a, c.point, c.normal, +1);
            const Vec2 rb = leverArm(*b, c.point, c.normal, -1);
            const float crossA = ra.cross(tangent) * a->mass.invInertia;
            const float crossB = rb.cross(tangent) * b->mass.invInertia;
            const float invMassSum = a->mass.invMass + b->mass.invMass;
            const float effMass = 1.0f / (invMassSum + crossA * crossA + crossB * crossB);
            if (effMass <= 0.0f) continue;

            // Relative velocity *at the contact point*. Omitting the angular
            // term here makes friction fight the wrong velocity whenever a body
            // is spinning, which injects energy instead of removing it.
            const float vt = contactVelocity(*a, *b, ra, rb).dot(tangent);
            const float lambda = -vt * effMass;
            // Clamp to the friction cone anchored by the normal impulse.
            const float maxFriction =
                std::max(0.0f, std::sqrt(a->friction * b->friction) * c.normalImpulse);
            const float old = c.tangentImpulse;
            c.tangentImpulse = clampf(old + lambda, -maxFriction, maxFriction);
            applyContactImpulse(*a, *b, ra, rb, tangent * (c.tangentImpulse - old));
        }
    }

    if (useBias) {
        for (const Contact& c : contacts_) {
            Body* a = body(c.a);
            Body* b = body(c.b);
            if (a && b && listener_.solve) listener_.solve(*a, *b, c);
        }
    }
}

/// Bounce. This has to be the very last thing that touches the velocities:
/// folding it into the push-out makes a resting body buzz and a real bounce
/// come out wrong, and running it before the relax pass lets that pass cancel
/// the rebound it just created.
void World::applyRestitution() {
    for (size_t mi = 0; mi < manifoldStart_.size(); ++mi) {
        const size_t base = manifoldStart_[mi];
        Contact& c = contacts_[base];
        Body* a = body(c.a);
        Body* b = body(c.b);
        if (!a || !b) continue;
        const int count = c.manifoldCount;
        const float restitution = std::min(a->restitution, b->restitution);
        // Only bounce off a genuine impact: approachVelocity is negative while
        // closing. A slow arrival is a body coming to rest, and bouncing it just
        // makes it jitter forever.
        if (restitution <= 0.0f || c.approachVelocity > -1.0f) continue;
        if (!c.touching) continue;  // still inside the speculative margin
        const float target = -restitution * c.approachVelocity;
        for (int j = 0; j < count; ++j) {
            Contact& cj = contacts_[base + j];
            const Vec2 ra = cj.point - a->position;
            const Vec2 rb = cj.point - b->position;
            const float crossA = ra.cross(cj.normal) * a->mass.invInertia;
            const float crossB = rb.cross(cj.normal) * b->mass.invInertia;
            const float invMassSum = a->mass.invMass + b->mass.invMass;
            const float mass = 1.0f / (invMassSum + crossA * crossA + crossB * crossB);
            const float vn = contactVelocity(*a, *b, ra, rb).dot(cj.normal);
            const float newImpulse =
                std::max(0.0f, cj.normalImpulse + mass * (target - vn));
            const float applied = newImpulse - cj.normalImpulse;
            cj.normalImpulse = newImpulse;
            if (applied != 0.0f) applyContactImpulse(*a, *b, ra, rb, cj.normal * applied);
        }
    }
}

void World::applyContact(Body& a, Body& b, const Contact& c, float factor) {
    // Recompute the separation from the stored anchors rather than reusing
    // [c.penetration]. The moment the first iteration moves a body the cached
    // penetration is stale, and repeating a full-depth stale push is exactly
    // what makes a tall stack detonate as soon as the iteration count rises.
    const Vec2 ra = c.anchorA + (a.position - c.posA);
    const Vec2 rb = c.anchorB + (b.position - c.posB);
    // The anchors are points on each *surface*, so their difference along the
    // normal is the real gap between the shapes, not the distance between
    // their centres. [adjustedSeparation] is captured at contact time so the
    // formula reproduces the gap exactly on the first iteration.
    const float separation = (rb - ra).dot(c.normal) + c.adjustedSeparation + slop;
    if (separation >= 0.0f) return;  // already clear of each other

    const float invMassSum = a.mass.invMass + b.mass.invMass;
    if (invMassSum <= 0.0f) return;
    const float depth = -separation * clampf(factor, 0.0f, 1.0f);
    if (depth <= 0.0f) return;
    const Vec2 correction = c.normal * (depth / invMassSum);
    a.position -= correction * a.mass.invMass;
    b.position += correction * b.mass.invMass;
}

namespace {

/// Lower bound on the distance between two shapes placed at explicit poses.
/// Positive means "definitely at least this far apart".
///
/// Conservative advancement needs an *under*-estimate: over-reporting the gap
/// would step the body straight through the obstacle. A separating-axis gap is
/// exactly that, because the distance from one shape to the nearest face plane
/// of the other is never more than the true distance between them. So the same
/// test the narrowphase runs doubles as the distance bound here.
float poseSeparation(const Body& a, const Vec2& posA, float rotA, const Body& b, const Vec2& posB,
                     float rotB, Vec2& normal) {
    normal = Vec2{0, 1};
    if (a.shape == ColliderShape::Circle && b.shape == ColliderShape::Circle) {
        const Vec2 d = posB - posA;
        const float dist = d.length();
        if (dist > 1e-6f) normal = d / dist;
        return dist - (a.radius + b.radius);
    }

    const bool aCircle = a.shape == ColliderShape::Circle;
    const Body* poly = aCircle ? &b : &a;
    const Body* circ = aCircle ? &a : &b;
    const Vec2& pc = aCircle ? posA : posB;  // the circle's centre
    const WorldPoly wp = toWorld(poly->polygon, aCircle ? posB : posA, aCircle ? rotB : rotA);

    // Nearest face plane to the circle's centre.
    float best = -std::numeric_limits<float>::max();
    size_t face = 0;
    for (size_t i = 0; i < wp.normals.size(); ++i) {
        const float sep = (pc - wp.v[i]).dot(wp.normals[i]);
        if (sep > best) {
            best = sep;
            face = i;
        }
    }
    if (best <= 0.0f) {
        normal = wp.normals[face];  // centre is inside the polygon
        return best - circ->radius;
    }

    // Centre is outside: the gap is to the nearest point on that face, which is
    // the projection clamped into the face's span.
    const Vec2& v0 = wp.v[face];
    const Vec2& v1 = wp.v[(face + 1) % wp.v.size()];
    const Vec2 edge = v1 - v0;
    const float len = edge.length();
    Vec2 nearest = v0;
    if (len > 1e-6f) {
        const Vec2 axis = edge / len;
        nearest = v0 + axis * clampf((pc - v0).dot(axis), 0.0f, len);
    }
    const Vec2 delta = pc - nearest;
    const float dist = delta.length();
    if (dist > 1e-6f) normal = delta / dist;
    return dist - circ->radius;
}

/// Lower bound on the distance between polygon [pa] and body [b] at [pb].
float poseSeparationPoly(const WorldPoly& pa, const Body& b, const Vec2& pb, float rotB) {
    if (b.shape == ColliderShape::Circle) {
        float best = -std::numeric_limits<float>::max();
        size_t face = 0;
        for (size_t i = 0; i < pa.normals.size(); ++i) {
            const float sep = (pb - pa.v[i]).dot(pa.normals[i]);
            if (sep > best) {
                best = sep;
                face = i;
            }
        }
        return best - b.radius;
    }
    const WorldPoly other = toWorld(b.polygon, pb, rotB);
    int f1 = 0, f2 = 0;
    return std::max(maxSeparation(pa, other, f1), maxSeparation(other, pa, f2));
}

}  // namespace

float World::timeOfImpact(const Body& a, const Vec2& posA0, float rotA0, const Vec2& posA1,
                          float rotA1, const Body& b, float travelBound, float margin) {
    if (travelBound <= 0.0f) return 1.0f;

    // Conservative advancement: start at the pose we know is clear and walk
    // forward. The remaining gap is a proven lower bound on the distance and
    // [travelBound] is an upper bound on how far that distance can shrink over
    // the whole step, so [t + gap/travelBound] can never step past the real
    // moment of contact. With a tight bound the walk lands on the answer in one
    // or two steps; with a loose one it closes in geometrically.
    float t = 0.0f;
    for (int iter = 0; iter < 48; ++iter) {
        const Vec2 pos = posA0 + (posA1 - posA0) * t;
        const float rot = rotA0 + (rotA1 - rotA0) * t;
        Vec2 normal{0, 1};
        float sep;
        if (a.shape == ColliderShape::Circle || b.shape == ColliderShape::Circle) {
            sep = poseSeparation(a, pos, rot, b, b.position, b.rotationDeg, normal);
        } else {
            const WorldPoly moved = toWorld(a.polygon, pos, rot);
            sep = poseSeparationPoly(moved, b, b.position, b.rotationDeg);
        }
        if (sep <= margin) return t;
        t += sep / travelBound;
        if (t >= 1.0f) return 1.0f;
    }
    return 1.0f;
}

void World::solveContinuous(float dt) {
    for (Body& b : bodies_) {
        if (b.type != BodyType::Dynamic || !b.enabled || b.frozen || b.sleeping) continue;
        const Vec2 motion = b.position - b.previousPosition;
        const float travel = motion.length();
        // The sweep is only worth its cost when the body covered more ground
        // than its own size; anything slower is already caught by the discrete
        // contacts, and running it on everything would dominate the frame.
        if (travel <= b.boundingRadius && !b.bullet) continue;

        // Upper bound on how fast the surface can close: the centre's speed
        // plus whatever the spin adds at the rim.
        const float spin = std::fabs(b.angularVelocity) * kDegToRad * b.boundingRadius * dt;
        // Everything the surface could possibly cover this sub-step: the
        // straight-line travel plus whatever the spin adds at the rim.
        const float bound = travel + spin;
        if (bound <= 0.0f) continue;

        // Sweep the broadphase over the whole travel, not just where it lands.
        const float pad = b.boundingRadius;
        const Aabb2 swept{{std::min(b.previousPosition.x, b.position.x) - pad,
                           std::min(b.previousPosition.y, b.position.y) - pad},
                          {std::max(b.previousPosition.x, b.position.x) + pad,
                           std::max(b.previousPosition.y, b.position.y) + pad}};
        const int x0 = static_cast<int>(std::floor(swept.min.x / cellSize_));
        const int x1 = static_cast<int>(std::floor(swept.max.x / cellSize_));
        const int y0 = static_cast<int>(std::floor(swept.min.y / cellSize_));
        const int y1 = static_cast<int>(std::floor(swept.max.y / cellSize_));

        float bestT = 1.0f;
        BodyId bestBody = kInvalidBody;
        Vec2 bestNormal{0, 1};
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const auto it = grid_.find(GridKey{x, y});
                if (it == grid_.end()) continue;
                for (BodyId id : it->second) {
                    if (id == b.id) continue;
                    const Body* other = body(id);
                    if (!other || !other->enabled || !shouldCollide(b, *other, false)) continue;
                    const float otherBound =
                        (std::fabs(other->angularVelocity) * kDegToRad * other->boundingRadius +
                         other->linearVelocity.length()) *
                        dt;
                    const float t = timeOfImpact(b, b.previousPosition, b.previousRotation,
                                                b.position, b.rotationDeg, *other, bound + otherBound,
                                                speculativeMargin);
                    if (t < bestT) {
                        bestT = t;
                        bestBody = id;
                        const Vec2 d = other->position - b.position;
                        bestNormal = d.lengthSq() > 1e-8f ? d.normalized() : Vec2{0, 1};
                    }
                }
            }
        }
        if (bestBody == kInvalidBody) continue;

        // Rewind to the instant of contact and take the approaching speed off,
        // so the ordinary contact solve finds a body resting on the surface
        // rather than one still trying to cover the rest of its step.
        b.position = b.previousPosition + motion * bestT;
        b.rotationDeg = b.previousRotation + (b.rotationDeg - b.previousRotation) * bestT;
        const Body* other = body(bestBody);
        Vec2 normal = bestNormal;
        const Vec2 toOther = other->position - b.position;
        if (toOther.lengthSq() > 1e-8f) normal = toOther.normalized();
        const float vn = b.linearVelocity.dot(normal);
        if (vn < 0.0f) b.linearVelocity -= normal * vn;
        b.angularVelocity = 0.0f;
    }
}

void World::solvePositionConstraints() {
    // Spread the Baumgarte factor across the iterations, so the total
    // correction per step is [baumgarte] regardless of how many iterations
    // run. Applying the full factor every iteration overshoots the separation
    // and pumps energy into the stack.
    const int iters = std::max(1, positionIterations);
    const float factor = baumgarte / static_cast<float>(iters);
    for (int iter = 0; iter < iters; ++iter) {
        for (const Contact& c : contacts_) {
            Body* a = body(c.a);
            Body* b = body(c.b);
            if (!a || !b) continue;
            applyContact(*a, *b, c, factor);
        }
    }
    refreshAllBoundsAndAabbs();
}

// --- queries ---------------------------------------------------------------

namespace {

/// Ray vs convex polygon (slab clipping in the polygon's local frame).
bool rayPolygon(const ConvexPolygon& poly, const Vec2& pos, float rotDeg,
                const Vec2& origin, const Vec2& dir, float maxDist,
                float& outT, Vec2& outNormal) {
    if (poly.vertices.empty()) return false;
    const float r = degToRad(rotDeg), c = std::cos(r), s = std::sin(r);
    const Vec2 localOrigin{c * (origin.x - pos.x) + s * (origin.y - pos.y),
                           -s * (origin.x - pos.x) + c * (origin.y - pos.y)};
    const Vec2 localDir{c * dir.x + s * dir.y, -s * dir.x + c * dir.y};

    float tMin = 0.0f, tMax = maxDist;
    Vec2 normal{0, 0};
    for (size_t i = 0; i < poly.vertices.size(); ++i) {
        const Vec2& v0 = poly.vertices[i];
        const Vec2& v1 = poly.vertices[(i + 1) % poly.vertices.size()];
        Vec2 edge{v1.x - v0.x, v1.y - v0.y};
        if (edge.lengthSq() < 1e-12f) continue;
        edge = edge.normalized();
        const Vec2 n{edge.y, -edge.x};
        const float dist = (localOrigin - v0).dot(n);
        const float denom = localDir.dot(n);
        if (std::fabs(denom) < 1e-8f) {
            if (dist > 0.0f) return false;  // parallel and outside
            continue;
        }
        const float t = -dist / denom;
        if (denom < 0.0f) {
            if (t > tMin) { tMin = t; normal = n; }   // entering face
        } else {
            tMax = std::min(tMax, t);
        }
        if (tMin > tMax) return false;
    }
    outT = tMin;
    outNormal = {c * normal.x - s * normal.y, s * normal.x + c * normal.y};
    return tMin >= 0.0f && tMin <= maxDist;
}

bool rayCircle(const Vec2& center, float radius, const Vec2& origin, const Vec2& dir,
               float maxDist, float& outT, Vec2& outNormal) {
    const Vec2 m = origin - center;
    const float b = m.dot(dir);
    const float c = m.lengthSq() - radius * radius;
    if (c > 0.0f && b > 0.0f) return false;          // pointing away, outside
    const float disc = b * b - c;
    if (disc < 0.0f) return false;                    // no intersection
    float t = -b - std::sqrt(disc);
    if (t < 0.0f) t = 0.0f;                           // origin inside
    if (t > maxDist) return false;
    outT = t;
    outNormal = (origin + dir * t - center).normalized();
    return true;
}

}  // namespace

std::vector<QueryHit> World::raycastAll(const Vec2& origin, const Vec2& direction,
                                        float maxDistance, int collisionMask) const {
    std::vector<QueryHit> out;
    if (direction.lengthSq() < 1e-12f || maxDistance <= 0.0f) return out;
    const Vec2 dir = direction.normalized();

    // Walk the ray through the grid so the query stays near-linear.
    const int steps = static_cast<int>(std::ceil(maxDistance / cellSize_)) + 1;
    for (int s = 0; s <= steps; ++s) {
        const Vec2 p = origin + dir * (static_cast<float>(s) * cellSize_);
        const int cx = static_cast<int>(std::floor(p.x / cellSize_));
        const int cy = static_cast<int>(std::floor(p.y / cellSize_));
        const auto it = grid_.find(GridKey{cx, cy});
        if (it == grid_.end()) continue;
        for (BodyId id : it->second) {
            const Body* b = body(id);
            if (!b || !b->enabled) continue;
            // Same category test the contact filter uses: the query mask names
            // the categories it is willing to see.
            if ((collisionMask & (1 << b->collisionGroup)) == 0) continue;
            if (b->frozen && b->type == BodyType::Static) continue;
            float t = 0.0f;
            Vec2 n{0, 0};
            bool got = false;
            if (b->shape == ColliderShape::Circle) {
                got = rayCircle(b->position, b->radius, origin, dir, maxDistance, t, n);
            } else {
                got = rayPolygon(b->polygon, b->position, b->rotationDeg, origin, dir,
                                 maxDistance, t, n);
            }
            if (!got) continue;
            QueryHit h;
            h.body = id;
            h.point = origin + dir * t;
            h.distance = t;
            h.normal = n;
            h.userData = b->userData;
            h.tag = b->tag;
            out.push_back(std::move(h));
        }
    }
    // Dedupe (a body can appear in several cells) and sort by distance.
    std::sort(out.begin(), out.end(), [](const QueryHit& a, const QueryHit& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        return a.body < b.body;
    });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const QueryHit& a, const QueryHit& b) { return a.body == b.body; }),
              out.end());
    return out;
}

RayHit World::raycast(const Vec2& origin, const Vec2& direction, float maxDistance,
                       int collisionMask) const {
    const auto all = raycastAll(origin, direction, maxDistance, collisionMask);
    RayHit out;
    if (all.empty()) return out;
    out.hit = true;
    out.body = all.front().body;
    out.point = all.front().point;
    out.normal = all.front().normal;
    out.distance = all.front().distance;
    out.userData = all.front().userData;
    out.tag = all.front().tag;
    return out;
}

std::vector<OverlapHit> World::overlapAabb(const Aabb2& box) const {
    std::vector<OverlapHit> out;
    const int x0 = static_cast<int>(std::floor(box.min.x / cellSize_));
    const int x1 = static_cast<int>(std::floor(box.max.x / cellSize_));
    const int y0 = static_cast<int>(std::floor(box.min.y / cellSize_));
    const int y1 = static_cast<int>(std::floor(box.max.y / cellSize_));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const auto it = grid_.find(GridKey{x, y});
            if (it == grid_.end()) continue;
            for (BodyId id : it->second) {
                const Body* b = body(id);
                if (!b || !b->enabled) continue;
                if (!b->aabb.intersects(box)) continue;
                OverlapHit h;
                h.body = id;
                h.userData = b->userData;
                h.tag = b->tag;
                out.push_back(std::move(h));
            }
        }
    }
    std::sort(out.begin(), out.end(),
              [](const OverlapHit& a, const OverlapHit& b) { return a.body < b.body; });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const OverlapHit& a, const OverlapHit& b) { return a.body == b.body; }),
              out.end());
    return out;
}

std::vector<OverlapHit> World::overlapPoint(const Vec2& point) const {
    return overlapAabb(Aabb2(point, point));
}

std::vector<OverlapHit> World::overlapCircle(const Vec2& center, float radius) const {
    auto candidates = overlapAabb(Aabb2::fromCenterSize(center, Vec2(radius * 2, radius * 2)));
    std::vector<OverlapHit> out;
    for (OverlapHit h : candidates) {
        const Body* b = body(h.body);
        if (!b) continue;
        if (b->shape == ColliderShape::Circle) {
            if (b->position.distanceTo(center) > b->radius + radius) continue;
        } else {
            // Refine the AABB hit with a real polygon test.
            Contact c;
            Body probe;
            probe.shape = ColliderShape::Circle;
            probe.position = center;
            probe.radius = radius;
            probe.id = 0;
            Vec2 probeCircle, probePoly;
            if (!collidePolygonCircle(*b, probe, c, probeCircle, probePoly, /*flip=*/true, 0.0f)) {
                continue;
            }
        }
        out.push_back(std::move(h));
    }
    return out;
}

std::vector<BodyId> World::overlapsForSensor(BodyId sensor) const {
    const auto it = sensorOverlaps_.find(sensor);
    return it == sensorOverlaps_.end() ? std::vector<BodyId>{} : it->second;
}

WorldStats World::stats() const { return stats_; }

}  // namespace physics2d
}  // namespace nova
