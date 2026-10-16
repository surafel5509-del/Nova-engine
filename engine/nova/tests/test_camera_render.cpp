// Camera, mesh, renderer and input tests.
#include "tests/TestHarness.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "input/Input.h"
#include "render/Camera.h"
#include "render/Mesh.h"
#include "render/Renderer.h"

using namespace nova;
using namespace nova::render;

namespace {

Camera lookingAt(const Vec3& eye, const Vec3& target) {
    Camera c;
    c.mode = ProjectionMode::Perspective;
    c.position = eye;
    c.target = target;
    c.aspect = 1.0f;
    c.fovYDeg = 60.0f;
    return c;
}

/// A framebuffer plus a renderer already aimed at it.
struct Scene {
    Framebuffer fb;
    Renderer3D r3;
    Scene(int w, int h) {
        fb.resize(w, h);
        fb.clear(Color{0, 0, 0, 1}, 1.0f);
    }
};

Color px(const Framebuffer& fb, int x, int y) { return fb.color.get(x, y); }

}  // namespace

// --- Camera -----------------------------------------------------------------

NOVA_TEST(Camera, ForwardRightAndUpAreOrthonormal) {
    Camera c = lookingAt({3, 4, 5}, {0, 0, 0});
    const Vec3 f = c.forward(), r = c.right(), u = c.realUp();
    CHECK_NEAR(f.length(), 1.0f, 0.001);
    CHECK_NEAR(f.dot(r), 0.0f, 0.001);
    CHECK_NEAR(f.dot(u), 0.0f, 0.001);
    CHECK_NEAR(r.dot(u), 0.0f, 0.001);
    CHECK_NEAR(u.cross(r).dot(f), 1.0f, 0.001);
    // The camera looks from the eye toward the target.
    CHECK(f.dot((Vec3{0, 0, 0} - Vec3{3, 4, 5})) > 0.0f);
}

NOVA_TEST(Camera, DegenerateLookAtFallsBackInsteadOfProducingNaN) {
    // Straight down the up vector: lookAt's cross product is zero.
    Camera c;
    c.position = Vec3{0, 0, 0};
    c.target = Vec3{0, 10, 0};
    c.up = Vec3{0, 1, 0};
    const Vec3 f = c.forward();
    CHECK(std::isfinite(f.x) && std::isfinite(f.y) && std::isfinite(f.z));
    CHECK_NEAR(f.length(), 1.0f, 0.001);
    const Vec3 r = c.right();
    CHECK(std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z));
    CHECK_NEAR(r.length(), 1.0f, 0.001);

    // Position equal to target: no direction at all.
    Camera z;
    z.position = Vec3{1, 2, 3};
    z.target = z.position;
    CHECK_NEAR(z.forward().length(), 1.0f, 0.001);
    const Mat4 v = z.view();
    for (int i = 0; i < 16; ++i) CHECK(std::isfinite(v.m[i]));
}

NOVA_TEST(Camera, RollRotatesTheUpVectorAboutTheViewAxis) {
    Camera a = lookingAt({0, 0, 5}, {0, 0, 0});
    Camera b = lookingAt({0, 0, 5}, {0, 0, 0});
    b.rollDeg = 90.0f;
    // Rolling 90 degrees about -Z swaps the screen's right and up.
    CHECK_NEAR(a.right().dot(b.realUp()), 1.0f, 0.01);
    CHECK_NEAR(a.realUp().dot(b.realUp()), 0.0f, 0.01);
    // The forward axis is unchanged by a roll.
    CHECK_NEAR(a.forward().dot(b.forward()), 1.0f, 0.001);
}

