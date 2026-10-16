// Nova Engine — math primitives.
//
// Hand-rolled linear algebra: the engine needs exact control over memory
// layout (SIMD-friendly, POD, no heap) because these types are copied by the
// million per frame in the transform and render paths.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace nova {

// ---------------------------------------------------------------------------
// Scalar helpers
// ---------------------------------------------------------------------------

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;
inline constexpr float kHalfPi = 1.57079632679489661923f;
inline constexpr float kEpsilon = 1e-6f;

inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;
inline float degToRad(float d) { return d * kDegToRad; }
inline float radToDeg(float r) { return r * kRadToDeg; }

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float signf(float v) { return v < 0.0f ? -1.0f : (v > 0.0f ? 1.0f : 0.0f); }

/// Frame-rate independent exponential smoothing.
inline float damp(float a, float b, float lambda, float dt) {
    return lerpf(a, b, 1.0f - std::exp(-lambda * dt));
}

/// Moves [a] toward [b] by at most [maxDelta]. Used for all axis movement.
inline float moveTowards(float a, float b, float maxDelta) {
    const float d = b - a;
    if (std::fabs(d) <= maxDelta) return b;
    return a + signf(d) * maxDelta;
}

/// Shortest signed angular difference in degrees, result in (-180, 180].
inline float angleDeltaDeg(float from, float to) {
    float d = std::fmod(to - from, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

/// Maps [a,b] to [0,1], clamped. Safe when a == b.
inline float inverseLerp(float a, float b, float v) {
    if (std::fabs(b - a) < kEpsilon) return 0.0f;
    return saturate((v - a) / (b - a));
}

inline float smoothstep(float edge0, float edge1, float x) {
    const float t = inverseLerp(edge0, edge1, x);
    return t * t * (3.0f - 2.0f * t);
}

inline bool nearlyEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

inline float wrapAngle(float deg) {
    deg = std::fmod(deg, 360.0f);
    if (deg < 0.0f) deg += 360.0f;
    return deg;
}

// ---------------------------------------------------------------------------
// Vec2
// ---------------------------------------------------------------------------

struct Vec2 {
    float x = 0.0f, y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    explicit constexpr Vec2(float s) : x(s), y(s) {}

    float& operator[](int i) { return i == 0 ? x : y; }
    const float& operator[](int i) const { return i == 0 ? x : y; }

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    Vec2& operator/=(float s) { x /= s; y /= s; return *this; }

    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }

    float lengthSq() const { return x * x + y * y; }
    float length() const { return std::sqrt(x * x + y * y); }
    float distanceTo(const Vec2& o) const { return (*this - o).length(); }
    float distanceSqTo(const Vec2& o) const { return (*this - o).lengthSq(); }
    float dot(const Vec2& o) const { return x * o.x + y * o.y; }
    /// 2D scalar cross (z component of the 3D cross product).
    float cross(const Vec2& o) const { return x * o.y - y * o.x; }

    Vec2 normalized() const {
        const float len = length();
        return len > kEpsilon ? Vec2{x / len, y / len} : Vec2{0.0f, 0.0f};
    }
    void normalize() { *this = normalized(); }

    /// Perpendicular rotated +90 degrees (counter-clockwise in a Y-up space).
    Vec2 perpendicular() const { return {-y, x}; }
    Vec2 rotated(float deg) const {
        const float r = degToRad(deg), c = std::cos(r), s = std::sin(r);
        return {x * c - y * s, x * s + y * c};
    }
    static float distance(const Vec2& a, const Vec2& b) { return a.distanceTo(b); }
    static float dot(const Vec2& a, const Vec2& b) { return a.dot(b); }
    static Vec2 min(const Vec2& a, const Vec2& b) { return {std::fmin(a.x, b.x), std::fmin(a.y, b.y)}; }
    static Vec2 max(const Vec2& a, const Vec2& b) { return {std::fmax(a.x, b.x), std::fmax(a.y, b.y)}; }
    static Vec2 lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }
    /// Clamps the vector length to [maxLength].
    Vec2 clamped(float maxLength) const {
        const float lenSq = lengthSq();
        if (lenSq > maxLength * maxLength && lenSq > kEpsilon) return normalized() * maxLength;
        return *this;
    }
};

inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

// ---------------------------------------------------------------------------
// Vec3
// ---------------------------------------------------------------------------

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit constexpr Vec3(float s) : x(s), y(s), z(s) {}
    constexpr Vec3(const Vec2& v, float z_ = 0.0f) : x(v.x), y(v.y), z(z_) {}

    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    const float& operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

    Vec2 xy() const { return {x, y}; }

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator*=(const Vec3& o) { x *= o.x; y *= o.y; z *= o.z; return *this; }
    Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }

    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vec3& o) const { return !(*this == o); }

    float lengthSq() const { return x * x + y * y + z * z; }
    float length() const { return std::sqrt(lengthSq()); }
    float distanceTo(const Vec3& o) const { return (*this - o).length(); }
    float distanceSqTo(const Vec3& o) const { return (*this - o).lengthSq(); }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    Vec3 normalized() const {
        const float len = length();
        return len > kEpsilon ? Vec3{x / len, y / len, z / len} : Vec3{0.0f, 0.0f, 0.0f};
    }
    void normalize() { *this = normalized(); }
    Vec3 clamped(float maxLength) const {
        const float lenSq = lengthSq();
        if (lenSq > maxLength * maxLength && lenSq > kEpsilon) return normalized() * maxLength;
        return *this;
    }
    /// Any unit vector perpendicular to this one (branchless-ish selection).
    Vec3 anyPerpendicular() const {
        const Vec3 a = (std::fabs(x) < 0.9f) ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        return cross(a).normalized();
    }
    static float dot(const Vec3& a, const Vec3& b) { return a.dot(b); }
    static Vec3 cross(const Vec3& a, const Vec3& b) { return a.cross(b); }
    static Vec3 min(const Vec3& a, const Vec3& b) {
        return {std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z)};
    }
    static Vec3 max(const Vec3& a, const Vec3& b) {
        return {std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z)};
    }
    static Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

