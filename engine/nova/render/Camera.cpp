#include "render/Camera.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace nova {
namespace render {

namespace {
/// lookAt's up vector after [rollDeg] of rotation about the view axis.
Vec3 rolledUp(const Vec3& f, const Vec3& u, float rollDeg) {
    if (std::fabs(rollDeg) < 1e-6f) return u;
    const Vec3 axis = f.normalized();
    // Rodrigues rotation of u about the forward axis.
    const float rad = degToRad(rollDeg);
    const float c = std::cos(rad), s = std::sin(rad);
    return u * c + axis.cross(u) * s + axis * (axis.dot(u)) * (1.0f - c);
}
}  // namespace

Vec3 Camera::forward() const {
    const Vec3 d = target - position;
    if (d.lengthSq() < 1e-12f) return Vec3{0, 0, 1};
    return d.normalized();
}

Vec3 Camera::right() const {
    const Vec3 f = forward();
    Vec3 r = f.cross(rolledUp(f, up, rollDeg));
    if (r.lengthSq() < 1e-12f) r = f.anyPerpendicular();
    return r.normalized();
}

Vec3 Camera::realUp() const { return right().cross(forward()).normalized(); }

Mat4 Camera::view() const {
    const Vec3 f = forward();
    return Mat4::lookAt(position, position + f, rolledUp(f, up, rollDeg));
}

Mat4 Camera::projection() const {
    const float z = (std::fabs(zoom) > 1e-6f) ? zoom : 1e-6f;
    if (mode == ProjectionMode::Perspective) {
        return Mat4::perspective(clampf(fovYDeg, 1.0f, 179.0f), aspect > 1e-6f ? aspect : 1e-6f,
                                 std::max(1e-4f, nearPlane), std::max(nearPlane + 1e-3f, farPlane));
    }
    const float halfH = orthoSize / z;
    const float halfW = halfH * (aspect > 1e-6f ? aspect : 1e-6f);
    return Mat4::ortho(-halfW, halfW, -halfH, halfH, -std::max(1e-3f, farPlane),
                       std::max(1e-3f, farPlane));
}

Mat4 Camera::viewProjection() const { return projection() * view(); }

bool Camera::worldToScreen(const Vec3& p, int viewportW, int viewportH, float& outX,
                           float& outY, float& outDepth) const {
    if (viewportW <= 0 || viewportH <= 0) return false;
    const Vec4 clip = viewProjection().project(p);
    // z + w is the near-plane test; dividing by a w that is not positive puts
    // the point behind the eye, where there is no meaningful screen position.
    if (clip.w <= 1e-6f) return false;
    const float iw = 1.0f / clip.w;
    outX = (clip.x * iw * 0.5f + 0.5f) * static_cast<float>(viewportW);
    outY = (0.5f - clip.y * iw * 0.5f) * static_cast<float>(viewportH);
    outDepth = clip.z * iw;
    return true;
}

bool Camera::worldToScreen(const Vec3& p, int viewportW, int viewportH, float& outX,
                           float& outY) const {
    float depth = 0.0f;
    return worldToScreen(p, viewportW, viewportH, outX, outY, depth);
}

void Camera::screenRay(float x, float y, int viewportW, int viewportH, Vec3& origin,
                       Vec3& direction) const {
    const Mat4 inv = viewProjection().inverse();
    const float ndcX = (x / std::max(1, viewportW)) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (y / std::max(1, viewportH)) * 2.0f;

    auto unproject = [&](float z) -> Vec3 {
        const Vec4 clip{ndcX, ndcY, z, 1.0f};
        const Vec3 p{
            inv.m[0] * clip.x + inv.m[4] * clip.y + inv.m[8] * clip.z + inv.m[12],
            inv.m[1] * clip.x + inv.m[5] * clip.y + inv.m[9] * clip.z + inv.m[13],
            inv.m[2] * clip.x + inv.m[6] * clip.y + inv.m[10] * clip.z + inv.m[14],
        };
        // The inverse of a projection is itself projective: the result's own w
        // is not 1, so it has to be divided out. Skipping this puts every
        // unprojected point metres behind the camera.
        const float w = inv.m[3] * clip.x + inv.m[7] * clip.y + inv.m[11] * clip.z + inv.m[15];
        const float iw = (std::fabs(w) > 1e-9f) ? 1.0f / w : 1.0f;
        return p * iw;
    };

    // The ray starts on the near plane, not at the eye. Under an orthographic
    // projection the two differ by a whole screen height across the frame, so
    // anchoring it at the eye would collapse every pixel onto one line.
    const Vec3 a = unproject(-1.0f);
    const Vec3 b = unproject(1.0f);
    const Vec3 d = b - a;
    direction = d.lengthSq() > 1e-12f ? d.normalized() : forward();
    origin = a;
}

void Camera::frameBox(const Aabb3& box, float padding) {
    if (!box.valid()) return;
    const Vec3 c = box.center();
    const Vec3 size = box.size();
    const float radius = std::sqrt(size.lengthSq()) * 0.5f;
    target = c;

    mode = ProjectionMode::Perspective;
    const float pad = padding > 1.0f ? padding : 1.0f;
    // Pull back far enough that a sphere of [radius] fits the vertical fov.
    const float halfFov = degToRad(clampf(fovYDeg, 1.0f, 179.0f)) * 0.5f;
    const float dist = (radius * pad) / std::max(0.02f, std::sin(halfFov));
    const float safeDist = std::max(dist, nearPlane + radius + 0.01f);
    position = c - forward() * safeDist;
    if ((position - c).lengthSq() < 1e-9f) position = c + Vec3{0, 0, -safeDist};
    // Keep the frustum tight enough that the depth buffer still resolves.
    nearPlane = std::max(0.01f, safeDist - radius * 2.0f);
    farPlane = std::max(nearPlane + 0.1f, safeDist + radius * 2.0f);
}

void Camera::orbitAround(const Vec3& world, float distance, float yawDeg, float pitchDeg) {
    const float d = std::max(1e-3f, distance);
    const float yaw = degToRad(yawDeg);
    // Clamp just short of straight up/down: exactly 90 degrees makes the view
    // direction parallel to the up vector and lookAt degenerates.
    const float pitch = degToRad(clampf(pitchDeg, -89.5f, 89.5f));
    target = world;
    position = world + Vec3{d * std::cos(pitch) * std::sin(yaw), d * std::sin(pitch),
                            d * std::cos(pitch) * std::cos(yaw)};
}

std::string Camera::toString() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "Camera pos(%.2f %.2f %.2f) target(%.2f %.2f %.2f) %s fov=%.1f aspect=%.3f "
                  "near=%.3f far=%.1f",
                  position.x, position.y, position.z, target.x, target.y, target.z,
                  mode == ProjectionMode::Perspective ? "persp" : "ortho", fovYDeg, aspect,
                  nearPlane, farPlane);
    return buf;
}