NOVA_TEST(Camera, OrthographicWorldToScreenMapsOneToOne) {
    Camera c;
    c.mode = ProjectionMode::Orthographic;
    c.position = Vec3{0, 0, 0};
    c.target = Vec3{0, 0, -1};  // looking down -z, the engine's convention
    c.aspect = 1.0f;
    c.orthoSize = 5.0f;  // half-height 5 world units over a 64-pixel frame

    float x = 0, y = 0;
    CHECK(c.worldToScreen(Vec3{0, 0, 0}, 64, 64, x, y));
    CHECK_NEAR(x, 32.0f, 0.5f);
    CHECK_NEAR(y, 32.0f, 0.5f);

    // Half the ortho height is 5 units, which is half the viewport.
    CHECK(c.worldToScreen(Vec3{5, 0, 0}, 64, 64, x, y));
    CHECK_NEAR(x, 64.0f, 0.5f);
    CHECK(c.worldToScreen(Vec3{0, 5, 0}, 64, 64, x, y));
    CHECK_NEAR(y, 0.0f, 0.5f);

    // Round trip through the whole frame.
    for (float px = 0.0f; px <= 64.0f; px += 8.0f) {
        for (float py = 0.0f; py <= 64.0f; py += 8.0f) {
            CHECK(c.worldToScreen(Vec3{0, 0, 0}, 64, 64, x, y));
            Vec3 origin, dir;
            c.screenRay(px, py, 64, 64, origin, dir);
            // The ray's origin has to move across the screen, not sit on the
            // eye: an orthographic camera has parallel rays.
            float bx = 0, by = 0;
            CHECK(c.worldToScreen(origin + dir * 1.0f, 64, 64, bx, by));
            CHECK_NEAR(bx, px, 0.5f);
            CHECK_NEAR(by, py, 0.5f);
        }
    }
}

NOVA_TEST(Camera, PerspectiveDepthGrowsWithDistance) {
    Camera c = lookingAt({0, 0, 0}, {0, 0, -1});
    float x, y, d1 = 0, d2 = 0, d3 = 0;
    CHECK(c.worldToScreen(Vec3{0, 0, -1}, 64, 64, x, y, d1));
    CHECK(c.worldToScreen(Vec3{0, 0, -5}, 64, 64, x, y, d2));
    CHECK(c.worldToScreen(Vec3{0, 0, -50}, 64, 64, x, y, d3));
    CHECK(d1 < d2);
    CHECK(d2 < d3);
    // Everything is still centred on screen; only depth changes.
    CHECK_NEAR(x, 32.0f, 0.5f);
    CHECK_NEAR(y, 32.0f, 0.5f);

    // Behind the eye: no screen position at all.
    float bx = 0, by = 0;
    CHECK(!c.worldToScreen(Vec3{0, 0, 1}, 64, 64, bx, by));
}

NOVA_TEST(Camera, PerspectiveScreenRayPointsAtTheClickedPixel) {
    Camera c = lookingAt({2, 3, 7}, {0, 0, 0});
    for (float sx = 1.0f; sx < 64.0f; sx += 15.5f) {
        for (float sy = 1.0f; sy < 64.0f; sy += 15.5f) {
            Vec3 origin, dir;
            c.screenRay(sx, sy, 64, 64, origin, dir);
            // The ray starts just in front of the eye and heads forward.
            CHECK((origin - c.position).dot(c.forward()) > 0.0f);
            CHECK((origin - c.position).length() < 1.0f);
            CHECK(dir.dot(c.forward()) > 0.3f);
            // Whatever it hits on the target plane must project back to the
            // pixel we started from.
            const float denom = dir.dot(c.forward());
            if (std::fabs(denom) < 1e-4f) continue;
            const float t = c.forward().dot(c.target - origin) / denom;
            if (t <= 0.0f) continue;
            float bx = 0, by = 0;
            CHECK(c.worldToScreen(origin + dir * t, 64, 64, bx, by));
            CHECK_NEAR(bx, sx, 1.0f);
            CHECK_NEAR(by, sy, 1.0f);
        }
    }
}

