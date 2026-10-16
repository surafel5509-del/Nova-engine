// Nova Engine — 2D rigid body physics.
//
// Model
// -----
// * Semi-implicit Euler integration at a fixed 60 Hz step.
// * Broadphase: a uniform spatial hash, so cost scales with the number of
//   *nearby* pairs rather than pairs in the world.
// * Narrowphase: SAT for OBB-vs-OBB and OBB-vs-circle, producing both a
//   contact point and a normal.
// * Solver: sequential impulses with restitution, friction and Baumgarte
//   position correction. Restitution is applied from the pre-solve relative
//   velocity only, so overlapping bodies never gain energy ("explosions").
//
// Everything runs on explicit f32/f64 math with no hidden allocations in the
// inner loop, and the RNG is caller-supplied so a replay is bit-identical.
#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "math/Math.h"

namespace nova {
namespace physics2d {

using BodyId = uint32_t;
constexpr BodyId kInvalidBody = 0xFFFFFFFFu;

enum class BodyType : uint8_t { Static, Kinematic, Dynamic };

enum class ColliderShape : uint8_t { Box, Circle, Capsule };

/// A convex polygon, used for boxes and for arbitrary convex hulls.
/// Winding must be counter-clockwise; the constructor fixes it for you.
struct ConvexPolygon {
    std::vector<Vec2> vertices;

    ConvexPolygon() = default;
    explicit ConvexPolygon(std::vector<Vec2> v) : vertices(std::move(v)) { fixWinding(); }

    /// Axis-aligned box centred on the origin.
    static ConvexPolygon box(float halfWidth, float halfHeight) {
        return ConvexPolygon({{-halfWidth, -halfHeight}, {halfWidth, -halfHeight},
                              {halfWidth, halfHeight}, {-halfWidth, halfHeight}});
    }
    /// Oriented box: half extents rotated by [degrees] about the origin.
    static ConvexPolygon orientedBox(float halfWidth, float halfHeight, float degrees) {
        const float r = degToRad(degrees), c = std::cos(r), s = std::sin(r);
        auto rot = [&](float x, float y) { return Vec2{x * c - y * s, x * s + y * c}; };
        return ConvexPolygon({rot(-halfWidth, -halfHeight), rot(halfWidth, -halfHeight),
                              rot(halfWidth, halfHeight), rot(-halfWidth, halfHeight)});
    }
    /// Regular n-gon, handy for tests and for round-ish gameplay shapes.
    static ConvexPolygon regular(float radius, int sides, float rotationDeg = 0.0f);

    void fixWinding();
    void computeCentroid();
    Aabb2 bounds() const;
    Vec2 centroid{0, 0};

private:
    void computeSignedArea(double& area) const;
};

struct MassData {
    float mass = 1.0f;
    float invMass = 1.0f;
    /// Rotational inertia about the centroid (kg * m^2).
    float inertia = 1.0f;
    float invInertia = 1.0f;
};

struct Body {
    BodyId id = kInvalidBody;
    BodyType type = BodyType::Dynamic;
    std::string tag;
    int userData = 0;  ///< free integer for game code (team, index, ...)

    // --- state ---
    Vec2 position{0, 0};
    Vec2 linearVelocity{0, 0};
    float rotationDeg = 0.0f;
    float angularVelocity = 0.0f;  ///< degrees / second

    // --- forces, cleared every step ---
    Vec2 force{0, 0};
    float torque = 0.0f;

    // --- material ---
    float friction = 0.3f;
    float restitution = 0.2f;
    float linearDamping = 0.0f;
    float angularDamping = 0.0f;
    /// Bodies on the same non-colliding group never collide (tilemap layers).
    int collisionGroup = 0;
    int collisionMask = ~0;
    /// Always sweep this body against the world instead of relying on discrete
    /// contacts. Only worth it for genuinely fast things (bullets); ordinary
    /// bodies are detected automatically once they travel more than their own
    /// size in a single sub-step.
    bool bullet = false;
    /// Disables simulation but keeps the collider for queries.
    bool frozen = false;
    bool sensor = false;  ///< detects overlap, never applies a response
    bool enabled = true;