// --- Frustum ----------------------------------------------------------------

void Frustum::setFromViewProjection(const Mat4& vp) {
    // Gribb/Hartmann extraction. Adding the w row picks the plane for +w and
    // subtracting it the plane for -w, so all six come out inward-facing in
    // clip space, where the visible region is |x| <= w, |y| <= w, |z| <= w.
    auto row = [&](int r, float* out) {
        out[0] = vp.m[0 * 4 + r];
        out[1] = vp.m[1 * 4 + r];
        out[2] = vp.m[2 * 4 + r];
        out[3] = vp.m[3 * 4 + r];
    };
    float r0[4], r1[4], r2[4], r3[4];
    row(0, r0);
    row(1, r1);
    row(2, r2);
    row(3, r3);

    struct PlaneSpec {
        const float* rowIn;
        float sign;
    };
    const PlaneSpec spec[6] = {{r0, 1.0f}, {r0, -1.0f}, {r1, 1.0f},
                               {r1, -1.0f}, {r2, 1.0f}, {r2, -1.0f}};
    for (int i = 0; i < 6; ++i) {
        Vec4& p = planes[i];
        p.x = r3[0] + spec[i].sign * spec[i].rowIn[0];
        p.y = r3[1] + spec[i].sign * spec[i].rowIn[1];
        p.z = r3[2] + spec[i].sign * spec[i].rowIn[2];
        p.w = r3[3] + spec[i].sign * spec[i].rowIn[3];
    }

    // Normalising makes "distance to plane" a true metric, which the sphere
    // test below relies on.
    for (Vec4& p : planes) {
        const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (len > 1e-9f) {
            p.x /= len;
            p.y /= len;
            p.z /= len;
            p.w /= len;
        }
    }
}

bool Frustum::containsPoint(const Vec3& p) const {
    for (const Vec4& plane : planes) {
        if (plane.x * p.x + plane.y * p.y + plane.z * p.z + plane.w < 0.0f) return false;
    }
    return true;
}

bool Frustum::intersectsSphere(const Vec3& center, float radius) const {
    const float r = std::fabs(radius);
    for (const Vec4& plane : planes) {
        if (plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w < -r) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersectsAabb(const Aabb3& box) const {
    if (!box.valid()) return false;
    // Test the box's positive vertex: if the furthest point along the plane
    // normal is still behind, the whole box is out.
    for (const Vec4& plane : planes) {
        const Vec3 positive{
            plane.x >= 0.0f ? box.max.x : box.min.x,
            plane.y >= 0.0f ? box.max.y : box.min.y,
            plane.z >= 0.0f ? box.max.z : box.min.z,
        };
        if (plane.x * positive.x + plane.y * positive.y + plane.z * positive.z + plane.w <
            0.0f) {
            return false;
        }
    }
    return true;
}

}  // namespace render
}  // namespace nova