NOVA_TEST(Camera, FrameBoxPutsEverythingOnScreen) {
    Camera c;
    c.mode = ProjectionMode::Perspective;
    c.fovYDeg = 60.0f;
    c.aspect = 1.5f;
    const Aabb3 box{{-5, -2, -3}, {7, 4, 9}};
    c.frameBox(box, 1.2f);

    // All eight corners must land inside the viewport.
    int inside = 0;
    for (int i = 0; i < 8; ++i) {
        const Vec3 p{(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                     (i & 4) ? box.max.z : box.min.z};
        float x = 0, y = 0;
        CHECK(c.worldToScreen(p, 120, 80, x, y));
        if (x >= -1.0f && x <= 121.0f && y >= -1.0f && y <= 81.0f) ++inside;
    }
    CHECK_MSG(inside == 8, "inside=" + std::to_string(inside));
    CHECK_NEAR((c.position - c.target).length() > 0.0f ? 1.0f : 0.0f, 1.0f, 0.001);

    // An invalid box is ignored rather than producing a NaN camera.
    Camera d;
    d.frameBox(Aabb3{});
    CHECK(std::isfinite(d.position.x));
}

NOVA_TEST(Camera, OrbitKeepsDistanceAndAvoidsThePole) {
    Camera c;
    const Vec3 target{1, 2, 3};
    c.orbitAround(target, 10.0f, 45.0f, 30.0f);
    CHECK_NEAR((c.position - target).length(), 10.0f, 0.01);
    CHECK_NEAR(c.target.x, 1.0f, 0.001);

    // Straight up is degenerate for lookAt, so the pitch is clamped short of it.
    c.orbitAround(target, 10.0f, 0.0f, 90.0f);
    CHECK_NEAR((c.position - target).length(), 10.0f, 0.01);
    CHECK(std::isfinite(c.view().m[12]));
    const Vec3 r = c.right();
    CHECK_NEAR(r.length(), 1.0f, 0.001);
}

NOVA_TEST(Camera, ZoomScalesTheOrthoExtent) {
    Camera c;
    c.mode = ProjectionMode::Orthographic;
    c.position = Vec3{0, 0, 0};
    c.target = Vec3{0, 0, -1};
    c.orthoSize = 10.0f;
    float x = 0, y = 0;
    c.worldToScreen(Vec3{2, 0, 0}, 64, 64, x, y);
    const float wide = x;
    // Half-height 10 over 64 pixels, so world x = 2 is a fifth of the way out.
    CHECK_NEAR(wide, 32.0f + 0.2f * 32.0f, 0.5f);
    c.zoom = 2.0f;
    c.worldToScreen(Vec3{2, 0, 0}, 64, 64, x, y);
    // Doubling the zoom halves the visible width, so a fixed world point moves
    // twice as far from the centre.
    CHECK(x > wide);
    CHECK_NEAR(x, 32.0f + (wide - 32.0f) * 2.0f, 1.0f);

    // A zero zoom must not divide by zero.
    c.zoom = 0.0f;
    c.worldToScreen(Vec3{1, 0, 0}, 64, 64, x, y);
    CHECK(std::isfinite(x));
}

NOVA_TEST(Frustum, CullsWhatIsBehindAndBehindTheCamera) {
    Camera c = lookingAt({0, 0, 10}, {0, 0, 0});
    Frustum f;
    f.setFromViewProjection(c.viewProjection());

    CHECK(f.containsPoint(c.position + c.forward() * 5.0f));
    CHECK(!f.containsPoint(c.position + c.forward() * 1e6f));
    CHECK(!f.containsPoint(c.position - c.forward() * 5.0f));

    // A big sphere at the camera intersects; a far one does not.
    CHECK(f.intersectsSphere(c.position + c.forward() * 5.0f, 0.5f));
    CHECK(!f.intersectsSphere(c.position + c.forward() * 5000.0f, 0.5f));
    // A sphere that straddles a plane counts as visible: culling must not pop.
    CHECK(f.intersectsSphere(c.position + c.forward() * 5.0f, 100.0f));

    const Aabb3 near{{-1, -1, 8}, {1, 1, 10}};
    const Aabb3 far{{-1, -1, 9000}, {1, 1, 9002}};
    CHECK(f.intersectsAabb(near));
    CHECK(!f.intersectsAabb(far));
    CHECK(!f.intersectsAabb(Aabb3{{1, 1, 1}, {0, 0, 0}}));  // min > max: invalid
}

NOVA_TEST(Frustum, CoversTheWholeViewportForOrthoAndPerspective) {
    Camera ortho;
    ortho.mode = ProjectionMode::Orthographic;
    ortho.aspect = 1.0f;
    ortho.orthoSize = 10.0f;
    Frustum f;
    f.setFromViewProjection(ortho.viewProjection());
    // Every pixel of a 64x64 frame, unprojected, must be inside.
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            Vec3 o, d;
            ortho.screenRay(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 64, 64, o,
                            d);
            CHECK_MSG(f.containsPoint(o), "pixel " + std::to_string(x) + "," + std::to_string(y));
        }
    }

    Camera persp = lookingAt({0, 0, 10}, {0, 0, 0});
    persp.aspect = 16.0f / 9.0f;
    f.setFromViewProjection(persp.viewProjection());
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            Vec3 o, d;
            persp.screenRay(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 64, 64, o,
                            d);
            CHECK(f.containsPoint(o));
        }
    }
}