    ColliderShape shape = ColliderShape::Box;
    ConvexPolygon polygon;  ///< Box and Capsule use this
    float radius = 0.5f;    ///< Circle and Capsule

    MassData mass;
    Vec2 previousPosition{0, 0};
    float previousRotation = 0.0f;
    /// Radius of the smallest circle around the centre that contains the whole
    /// shape. Continuous collision needs it to bound how fast the surface can
    /// move, and the broadphase needs it for spheres.
    float boundingRadius = 0.5f;

    // --- sleep bookkeeping ---
    bool sleeping = false;
    float sleepTimer = 0.0f;

    // --- cached each step by the broadphase ---
    Aabb2 aabb;
    int islandId = -1;
    uint32_t frameTouched = 0;
};

/// A face-face collision yields up to two contact points. One point cannot
/// resist torque, which is why single-point stacks shear apart sideways; two
/// points (the clip result) let a box rest flat on another box.
constexpr int kMaxManifoldPoints = 2;

/// Result of a narrowphase test, before it is turned into [Contact]s.
struct Manifold {
    Vec2 normal{0, 1};
    int count = 0;
    Vec2 points[kMaxManifoldPoints];        ///< world contact point (on the incident face)
    Vec2 faceA[kMaxManifoldPoints];         ///< matching point on A's surface
    Vec2 faceB[kMaxManifoldPoints];         ///< matching point on B's surface
    float separations[kMaxManifoldPoints];  ///< signed gap, negative = penetrating
    /// Identifies which features produced each point, for warm-start matching.
    uint32_t featureIds[kMaxManifoldPoints] = {0, 0};
};

struct Contact {
    BodyId a = kInvalidBody;
    BodyId b = kInvalidBody;
    uint32_t featureId = 0; ///< matches points across frames for warm starting
    /// Index of this contact's manifold inside the world's contact array, and
    /// how many points that manifold has. The velocity solver needs the whole
    /// manifold to solve the two corners as a coupled 2x2 system: solved one at
    /// a time a box cannot feel that loading one corner also lifts the other,
    /// which is what makes tall stacks sag and topple.
    int manifoldStart = 0;
    int manifoldCount = 1;
    Vec2 point{0, 0};      ///< world-space contact point
    Vec2 normal{0, 1};     ///< from A towards B
    float penetration = 0.0f;
    /// Accumulated normal and tangent impulses, kept for warm starting.
    float normalImpulse = 0.0f;
    float tangentImpulse = 0.0f;
    /// Approach speed captured when the contact was first made, and the normal
    /// effective mass. Restitution is applied as a separate pass at the end of
    /// the solve using these, so bouncing never fights the push-out.
    float approachVelocity = 0.0f;
    float normalMass = 0.0f;
    /// True when the shapes actually overlapped when this contact was made.
    /// A contact created inside the speculative margin is not touching yet and
    /// must not bounce: its "approach velocity" is just how fast it is flying
    /// towards a gap it has not reached.
    bool touching = false;