// ---------------------------------------------------------------------------
// Vec4
// ---------------------------------------------------------------------------

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(const Vec3& v, float w_ = 0.0f) : x(v.x), y(v.y), z(v.z), w(w_) {}
    constexpr Vec4(const Vec2& v, float z_ = 0.0f, float w_ = 0.0f) : x(v.x), y(v.y), z(z_), w(w_) {}

    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }
    const float& operator[](int i) const { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }

    Vec3 xyz() const { return {x, y, z}; }

    Vec4 operator+(const Vec4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4 operator-(const Vec4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    Vec4 operator*(const Vec4& o) const { return {x * o.x, y * o.y, z * o.z, w * o.w}; }
    Vec4& operator+=(const Vec4& o) { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }
    Vec4& operator*=(float s) { x *= s; y *= s; z *= s; w *= s; return *this; }

    float dot(const Vec4& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
    float length() const { return std::sqrt(dot(*this)); }
    Vec4 normalized() const {
        const float len = length();
        return len > kEpsilon ? (*this) * (1.0f / len) : Vec4{0, 0, 0, 0};
    }
};

inline Vec4 operator*(float s, const Vec4& v) { return v * s; }

// ---------------------------------------------------------------------------
// Quat
// ---------------------------------------------------------------------------

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat identity() { return {0, 0, 0, 1}; }

    /// Euler angles in degrees, applied as yaw(Y) then pitch(X) then roll(Z).
    /// This is the standard game convention: yaw is the world-space turn.
    static Quat fromEuler(float pitchDeg, float yawDeg, float rollDeg) {
        return Quat::fromAngleAxis(yawDeg, Vec3{0, 1, 0}) *
               Quat::fromAngleAxis(pitchDeg, Vec3{1, 0, 0}) *
               Quat::fromAngleAxis(rollDeg, Vec3{0, 0, 1});
    }

    Quat operator*(const Quat& o) const {
        return {
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
            w * o.w - x * o.x - y * o.y - z * o.z,
        };
    }
    Quat operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    Quat operator-() const { return {-x, -y, -z, -w}; }
    Quat& operator*=(const Quat& o) { *this = *this * o; return *this; }
    Quat conjugate() const { return {-x, -y, -z, w}; }
    float norm() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    Quat normalized() const {
        const float n = norm();
        return n > kEpsilon ? Quat{x / n, y / n, z / n, w / n} : identity();
    }
    float dot(const Quat& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }

    Quat inverse() const {
        const float n2 = dot(*this);
        return n2 > kEpsilon ? conjugate() * (1.0f / n2) : identity();
    }

    /// Shortest-arc rotation taking [from] to [to]. Both must be normalized.
    static Quat fromToRotation(const Vec3& from, const Vec3& to) {
        const Vec3 f = from.normalized(), t = to.normalized();
        const float d = f.dot(t);
        if (d > 0.999999f) return identity();
        if (d < -0.999999f) {
            // Antiparallel: pick any perpendicular axis.
            return fromAngleAxis(180.0f, f.anyPerpendicular());
        }
        const Vec3 axis = f.cross(t);
        Quat q{axis.x, axis.y, axis.z, 1.0f + d};
        return q.normalized();
    }

    static Quat fromAngleAxis(float deg, const Vec3& axis) {
        const Vec3 a = axis.normalized();
        const float h = degToRad(deg) * 0.5f;
        const float s = std::sin(h);
        return {a.x * s, a.y * s, a.z * s, std::cos(h)};
    }

    Vec3 rotate(const Vec3& v) const {
        // v + 2*cross(q.xyz, cross(q.xyz, v) + q.w*v)
        const Vec3 u{x, y, z};
        const Vec3 t = u.cross(v) * 2.0f;
        return v + t * w + u.cross(t);
    }

    /// Euler angles in degrees. Exact inverse of [fromEuler] for
    /// q = yaw(Y) * pitch(X) * roll(Z). Defined after Mat3 below.
    void toEuler(float& pitchDeg, float& yawDeg, float& rollDeg) const;

    static Quat slerp(const Quat& a, const Quat& b, float t) {
        Quat q1 = b;
        float d = a.dot(b);
        if (d < 0.0f) { q1 = -b; d = -d; }  // shortest path
        if (d > 0.9995f) {
            // Nearly parallel: lerp + renormalize avoids a sin(0) division.
            return Quat{lerpf(a.x, q1.x, t), lerpf(a.y, q1.y, t),
                        lerpf(a.z, q1.z, t), lerpf(a.w, q1.w, t)}.normalized();
        }
        const float theta0 = std::acos(clampf(d, -1.0f, 1.0f));
        const float theta = theta0 * t;
        const float sinTheta0 = std::sin(theta0);
        const float s0 = std::cos(theta) - d * std::sin(theta) / sinTheta0;
        const float s1 = std::sin(theta) / sinTheta0;
        return Quat{a.x * s0 + q1.x * s1, a.y * s0 + q1.y * s1,
                    a.z * s0 + q1.z * s1, a.w * s0 + q1.w * s1};
    }
};

// ---------------------------------------------------------------------------
// Mat3 — rotation/scale only (transform normals)
// ---------------------------------------------------------------------------

struct Mat3 {
    float m[9];

    constexpr Mat3() : m{1, 0, 0, 0, 1, 0, 0, 0, 1} {}

    static Mat3 identity() { return Mat3{}; }

    /// Column-major: m[col * 3 + row] == R[row][col].
    static Mat3 rotation(const Quat& q) {
        const float x = q.x, y = q.y, z = q.z, w = q.w;
        Mat3 r;
        r.m[0] = 1 - 2 * (y * y + z * z); r.m[1] = 2 * (x * y + z * w);     r.m[2] = 2 * (x * z - y * w);
        r.m[3] = 2 * (x * y - z * w);     r.m[4] = 1 - 2 * (x * x + z * z); r.m[5] = 2 * (y * z + x * w);
        r.m[6] = 2 * (x * z + y * w);     r.m[7] = 2 * (y * z - x * w);     r.m[8] = 1 - 2 * (x * x + y * y);
        return r;
    }

    static Mat3 scale(const Vec3& s) {
        Mat3 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        return r;
    }

    float determinant() const {
        return m[0] * (m[4] * m[8] - m[5] * m[7])
             - m[3] * (m[1] * m[8] - m[2] * m[7])
             + m[6] * (m[1] * m[5] - m[2] * m[4]);
    }

    /// Returns identity when singular, so callers never propagate NaN.
    Mat3 inverse() const {
        const float det = determinant();
        if (std::fabs(det) < 1e-9f) return Mat3::identity();
        const float inv = 1.0f / det;
        Mat3 r;
        r.m[0] = (m[4] * m[8] - m[5] * m[7]) * inv;
        r.m[1] = (m[2] * m[7] - m[1] * m[8]) * inv;
        r.m[2] = (m[1] * m[5] - m[2] * m[4]) * inv;
        r.m[3] = (m[5] * m[6] - m[3] * m[8]) * inv;
        r.m[4] = (m[0] * m[8] - m[2] * m[6]) * inv;
        r.m[5] = (m[2] * m[3] - m[0] * m[5]) * inv;
        r.m[6] = (m[3] * m[7] - m[4] * m[6]) * inv;
        r.m[7] = (m[1] * m[6] - m[0] * m[7]) * inv;
        r.m[8] = (m[0] * m[4] - m[1] * m[3]) * inv;
        return r;
    }

    /// Transpose-inverse, i.e. the correct normal matrix under non-uniform scale.
    Mat3 normalMatrix() const { return inverse().transpose(); }

    Mat3 transpose() const {
        Mat3 r;
        r.m[0] = m[0]; r.m[1] = m[3]; r.m[2] = m[6];
        r.m[3] = m[1]; r.m[4] = m[4]; r.m[5] = m[7];
        r.m[6] = m[2]; r.m[7] = m[5]; r.m[8] = m[8];
        return r;
    }

    /// Column-major storage (m[col * 3 + row] == R[row][col]), so this is a
    /// true M * v: each output component is the dot of a *column* with v.
    Vec3 operator*(const Vec3& v) const {
        return {
            m[0] * v.x + m[3] * v.y + m[6] * v.z,
            m[1] * v.x + m[4] * v.y + m[7] * v.z,
            m[2] * v.x + m[5] * v.y + m[8] * v.z,
        };
    }

    Mat3 operator*(const Mat3& o) const {
        Mat3 r;
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row)
                r.m[c * 3 + row] = m[0 * 3 + row] * o.m[c * 3 + 0]
                                 + m[1 * 3 + row] * o.m[c * 3 + 1]
                                 + m[2 * 3 + row] * o.m[c * 3 + 2];
        return r;
    }
};