// --- Mesh -------------------------------------------------------------------

NOVA_TEST(Mesh, BoxHasCorrectBoundsAndFaceNormals) {
    const Mesh m = makeBox({2, 4, 6});
    const Aabb3 b = m.bounds();
    CHECK(b.valid());
    CHECK_NEAR(b.min.x, -1.0f, 0.001);
    CHECK_NEAR(b.max.y, 2.0f, 0.001);
    CHECK_NEAR(b.max.z, 3.0f, 0.001);
    CHECK_NEAR(b.size().y, 4.0f, 0.001);
    // Six faces of two triangles each.
    CHECK_MSG(m.triangleCount() == 12, "tris=" + std::to_string(m.triangleCount()));

    // Every normal is a unit axis, and each of the six directions appears.
    int seen[6] = {0, 0, 0, 0, 0, 0};
    for (const MeshVertex& v : m.vertices) {
        CHECK_NEAR(v.normal.length(), 1.0f, 0.001);
        const int axis = (std::fabs(std::fabs(v.normal.x) - 1.0f) < 1e-4f)   ? 0
                         : (std::fabs(std::fabs(v.normal.y) - 1.0f) < 1e-4f) ? 1
                                                                             : 2;
        const float sign = (v.normal[axis] > 0.0f) ? 1.0f : 0.0f;
        seen[axis * 2 + static_cast<int>(sign)] = 1;
    }
    for (int i = 0; i < 6; ++i) CHECK_MSG(seen[i] == 1, "missing normal " + std::to_string(i));
}

NOVA_TEST(Mesh, BoxWindingIsConsistentAndCounterClockwiseFromOutside) {
    const Mesh m = makeBox({1, 1, 1});
    // For a closed, outward-facing mesh every triangle's geometric normal must
    // agree with its vertex normals.
    int agreeing = 0, total = 0;
    for (size_t t = 0; t < m.triangleCount(); ++t) {
        const MeshVertex& a = m.vertices[m.indices[t * 3]];
        const MeshVertex& b = m.vertices[m.indices[t * 3 + 1]];
        const MeshVertex& c = m.vertices[m.indices[t * 3 + 2]];
        const Vec3 face = (b.position - a.position).cross(c.position - a.position).normalized();
        ++total;
        if (face.dot(a.normal) > 0.5f) ++agreeing;
    }
    CHECK_MSG(agreeing == total, "agreeing=" + std::to_string(agreeing) + "/" +
                                     std::to_string(total));
}

NOVA_TEST(Mesh, FlipWindingReversesEveryTriangle) {
    Mesh m = makeQuad(2.0f);
    const Vec3 before = (m.vertices[m.indices[1]].position - m.vertices[m.indices[0]].position)
                            .cross(m.vertices[m.indices[2]].position -
                                   m.vertices[m.indices[0]].position);
    m.flipWinding();
    const Vec3 after = (m.vertices[m.indices[1]].position - m.vertices[m.indices[0]].position)
                           .cross(m.vertices[m.indices[2]].position -
                                  m.vertices[m.indices[0]].position);
    CHECK(before.dot(after) < 0.0f);
    CHECK_NEAR(before.length(), after.length(), 1e-4f);
    // Normals flip too, so shading stays consistent with the geometry.
    CHECK_NEAR(m.vertices[0].normal.z, -1.0f, 0.001);
}

NOVA_TEST(Mesh, SphereIsRoundAndCentred) {
    const Mesh m = makeSphere(2.0f, 24, 16);
    CHECK(m.triangleCount() > 24 * 16);
    const Aabb3 b = m.bounds();
    CHECK_NEAR(b.center().length(), 0.0f, 0.02f);
    for (int i = 0; i < 3; ++i) {
        CHECK_NEAR(b.min[i], -2.0f, 0.03f);
        CHECK_NEAR(b.max[i], 2.0f, 0.03f);
    }
    // On a sphere every normal points away from the centre.
    for (const MeshVertex& v : m.vertices) {
        CHECK_NEAR(v.normal.length(), 1.0f, 0.001);
        CHECK(v.position.normalized().dot(v.normal) > 0.99f);
    }
}

