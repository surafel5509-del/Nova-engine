#pragma once

#include <cmath>

namespace nova {

/** Column-major 4x4 matrix, compatible with OpenGL uniform upload. */
struct Mat4 {
    float m[16];

    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    static Mat4 ortho(float left, float right, float bottom, float top, float near, float far) {
        Mat4 r = identity();
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[10] = -2.0f / (far - near);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[14] = -(far + near) / (far - near);
        return r;
    }

    static Mat4 translation(float x, float y, float z) {
        Mat4 r = identity();
        r.m[12] = x;
        r.m[13] = y;
        r.m[14] = z;
        return r;
    }

    static Mat4 scaling(float x, float y, float z) {
        Mat4 r = identity();
        r.m[0] = x;
        r.m[5] = y;
        r.m[10] = z;
        return r;
    }

    /** Returns a * b (apply b first, then a). */
    static Mat4 multiply(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += a.m[k * 4 + row] * b.m[col * 4 + k];
                }
                r.m[col * 4 + row] = sum;
            }
        }
        return r;
    }

    /** Rotation about the X axis (degrees). */
    static Mat4 rotationX(float deg) {
        const float rad = deg * 3.14159265f / 180.0f;
        Mat4 r = identity();
        r.m[5] = std::cos(rad);
        r.m[6] = std::sin(rad);
        r.m[9] = -std::sin(rad);
        r.m[10] = std::cos(rad);
        return r;
    }

    /** Rotation about the Y axis (degrees). */
    static Mat4 rotationY(float deg) {
        const float rad = deg * 3.14159265f / 180.0f;
        Mat4 r = identity();
        r.m[0] = std::cos(rad);
        r.m[2] = -std::sin(rad);
        r.m[8] = std::sin(rad);
        r.m[10] = std::cos(rad);
        return r;
    }

    /** Rotation about the Z axis (degrees). */
    static Mat4 rotationZ(float deg) {
        const float rad = deg * 3.14159265f / 180.0f;
        Mat4 r = identity();
        r.m[0] = std::cos(rad);
        r.m[1] = std::sin(rad);
        r.m[4] = -std::sin(rad);
        r.m[5] = std::cos(rad);
        return r;
    }

    /** Perspective projection (vertical fov in degrees). */
    static Mat4 perspective(float fovYDeg, float aspect, float nearZ, float farZ) {
        const float rad = fovYDeg * 3.14159265f / 180.0f;
        const float f = 1.0f / std::tan(rad / 2.0f);
        Mat4 r{};
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (farZ + nearZ) / (nearZ - farZ);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
        return r;
    }

    /** Look-at view matrix (right-handed, GL convention). */
    static Mat4 lookAt(float ex, float ey, float ez,
                       float cx, float cy, float cz,
                       float ux, float uy, float uz) {
        float fx = cx - ex, fy = cy - ey, fz = cz - ez;
        float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (fl < 1e-6f) fl = 1e-6f;
        fx /= fl; fy /= fl; fz /= fl;
        // s = f x u
        float sx = fy * uz - fz * uy;
        float sy = fz * ux - fx * uz;
        float sz = fx * uy - fy * ux;
        float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
        if (sl < 1e-6f) sl = 1e-6f;
        sx /= sl; sy /= sl; sz /= sl;
        // u' = s x f
        const float upx = sy * fz - sz * fy;
        const float upy = sz * fx - sx * fz;
        const float upz = sx * fy - sy * fx;

        Mat4 r = identity();
        r.m[0] = sx;  r.m[4] = sy;  r.m[8] = sz;
        r.m[1] = upx; r.m[5] = upy; r.m[9] = upz;
        r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
        r.m[12] = -(sx * ex + sy * ey + sz * ez);
        r.m[13] = -(upx * ex + upy * ey + upz * ez);
        r.m[14] = (fx * ex + fy * ey + fz * ez);
        return r;
    }

    /** Transforms a 2D point (z=0, w=1). */
    void transformPoint(float x, float y, float& outX, float& outY) const {
        outX = m[0] * x + m[4] * y + m[12];
        outY = m[1] * x + m[5] * y + m[13];
    }

    /** Transforms a 3D point (w=1). */
    void transformPoint3(float x, float y, float z, float& outX, float& outY, float& outZ) const {
        outX = m[0] * x + m[4] * y + m[8] * z + m[12];
        outY = m[1] * x + m[5] * y + m[9] * z + m[13];
        outZ = m[2] * x + m[6] * y + m[10] * z + m[14];
    }
};

} // namespace nova