// Quat::toEuler needs Mat3, so it is defined here rather than inline in the
// class body.
inline void Quat::toEuler(float& pitchDeg, float& yawDeg, float& rollDeg) const {
    const Mat3 r = Mat3::rotation(*this);
    // Inverse of q = yaw(Y) * pitch(X) * roll(Z); see the test suite.
    pitchDeg = radToDeg(std::asin(clampf(-r.m[7], -1.0f, 1.0f)));  // -R[1][2]
    yawDeg   = radToDeg(std::atan2(r.m[6], r.m[8]));               // R[0][2], R[2][2]
    rollDeg  = radToDeg(std::atan2(r.m[1], r.m[4]));               // R[1][0], R[1][1]
}

// ---------------------------------------------------------------------------
// Mat4 — column-major, OpenGL-compatible upload order
// ---------------------------------------------------------------------------

struct Mat4 {
    float m[16];

    // Default-constructs to identity. The body inlines the identity fill
    // instead of calling identity(), which would recurse forever (identity()
    // constructs a Mat4 through this constructor).
    Mat4() {
        std::memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    static Mat4 identity() { return Mat4(); }

    static Mat4 fromMat3(const Mat3& a, const Vec3& translation = {0, 0, 0}) {
        Mat4 r = identity();
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row)
                r.m[c * 4 + row] = a.m[c * 3 + row];
        r.m[12] = translation.x; r.m[13] = translation.y; r.m[14] = translation.z;
        return r;
    }