    // --- anchors, captured when the contact is created ---
    // The position solver replays these every iteration instead of reusing
    // [penetration], which goes stale as soon as the bodies move. Re-deriving
    // the separation from the anchors is what keeps the iteration count from
    // changing the result.
    Vec2 anchorA{0, 0};    ///< point on A's surface, relative to A's centre
    Vec2 anchorB{0, 0};    ///< point on B's surface, relative to B's centre
    Vec2 posA{0, 0};       ///< A's centre when the contact was created
    Vec2 posB{0, 0};       ///< B's centre when the contact was created
    float separation = 0.0f;   ///< signed gap at creation (negative = overlap)
    /// Offset that makes the anchor-difference formula reproduce [separation].
    float adjustedSeparation = 0.0f;
};

struct ContactListener {
    std::function<void(const Body& a, const Body& b, const Contact& c)> begin;
    std::function<void(const Body& a, const Body& b, const Contact& c)> end;
    std::function<void(const Body& a, const Body& b, const Contact& c)> solve;
    /// Broadphase overlap, reported before narrowphase.
    std::function<void(const Body& a, const Body& b)> broadphase;
};

struct RayHit {
    bool hit = false;
    BodyId body = kInvalidBody;
    Vec2 point{0, 0};
    Vec2 normal{0, 1};
    float distance = 0.0f;
    int userData = 0;
    std::string tag;
};

struct QueryHit {
    BodyId body = kInvalidBody;
    Vec2 point{0, 0};
    Vec2 normal{0, 1};   ///< surface normal at the hit
    float distance = 0.0f;
    int userData = 0;
    std::string tag;
};

/// Result of a point / shape overlap query.
struct OverlapHit {
    BodyId body = kInvalidBody;
    int userData = 0;
    std::string tag;
};

struct WorldStats {
    int bodies = 0;
    int activeBodies = 0;
    int broadphasePairs = 0;
    int contacts = 0;
    int islands = 0;
    double stepMs = 0.0;
};

class World {
public:
    explicit World(Vec2 gravity = {0.0f, -9.81f});

    Vec2 gravity{0.0f, -9.81f};
    /// 0 = no positional correction (bodies may sink), 1 = instant (jittery).
    /// Stiffness of the contact constraint, in Hz. Contacts behave like springs
    /// rather than infinitely rigid walls, which is what lets a tall stack stay
    /// put with a handful of solver iterations instead of dozens. Set to 0 for
    /// the old rigid (Baumgarte) behaviour.
    float contactHertz = 60.0f;
    /// Damping of that spring. 10 is critically-ish damped and, crucially, bleeds
    /// off the energy the push-out adds each step.
    float contactDampingRatio = 10.0f;
    float baumgarte = 0.2f;       ///< only used when [contactHertz] is 0
    float slop = 0.005f;          ///< allowed overlap, in world units
    /// Sub-steps per [step]. The frame is integrated and solved in this many
    /// equal slices. Sub-stepping is what lets the contact constraint stay
    /// stiff: the soft-contact frequency is capped at a quarter of the rate the
    /// integrator can represent, so a single 1/60 s step tops out at 15 Hz and
    /// a stack visibly squashes. Four 1/240 s sub-steps lift that to 60 Hz and
    /// the same stack stands rigid.
    int subSteps = 4;
    int velocityIterations = 4;
    /// Velocity iterations run with the push-out bias switched off, straight
    /// after positions have been integrated. The bias drives overlapping bodies
    /// apart by adding real velocity; without taking it back out again the
    /// bodies keep coasting after the overlap is gone and a resting stack
    /// trampolines itself apart.
    int relaxIterations = 2;
    int positionIterations = 2;
    /// How far apart two shapes may be and still get a contact. A contact in
    /// this gap is *speculative*: the solver lets the bodies approach exactly
    /// fast enough to close it and no faster, so a body crossing a thin wall
    /// in a single step is caught instead of tunnelling through it. Set to 0
    /// for plain touching-contact behaviour.
    float speculativeMargin = 0.1f;
    /// Sweep fast bodies against their neighbours so they cannot pass through
    /// each other between two narrowphase runs. Costs a broadphase query and a
    /// short advancement loop per fast body per sub-step.
    bool enableContinuous = true;
    /// Fraction of last frame's impulse replayed to warm start the solve.
    /// Rolling resistance, as a multiple of the torque gravity would need to
    /// roll the body at its own radius. Zero (the default) means a ball that
    /// arrives rolling coasts forever, which is exact but not what anyone
    /// wants on a floor: real surfaces deform and eat that energy. Setting it
    /// to ~0.3 lets a crate stack come to rest in a second or two instead of
    /// drifting. Applied only to dynamic bodies with a touching contact, and
    /// only as a torque, so the friction constraint still converts the lost
    /// spin into the matching loss of speed.
    float rollingFriction = 0.0f;

