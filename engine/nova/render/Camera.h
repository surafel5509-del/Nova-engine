// Cameras: the eye the scene is viewed through.
//
// One type covers both cases because both are "a transform plus a projection",
// and keeping them together is what lets the editor swap a 2D scene between an
// orthographic and a perspective view without the renderer knowing.
#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "math/Math.h"

namespace nova {
namespace render {

/// How the world maps onto the viewport.
enum class ProjectionMode {
    /// Parallel projection. The default for 2D, where a pixel of zoom should
    /// look the same at the back of the scene as at the front.
    Orthographic,
    /// Vanishing-point projection.
    Perspective,
};

/// A viewpoint: where it sits, what it looks at, and how the world foreshortens.
///
/// The camera is a value type. Systems copy it, mutate the copy and hand it to
/// the renderer, which keeps rendering free of hidden shared state.
struct Camera {
    // --- placement ----------------------------------------------------------
    // Defaults look down -z, the same direction Mat4::perspective projects, so
    // a 2D scene and a 3D one start out agreeing on which way is forward.
    Vec3 position{0, 0, 10};
    Vec3 target{0, 0, 0};
    Vec3 up{0, 1, 0};

    // --- projection ---------------------------------------------------------
    ProjectionMode mode = ProjectionMode::Orthographic;
    /// Vertical field of view in degrees. Only used in perspective mode.
    float fovYDeg = 60.0f;
    /// Viewport width divided by height. Drives the horizontal frustum.
    float aspect = 1.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    /// Half-height of the visible area in world units. Only used in ortho mode.
    float orthoSize = 10.0f;

    // --- 2D behaviour -------------------------------------------------------
    /// Rotation about the view axis, in degrees. Applied after the look-at.
    float rollDeg = 0.0f;
    /// Extra scale on top of [orthoSize]; larger means "zoomed in".
    float zoom = 1.0f;

    // --- shading hints carried alongside the view ---------------------------
    Color clearColor{0.05f, 0.06f, 0.09f, 1.0f};

    Vec3 forward() const;
    Vec3 right() const;
    Vec3 realUp() const;

    /// World -> view. lookAt with the roll folded in.
    Mat4 view() const;
    /// view() * projection().
    Mat4 viewProjection() const;
    Mat4 projection() const;

    /// World -> screen pixels. Returns false when the point is behind the eye,
    /// where the projection is meaningless. [viewportW]/[viewportH] are the
    /// framebuffer dimensions, [originX]/[originY] the viewport's top-left.
    bool worldToScreen(const Vec3& p, int viewportW, int viewportH, float& outX,
                      float& outY) const;
    bool worldToScreen(const Vec3& p, int viewportW, int viewportH, float& outX, float& outY,
                       float& outDepth) const;
    /// Screen pixels -> a ray from the eye. The near/far components say how far
    /// along that ray to travel to reach the near and far planes.
    void screenRay(float x, float y, int viewportW, int viewportH, Vec3& origin,
                   Vec3& direction) const;

    // --- convenience --------------------------------------------------------
    /// Frame a bounding box: sets position, target and orthoSize/zoom so the
    /// whole box is visible. Aspect-correct, and never gets the box edge-on.
    void frameBox(const Aabb3& box, float padding = 1.1f);

    /// Point the camera at [world] from a fixed spherical offset. Yaw is
    /// degrees around +Y, pitch is degrees above the horizon.
    void orbitAround(const Vec3& world, float distance, float yawDeg, float pitchDeg);

    std::string toString() const;
};

/// The six planes of a view frustum, as inward-facing normals plus distances.
/// Planes are extracted from a view-projection matrix so any projection works.
struct Frustum {
    // nx*x + ny*y + nz*z + d >= 0 means "inside".
    Vec4 planes[6];

    void setFromViewProjection(const Mat4& vp);
    bool containsPoint(const Vec3& p) const;
    /// Conservative: reports intersection even when the sphere only straddles a
    /// plane, which is the behaviour a renderer wants for culling.
    bool intersectsSphere(const Vec3& center, float radius) const;
    bool intersectsAabb(const Aabb3& box) const;
};

}  // namespace render
}  // namespace nova