    static Mat4 trs(const Vec3& t, const Quat& r, const Vec3& s) {
        Mat3 m = Mat3::rotation(r) * Mat3::scale(s);
        return fromMat3(m, t);
    }

    static Mat4 translation(const Vec3& t) {
        Mat4 r = identity();
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    static Mat4 scaling(const Vec3& s) {
        Mat4 r = identity();
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
        return r;
    }
    static Mat4 rotation(const Quat& q) { return fromMat3(Mat3::rotation(q)); }

    static Mat4 ortho(float l, float r, float b, float t, float n, float f) {
        Mat4 res = identity();
        res.m[0] = 2.0f / (r - l);
        res.m[5] = 2.0f / (t - b);
        res.m[10] = -2.0f / (f - n);
        res.m[12] = -(r + l) / (r - l);
        res.m[13] = -(t + b) / (t - b);
        res.m[14] = -(f + n) / (f - n);
        return res;
    }

    static Mat4 perspective(float fovYDeg, float aspect, float n, float f) {
        const float tanHalf = std::tan(degToRad(fovYDeg) * 0.5f);
        Mat4 res;
        std::memset(res.m, 0, sizeof(res.m));
        res.m[0] = 1.0f / (aspect * tanHalf);
        res.m[5] = 1.0f / tanHalf;
        res.m[10] = (f + n) / (n - f);
        res.m[11] = -1.0f;
        res.m[14] = (2.0f * f * n) / (n - f);
        return res;
    }

    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        const Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        if (s.lengthSq() < kEpsilon) s = f.anyPerpendicular();  // up parallel to forward
        const Vec3 u = s.cross(f);
        Mat4 r = identity();
        r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -s.dot(eye);
        r.m[13] = -u.dot(eye);
        r.m[14] = f.dot(eye);
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                r.m[c * 4 + row] = m[0 * 4 + row] * o.m[c * 4 + 0]
                                 + m[1 * 4 + row] * o.m[c * 4 + 1]
                                 + m[2 * 4 + row] * o.m[c * 4 + 2]
                                 + m[3 * 4 + row] * o.m[c * 4 + 3];
            }
        }
        return r;
    }

    /// Transforms a point (w = 1) and performs the perspective divide.
    Vec3 transformPoint(const Vec3& p) const {
        const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        const float iw = (std::fabs(w) > kEpsilon) ? 1.0f / w : 1.0f;
        return {
            (m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) * iw,
            (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) * iw,
            (m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]) * iw,
        };
    }

    /// Transforms a direction (w = 0): ignores translation.
    Vec3 transformDirection(const Vec3& d) const {
        return {
            m[0] * d.x + m[4] * d.y + m[8] * d.z,
            m[1] * d.x + m[5] * d.y + m[9] * d.z,
            m[2] * d.x + m[6] * d.y + m[10] * d.z,
        };
    }

    /// Project [p] to normalized device coords without the perspective divide.
    Vec4 project(const Vec3& p) const {
        return {
            m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14],
            m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15],
        };
    }

    /// Upper-left 3x3, for normal transformation.
    Mat3 toMat3() const {
        Mat3 r;
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row)
                r.m[c * 3 + row] = m[c * 4 + row];
        return r;
    }

    /// Full 4x4 inverse via the adjugate. Returns identity when the matrix is
    /// singular (det ~ 0) so a degenerate transform degrades to a no-op
    /// instead of spraying NaNs through the scene graph.
    Mat4 inverse() const {
        const float* a = m;  // column-major: a[col * 4 + row]
        // Cofactor expansion, written out longhand for speed and clarity.
        float inv[16];
        inv[0]  =  a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
        inv[4]  = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
        inv[8]  =  a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
        inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
        inv[1]  = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
        inv[5]  =  a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
        inv[9]  = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
        inv[13] =  a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
        inv[2]  =  a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
        inv[6]  = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
        inv[10] =  a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
        inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
        inv[3]  = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
        inv[7]  =  a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
        inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
        inv[15] =  a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];

        const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if (std::fabs(det) < 1e-12f) return identity();
        const float s = 1.0f / det;
        Mat4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * s;
        return r;
    }

    /// Transforms [p] and divides by w (the standard clip -> NDC step).
    Vec3 projectPoint(const Vec3& p) const { return transformPoint(p); }
};

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