NOVA_TEST(Mesh, RecomputeNormalsMatchesSurfaceGeometry) {
    Mesh m = makePlane(2.0f, 2.0f, 2, 2);
    // Every vertex of a flat plane starts out exactly vertical.
    for (const MeshVertex& v : m.vertices) CHECK_NEAR(v.normal.y, 1.0f, 0.001);

    // Push one interior corner up and let recompute find the crease. The
    // interior vertices average their adjacent faces, so only they move.
    m.vertices[1].position.y = 0.8f;
    m.recomputeNormals();
    for (const MeshVertex& v : m.vertices) CHECK_NEAR(v.normal.length(), 1.0f, 0.001);
    CHECK_MSG(m.vertices[1].normal.y < 0.999f, "y=" + std::to_string(m.vertices[1].normal.y));
    CHECK(std::fabs(m.vertices[1].normal.x) > 1e-3f);
    // On average the surface still points up: one bumped corner tilts a few
    // normals, it does not turn the plane inside out.
    Vec3 sum{0, 0, 0};
    for (const MeshVertex& v : m.vertices) sum += v.normal;
    CHECK_MSG(sum.y > 0.0f, "sum.y=" + std::to_string(sum.y));

    // A degenerate mesh must not divide by zero.
    Mesh flat;
    flat.vertices.resize(3);
    flat.indices = {0, 1, 2};
    flat.recomputeNormals();
    for (const MeshVertex& v : flat.vertices) CHECK_NEAR(v.normal.length(), 1.0f, 0.001);
}

NOVA_TEST(Mesh, BuilderAppliesTransformStack) {
    MeshBuilder b;
    b.pushTransform(Mat4::translation({10, 0, 0}));
    b.addVertex(MeshVertex{{0, 0, 0}, {0, 1, 0}, {0, 0}, {1, 1, 1, 1}});
    b.popTransform();
    b.pushTransform(Mat4::scaling({2, 2, 2}));
    b.addVertex(MeshVertex{{1, 0, 0}, {0, 1, 0}, {0, 0}, {1, 1, 1, 1}});
    b.popTransform();
    const Mesh m = b.build();
    CHECK_NEAR(m.vertices[0].position.x, 10.0f, 0.001);
    CHECK_NEAR(m.vertices[1].position.x, 2.0f, 0.001);
    // Uniform scale keeps normals unit length.
    CHECK_NEAR(m.vertices[1].normal.length(), 1.0f, 0.001);
}

NOVA_TEST(Mesh, OutOfRangeIndicesAreIgnoredNotDereferenced) {
    Mesh m;
    m.vertices.resize(2);
    m.indices = {0, 1, 99, 0, 99, 1};
    m.recomputeNormals();
    for (const MeshVertex& v : m.vertices) CHECK(std::isfinite(v.normal.x));
    CHECK(!m.empty());

    Mesh empty;
    CHECK(empty.empty());
    // No vertices means no bounds; the sentinel is reported as invalid.
    CHECK(empty.bounds().min.x == 0.0f);
    CHECK(empty.boundsWireframe(Color{1, 1, 1, 1}).empty());
}

NOVA_TEST(Mesh, ConeAndGridAreWellFormed) {
    const Mesh cone = makeCone(1.0f, 2.0f, 12);
    CHECK(cone.triangleCount() >= 24);
    const Aabb3 b = cone.bounds();
    CHECK_NEAR(b.min.y, 0.0f, 0.001);
    CHECK_NEAR(b.max.y, 2.0f, 0.001);
    for (const MeshVertex& v : cone.vertices) CHECK_NEAR(v.normal.length(), 1.0f, 0.001);

    const Mesh grid = makeGrid(10.0f, 4);
    CHECK(grid.triangleCount() == 4 * 4 * 2);
    CHECK_NEAR(grid.bounds().min.x, -5.0f, 0.001);
    CHECK_NEAR(grid.bounds().max.x, 5.0f, 0.001);

    const Mesh plane = makePlane(3.0f, 5.0f, 2, 3, true);
    CHECK(plane.triangleCount() == 2 * 2 * 3 * 2);
}