    float warmStartScale = 0.9f;
    bool allowSleep = true;
    float sleepLinearThreshold = 0.02f;
    float sleepAngularThreshold = 0.5f;
    float sleepTimeRequired = 0.5f;

    // --- body management ---
    BodyId createBody(const Body& body);
    Body* body(BodyId id);
    const Body* body(BodyId id) const;
    void destroyBody(BodyId id);
    void clear();
    size_t bodyCount() const { return bodies_.size(); }
    std::vector<BodyId> allBodies() const;
    /// First body whose tag matches, or kInvalidBody.
    BodyId findByTag(const std::string& tag) const;
    std::vector<BodyId> findAllByTag(const std::string& tag) const;

    // --- forces ---
    void applyForce(BodyId id, const Vec2& force);
    void applyForceAtPoint(BodyId id, const Vec2& force, const Vec2& worldPoint);
    void applyImpulse(BodyId id, const Vec2& impulse);
    void applyImpulseAtPoint(BodyId id, const Vec2& impulse, const Vec2& worldPoint);
    void applyTorque(BodyId id, float torque);
    /// Sets velocity directly, for teleports and character controllers.
    void setVelocity(BodyId id, const Vec2& v, float angularDegPerSec = 0.0f);

    // --- mass ---
    void setMass(BodyId id, float mass);
    void setStatic(BodyId id, bool isStatic);
    /// Recomputes the centre of mass and inertia from the collider.
    void updateMassFromShape(BodyId id);
    /// Rescales velocities so the change in mass is momentum-preserving.
    void applyMassChange(BodyId id, float newMass);

    // --- simulation ---
    void step(float dt);
    /// Advances by exactly [steps] fixed sub-steps (used by the fixed stepper).
    void stepFixed(float dt, int steps);
    void setContactListener(ContactListener listener) { listener_ = std::move(listener); }

    // --- queries ---
    /// Closest hit along a ray, honouring collision groups.
    RayHit raycast(const Vec2& origin, const Vec2& direction, float maxDistance,
                   int collisionMask = ~0) const;
    /// Every hit along a ray, ordered by distance.
    std::vector<QueryHit> raycastAll(const Vec2& origin, const Vec2& direction,
                                     float maxDistance, int collisionMask = ~0) const;
    /// Bodies whose AABB contains the point.
    std::vector<OverlapHit> overlapPoint(const Vec2& point) const;
    /// Bodies whose AABB intersects the box.
    std::vector<OverlapHit> overlapAabb(const Aabb2& box) const;
    /// Bodies in the circle (broadphase AABB refined by an exact test).
    std::vector<OverlapHit> overlapCircle(const Vec2& center, float radius) const;

    const std::vector<Contact>& contacts() const { return contacts_; }
    WorldStats stats() const;