/// Axis-aligned bounding box in 2D.
struct Aabb2 {
    Vec2 min{0, 0};
    Vec2 max{0, 0};

    constexpr Aabb2() = default;
    constexpr Aabb2(const Vec2& lo, const Vec2& hi) : min(lo), max(hi) {}

    static Aabb2 fromCenterSize(const Vec2& c, const Vec2& s) {
        const Vec2 h = s * 0.5f;
        return {c - h, c + h};
    }
    static Aabb2 fromPoints(const Vec2* pts, int n) {
        if (n <= 0) return {};
        Vec2 lo = pts[0], hi = pts[0];
        for (int i = 1; i < n; ++i) { lo = Vec2::min(lo, pts[i]); hi = Vec2::max(hi, pts[i]); }
        return {lo, hi};
    }

    Vec2 center() const { return (min + max) * 0.5f; }
    Vec2 size() const { return max - min; }
    float width() const { return max.x - min.x; }
    float height() const { return max.y - min.y; }
    float area() const { return width() * height(); }
    bool valid() const { return max.x >= min.x && max.y >= min.y; }
    bool contains(const Vec2& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }
    bool intersects(const Aabb2& o) const {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y;
    }
    Aabb2 expanded(float m) const { return {min - Vec2{m}, max + Vec2{m}}; }
    Aabb2 merged(const Aabb2& o) const {
        return {Vec2::min(min, o.min), Vec2::max(max, o.max)};
    }
    Vec2 closestPoint(const Vec2& p) const {
        return {clampf(p.x, min.x, max.x), clampf(p.y, min.y, max.y)};
    }
    /// Slab-method ray intersection. Returns false for parallel/outside rays.
    bool raycast(const Vec2& origin, const Vec2& dir, float maxDist, float& outT) const {
        float tMin = 0.0f, tMax = maxDist;
        for (int i = 0; i < 2; ++i) {
            if (std::fabs(dir[i]) < kEpsilon) {
                if (origin[i] < min[i] || origin[i] > max[i]) return false;
                continue;
            }
            const float inv = 1.0f / dir[i];
            float t1 = (min[i] - origin[i]) * inv;
            float t2 = (max[i] - origin[i]) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tMin = std::fmax(tMin, t1);
            tMax = std::fmin(tMax, t2);
            if (tMin > tMax) return false;
        }
        outT = tMin;
        return true;
    }
};

/// Axis-aligned bounding box in 3D.
struct Aabb3 {
    Vec3 min{0, 0, 0};
    Vec3 max{0, 0, 0};

    constexpr Aabb3() = default;
    constexpr Aabb3(const Vec3& lo, const Vec3& hi) : min(lo), max(hi) {}