// --- Lighting ---------------------------------------------------------------

NOVA_TEST(Lighting, SurfaceFacingTheLightIsBrighterThanTheOneFacingAway) {
    Lighting l;
    const Vec3 towards = -l.lightDirection.normalized();
    const Color lit = l.shade(towards, Color{1, 1, 1, 1});
    const Color dark = l.shade(-towards, Color{1, 1, 1, 1});
    CHECK(lit.r > dark.r);
    CHECK(lit.r > l.ambient.r);

    // A zero-length light direction means "no directional light": ambient only,
    // never a division by zero.
    Lighting none;
    none.lightDirection = Vec3{0, 0, 0};
    const Color amb = none.shade({0, 1, 0}, Color{1, 1, 1, 1});
    CHECK(std::isfinite(amb.r));
    CHECK_NEAR(amb.r, none.ambient.r, 0.001);

    Lighting off;
    off.enabled = false;
    const Color raw = off.shade({0, 1, 0}, Color{0.4f, 0.5f, 0.6f, 1.0f});
    CHECK_NEAR(raw.r, 0.4f, 0.001);
    CHECK_NEAR(raw.a, 1.0f, 0.001);
}

// --- Renderer3D -------------------------------------------------------------

NOVA_TEST(Renderer3D, DrawsACubeInFrontOfTheCamera) {
    Scene s(64, 64);
    Camera cam = lookingAt({0, 0, 5}, {0, 0, 0});
    cam.clearColor = Color{0, 0, 0, 1};
    RenderSettings settings;
    settings.cull = CullMode::Back;
    settings.lighting.enabled = false;

    const Mesh cube = makeBox({2, 2, 2});
    DrawItem item;
    item.mesh = &cube;
    item.color = Color{1, 0, 0, 1};
    s.r3.render(s.fb, cam, settings, {item});

    CHECK(s.r3.stats().itemsSubmitted == 1);
    CHECK(s.r3.stats().itemsDrawn == 1);
    CHECK(s.r3.stats().itemsCulled == 0);
    // The near face of the cube fills the middle of the frame.
    CHECK(px(s.fb, 32, 32).r > 0.5f);
    // The corners are background.
    CHECK_NEAR(px(s.fb, 1, 1).r, 0.0f, 0.01);
}

NOVA_TEST(Renderer3D, DepthOrdersOverlappingCubes) {
    Scene s(64, 64);
    Camera cam = lookingAt({0, 0, 8}, {0, 0, 0});
    RenderSettings settings;
    settings.lighting.enabled = false;

    // The eye is at z = 8, so +z is the nearer side of the scene.
    const Mesh cube = makeBox({2, 2, 2});
    std::vector<DrawItem> items;
    DrawItem far;
    far.mesh = &cube;
    far.color = Color{0, 0, 1, 1};
    far.model = Mat4::translation({0, 0, -3});
    DrawItem near;
    near.mesh = &cube;
    near.color = Color{1, 0, 0, 1};
    near.model = Mat4::translation({0, 0, 3});
    // Submit the far one first: the depth test, not the order, has to decide.
    items.push_back(far);
    items.push_back(near);
    s.r3.render(s.fb, cam, settings, items);

    CHECK(px(s.fb, 32, 32).r > 0.5f);
    CHECK_NEAR(px(s.fb, 32, 32).b, 0.0f, 0.01);
}

NOVA_TEST(Renderer3D, CullsWhatIsBehindTheCamera) {
    Scene s(64, 64);
    Camera cam = lookingAt({0, 0, 10}, {0, 0, 0});
    cam.clearColor = Color{0, 0, 0, 1};
    RenderSettings settings;
    settings.lighting.enabled = false;

    const Mesh cube = makeBox({1, 1, 1});
    std::vector<DrawItem> items;
    DrawItem behind;
    behind.mesh = &cube;
    behind.model = Mat4::translation({0, 0, 50});
    items.push_back(behind);
    s.r3.render(s.fb, cam, settings, items);

    CHECK(s.r3.stats().itemsSubmitted == 1);
    CHECK(s.r3.stats().itemsDrawn == 0);
    CHECK(s.r3.stats().itemsCulled == 1);
    CHECK_NEAR(px(s.fb, 32, 32).r, 0.0f, 0.01);
}