    /// Bodies currently touching a sensor, keyed by sensor id.
    std::vector<BodyId> overlapsForSensor(BodyId sensor) const;

private:
    void integrateForces(float dt);
    void integratePositions(float dt);
    void broadphase();
    void narrowphase();
    /// [warmStart] replays last frame's converged impulses first. It is true
    /// only for the first sub-step of a frame: the impulses were solved for a
    /// whole frame of time, so replaying them once per sub-step would push the
    /// contact out several times over and leave the solver correcting a
    /// position it invented.
    void solveVelocityConstraints(float dt, int iterations, bool useBias, bool warmStart);
    /// Restitution pass, run last so the relax pass cannot cancel a bounce.
    void applyRestitution();
    void solvePositionConstraints();
    void updateSleepState(float dt);
    void applyRollingResistance(float dt);
    void refreshBounds(Body& b) const;
    void refreshAllBoundsAndAabbs();
    /// Layer/type filter shared by the narrowphase, queries and the continuous
    /// sweep. [requireOverlap] is off for CCD, where the two bounding boxes have
    /// deliberately been left apart by the motion being swept.
    bool shouldCollide(const Body& a, const Body& b, bool requireOverlap = true) const;
    static bool collidePolygonPolygonLegacy(const Body& a, const Body& b, Contact& out);
    static bool collideManifold(const Body& a, const Body& b, Manifold& out, float margin);
    /// Face clipping for convex polygons; returns false when disjoint.
    static bool clipPolygons(const Body& a, const Body& b, Manifold& out, float margin);
public:
private:
    static bool collideCircleCircle(const Body& a, const Body& b, Contact& out, float margin);
    /// [out] carries the A->B normal, the penetration and, in [circlePoint] /
    /// [polyPoint], the matching point on *each* surface. The position solver
    /// needs both: reusing a single point for both anchors measures the gap
    /// between the body centres instead of the gap between their surfaces.
    static bool collidePolygonCircle(const Body& poly, const Body& circ, Contact& out,
                                     Vec2& circlePoint, Vec2& polyPoint,
                                     bool flip, float margin);
    static bool collidePolygonPolygon(const Body& a, const Body& b, Contact& out);
    void applyContact(Body& a, Body& b, const Contact& c, float factor);
    /// Sweeps fast bodies against their neighbours and rewinds them to the
    /// moment of first contact. See [timeOfImpact].
    void solveContinuous(float dt);
    /// Fraction of the step at which two shapes first touch, by conservative
    /// advancement. [travelBound] is how far the surfaces could possibly move
    /// relative to each other over the whole step, in world units. Returns 1
    /// for "never touches during this step".
    static float timeOfImpact(const Body& a, const Vec2& posA0, float rotA0, const Vec2& posA1,
                              float rotA1, const Body& b, float travelBound, float margin);

    std::vector<Body> bodies_;
    std::unordered_map<BodyId, size_t> index_;
    BodyId nextId_ = 1;
    std::vector<Contact> contacts_;
    /// Indices of the first contact of each manifold inside [contacts_].
    std::vector<size_t> manifoldStart_;
    /// Scratch buffer for the per-island sleep decision, reused every step.
    std::vector<char> sleepCandidate_;
    /// Broadphase output, consumed by the narrowphase.
    std::vector<std::pair<BodyId, BodyId>> candidatePairs_;
    std::vector<Contact> persistent_;
    /// Sensor pairs seen last frame, so begin/end report real transitions
    /// instead of re-firing every frame the overlap lasts.
    std::vector<std::pair<BodyId, BodyId>> persistentSensors_;
    /// Overlap pairs this frame, published by narrowphase for the listener.
    std::vector<std::pair<BodyId, BodyId>> sensorPairs_;
  public:
  private:
    std::unordered_map<BodyId, std::vector<BodyId>> sensorOverlaps_;
    ContactListener listener_;
    WorldStats stats_;
    uint32_t frame_ = 0;

    // Uniform spatial hash broadphase.
    struct GridKey {
        int32_t x, y;
        bool operator==(const GridKey& o) const { return x == o.x && y == o.y; }
    };
    struct GridHash {
        size_t operator()(const GridKey& k) const {
            return static_cast<size_t>(static_cast<uint32_t>(k.x) * 73856093u ^
                                       static_cast<uint32_t>(k.y) * 19349663u);
        }
    };
    std::unordered_map<GridKey, std::vector<BodyId>, GridHash> grid_;
    float cellSize_ = 2.0f;
};

/// Separating Axis Test for two convex polygons.
/// Writes the minimum translation vector into [out] and returns false when the
/// polygons are disjoint.
bool satTest(const ConvexPolygon& a, const Vec2& posA, float rotA,
             const ConvexPolygon& b, const Vec2& posB, float rotB,
             Vec2& outNormal, float& outPenetration, Vec2& outPoint);

}  // namespace physics2d
}  // namespace nova