    static Aabb3 fromCenterSize(const Vec3& c, const Vec3& s) {
        const Vec3 h = s * 0.5f;
        return {c - h, c + h};
    }

    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 size() const { return max - min; }
    bool valid() const { return max.x >= min.x && max.y >= min.y && max.z >= min.z; }
    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }
    bool intersects(const Aabb3& o) const {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y &&
               max.y >= o.min.y && min.z <= o.max.z && max.z >= o.min.z;
    }
    Aabb3 expanded(float m) const { return {min - Vec3{m}, max + Vec3{m}}; }
    Aabb3 merged(const Aabb3& o) const {
        return {Vec3::min(min, o.min), Vec3::max(max, o.max)};
    }
    /// Transforms all 8 corners and re-fits — conservative but correct for
    /// any affine transform including rotation.
    Aabb3 transformed(const Mat4& m) const {
        Vec3 pts[8] = {
            {min.x, min.y, min.z}, {max.x, min.y, min.z},
            {min.x, max.y, min.z}, {max.x, max.y, min.z},
            {min.x, min.y, max.z}, {max.x, min.y, max.z},
            {min.x, max.y, max.z}, {max.x, max.y, max.z},
        };
        Vec3 lo = m.transformPoint(pts[0]), hi = lo;
        for (int i = 1; i < 8; ++i) {
            const Vec3 p = m.transformPoint(pts[i]);
            lo = Vec3::min(lo, p);
            hi = Vec3::max(hi, p);
        }
        return {lo, hi};
    }
};

// ---------------------------------------------------------------------------
// Ray
// ---------------------------------------------------------------------------

struct Ray {
    Vec3 origin{0, 0, 0};
    Vec3 direction{0, 0, -1};  // expected normalized

    Ray() = default;
    Ray(const Vec3& o, const Vec3& d) : origin(o), direction(d.normalized()) {}
    Vec3 at(float t) const { return origin + direction * t; }
};

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------

struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

    /// Packs to 0xAARRGGBB (little-endian RGBA byte order in memory).
    uint32_t toRgba8() const {
        auto q = [](float v) { return static_cast<uint32_t>(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return (q(r) << 16) | (q(g) << 8) | q(b) | (q(a) << 24);
    }
    static Color fromRgba8(uint32_t c) {
        return {(c >> 16 & 0xFF) / 255.0f, (c >> 8 & 0xFF) / 255.0f,
                (c & 0xFF) / 255.0f, (c >> 24 & 0xFF) / 255.0f};
    }
    /// sRGB hex literal 0xRRGGBB.
    static Color fromHex(uint32_t hex, float a = 1.0f) {
        return {((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f, a};
    }
    uint32_t toHex() const {
        auto q = [](float v) { return static_cast<uint32_t>(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return (q(r) << 16) | (q(g) << 8) | q(b);
    }
    Color operator*(const Color& o) const { return {r * o.r, g * o.g, b * o.b, a * o.a}; }
    Color operator*(float s) const { return {r * s, g * s, b * s, a * s}; }
    Color operator+(const Color& o) const { return {r + o.r, g + o.g, b + o.b, a + o.a}; }
    Color operator-(const Color& o) const { return {r - o.r, g - o.g, b - o.b, a - o.a}; }
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
    Color withAlpha(float a_) const { return {r, g, b, a_}; }
    Color lerpTo(const Color& o, float t) const {
        return {lerpf(r, o.r, t), lerpf(g, o.g, t), lerpf(b, o.b, t), lerpf(a, o.a, t)};
    }
    Vec3 rgb() const { return {r, g, b}; }
    /// Rec. 709 relative luminance.
    float luminance() const { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }
    /// Scales rgb while keeping hue; used for emissive/rim terms.
    Color scaledRgb(float s) const { return {r * s, g * s, b * s, a}; }
};

}  // namespace nova