NOVA_TEST(Renderer3D, LightingChangesWhatIsActuallyDrawn) {
    Scene s(64, 64);
    Camera cam = lookingAt({0, 0, 5}, {0, 0, 0});
    RenderSettings lit;
    lit.lighting.enabled = true;
    lit.lighting.ambient = Color{0, 0, 0, 1};
    lit.lighting.intensity = 1.0f;
    lit.lighting.wrap = 0.0f;

    const Mesh cube = makeBox({2, 2, 2});
    DrawItem item;
    item.mesh = &cube;
    item.color = Color{1, 1, 1, 1};
    s.r3.render(s.fb, cam, lit, {item});
    const float shaded = px(s.fb, 32, 32).r;

    RenderSettings flat = lit;
    flat.lighting.enabled = false;
    s.r3.render(s.fb, cam, flat, {item});
    const float unlit = px(s.fb, 32, 32).r;

    CHECK_MSG(unlit > shaded, "shaded=" + std::to_string(shaded) + " unlit=" +
                                   std::to_string(unlit));
    CHECK_NEAR(unlit, 1.0f, 0.02);
}

NOVA_TEST(Renderer3D, RejectsNullAndEmptySubmissions) {
    Scene s(32, 32);
    Camera cam = lookingAt({0, 0, 5}, {0, 0, 0});
    RenderSettings settings;
    DrawItem nothing;
    nothing.mesh = nullptr;
    const Mesh empty;
    DrawItem emptyItem;
    emptyItem.mesh = &empty;
    s.r3.render(s.fb, cam, settings, {nothing, emptyItem});
    CHECK(s.r3.stats().itemsSubmitted == 0);

    // A framebuffer that was never sized must not crash.
    Framebuffer bad;
    DrawItem real;
    real.mesh = &empty;
    s.r3.render(bad, cam, settings, {real});
    CHECK(s.r3.stats().itemsSubmitted == 0);
}

NOVA_TEST(Renderer3D, SubmittedBoundsCoverEveryItem) {
    Scene s(64, 64);
    Camera cam = lookingAt({0, 0, 20}, {0, 0, 0});
    RenderSettings settings;
    settings.lighting.enabled = false;
    const Mesh cube = makeBox({2, 2, 2});
    std::vector<DrawItem> items;
    DrawItem a;
    a.mesh = &cube;
    a.model = Mat4::translation({0, 0, 0});
    DrawItem b;
    b.mesh = &cube;
    b.model = Mat4::translation({10, 0, 0});
    items.push_back(a);
    items.push_back(b);
    s.r3.render(s.fb, cam, settings, items);
    const Aabb3 all = s.r3.submittedBounds();
    CHECK(all.valid());
    CHECK_NEAR(all.min.x, -1.0f, 0.01);
    CHECK_NEAR(all.max.x, 11.0f, 0.01);
}

NOVA_TEST(Renderer3D, BoundsOverlayDrawnWithoutCrashing) {
    Scene s(32, 32);
    Camera cam = lookingAt({0, 0, 6}, {0, 0, 0});
    RenderSettings settings;
    settings.drawBounds = true;
    settings.lighting.enabled = false;
    const Mesh cube = makeBox({2, 2, 2});
    DrawItem item;
    item.mesh = &cube;
    s.r3.render(s.fb, cam, settings, {item});
    CHECK(s.r3.stats().itemsDrawn == 1);
}

// --- Renderer2D -------------------------------------------------------------

NOVA_TEST(Renderer2D, SortKeyOverridesSubmissionOrder) {
    Framebuffer fb;
    fb.resize(32, 32);
    Renderer2D r;
    Camera cam;
    cam.mode = ProjectionMode::Orthographic;
    cam.orthoSize = 16.0f;
    RenderSettings settings;

    // A higher sort key is nearer the viewer, so it draws later.
    SpriteItem back;
    back.center = {0, 0};
    back.size = {20, 20};
    back.color = Color{0, 0, 1, 1};
    back.sortKey = 1.0f;
    SpriteItem front;
    front.center = {0, 0};
    front.size = {20, 20};
    front.color = Color{1, 0, 0, 1};
    front.sortKey = 10.0f;

    // Submitted back-to-front on purpose; the sort key must fix it.
    r.render(fb, cam, settings, {back, front});
    CHECK(px(fb, 16, 16).r > 0.5f);

    SpriteItem overTop;
    overTop.center = {0, 0};
    overTop.size = {20, 20};
    overTop.color = Color{0, 1, 0, 1};
    overTop.sortKey = 100.0f;
    r.render(fb, cam, settings, {back, front, overTop});
    CHECK(px(fb, 16, 16).g > 0.5f);
}

NOVA_TEST(Renderer2D, EqualSortKeysKeepSubmissionOrder) {
    Framebuffer fb;
    fb.resize(32, 32);
    Renderer2D r;
    Camera cam;
    cam.orthoSize = 16.0f;
    RenderSettings settings;
    SpriteItem first;
    first.size = {20, 20};
    first.color = Color{1, 0, 0, 1};
    first.sortKey = 5.0f;
    SpriteItem second = first;
    second.color = Color{0, 1, 0, 1};
    r.render(fb, cam, settings, {first, second});
    CHECK(px(fb, 16, 16).g > 0.5f);
    r.render(fb, cam, settings, {second, first});
    CHECK(px(fb, 16, 16).r > 0.5f);
}

NOVA_TEST(Renderer2D, CameraMovesTheView) {
    Framebuffer fb;
    fb.resize(64, 64);
    Renderer2D r;
    Camera cam;
    cam.mode = ProjectionMode::Orthographic;
    cam.orthoSize = 10.0f;
    cam.clearColor = Color{0, 0, 0, 1};
    RenderSettings settings;
    SpriteItem s;
    s.center = {0, 0};
    s.size = {2, 2};
    s.color = Color{1, 1, 1, 1};

    r.render(fb, cam, settings, {s});
    CHECK(px(fb, 32, 32).r > 0.5f);

    // Move the sprite to the right by 5 world units; with a half-height of 10
    // over 64 pixels that is 16 pixels.
    SpriteItem moved = s;
    moved.center = {5, 0};
    r.render(fb, cam, settings, {moved});
    CHECK(px(fb, 48, 32).r > 0.5f);
    CHECK_NEAR(px(fb, 32, 32).r, 0.0f, 0.01);
}

NOVA_TEST(Renderer2D, EmptyFrameIsJustAClear) {
    Framebuffer fb;
    fb.resize(16, 16);
    Renderer2D r;
    Camera cam;
    cam.clearColor = Color{0.25f, 0.5f, 0.75f, 1.0f};
    RenderSettings settings;
    r.render(fb, cam, settings, {});
    CHECK_NEAR(px(fb, 8, 8).r, 0.25f, 0.01);
    CHECK_NEAR(px(fb, 8, 8).b, 0.75f, 0.01);
    CHECK(r.stats().itemsDrawn == 0);
}

NOVA_TEST(Renderer, DrawLineSegmentsProducesVisiblePixels) {
    Framebuffer fb;
    fb.resize(32, 32);
    Camera cam = lookingAt({0, 0, 5}, {0, 0, 0});
    cam.clearColor = Color{0, 0, 0, 1};
    std::vector<MeshVertex> segs;
    segs.push_back(MeshVertex{{-1, 0, 0}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}});
    segs.push_back(MeshVertex{{1, 0, 0}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1}});
    drawLineSegments(fb, cam, segs, Color{1, 1, 0, 1});
    int lit = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            if (px(fb, x, y).g > 0.2f) ++lit;
        }
    }
    // One pixel tall, and about as wide as the segment is on screen.
    CHECK_MSG(lit >= 10, "lit=" + std::to_string(lit));

    // Degenerate input must not crash or draw.
    Framebuffer fb2;
    fb2.resize(8, 8);
    drawLineSegments(fb2, cam, {}, Color{1, 1, 1, 1});
    std::vector<MeshVertex> odd = {segs[0]};
    drawLineSegments(fb2, cam, odd, Color{1, 1, 1, 1});
    // A segment entirely behind the camera is skipped.
    segs[1].position = {1, 0, 50};
    drawLineSegments(fb2, cam, segs, Color{1, 1, 1, 1});
}