// Nova Engine — deterministic random number generation.
//
// Every stochastic engine system (particles, AI noise, procedural content)
// takes an explicit seed so replays, tests and networked sessions reproduce
// exactly. The engine never touches a global RNG implicitly.
#pragma once

#include <cstdint>
#include <cmath>

#include "math/Math.h"

namespace nova {

/// PCG32 — small, fast, statistically strong, and trivially reproducible.
class Random {
public:
    explicit Random(uint64_t seed = 0x853c49e6748fea9bULL) { reseed(seed); }

    void reseed(uint64_t seed) {
        state_ = 0u;
        inc_ = (seed << 1u) | 1u;
        nextUInt();
        state_ += seed;
        nextUInt();
    }

    uint32_t nextUInt() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    /// Uniform in [0, 1).
    float nextFloat() {
        // 24 bits of mantissa is the full precision of a float.
        return static_cast<float>(nextUInt() >> 8) * (1.0f / 16777216.0f);
    }

    /// Uniform in [lo, hi).
    float range(float lo, float hi) { return lo + (hi - lo) * nextFloat(); }

    /// Uniform integer in [lo, hi] inclusive.
    int rangeInt(int lo, int hi) {
        if (hi <= lo) return lo;
        const uint32_t span = static_cast<uint32_t>(hi - lo) + 1u;
        return lo + static_cast<int>(nextUInt() % span);
    }

    /// True with probability [p].
    bool chance(float p) { return nextFloat() < p; }

    /// Standard normal via Box-Muller (one value per call, no caching).
    float gaussian(float mean = 0.0f, float stdDev = 1.0f) {
        float u1 = nextFloat();
        const float u2 = nextFloat();
        if (u1 < 1e-7f) u1 = 1e-7f;
        return mean + stdDev * std::sqrt(-2.0f * std::log(u1)) *
                          std::cos(kTwoPi * u2);
    }

    Vec2 onUnitCircle() {
        const float a = nextFloat() * kTwoPi;
        return {std::cos(a), std::sin(a)};
    }
    Vec3 onUnitSphere() {
        // Archimedes' theorem: uniform z, uniform azimuth.
        const float z = range(-1.0f, 1.0f);
        const float a = nextFloat() * kTwoPi;
        const float r = std::sqrt(std::fmax(0.0f, 1.0f - z * z));
        return {r * std::cos(a), r * std::sin(a), z};
    }
    Vec3 insideUnitSphere() { return onUnitSphere() * std::cbrt(nextFloat()); }

    /// Weighted pick. Returns -1 when the table is empty or weights are all zero.
    template <typename T>
    int pickWeighted(const T* items, int count, const float* weights) {
        if (count <= 0) return -1;
        float total = 0.0f;
        for (int i = 0; i < count; ++i) total += weights[i];
        if (total <= 0.0f) return -1;
        float r = nextFloat() * total;
        for (int i = 0; i < count; ++i) {
            r -= weights[i];
            if (r <= 0.0f) return i;
        }
        return count - 1;
    }

    /// Fisher-Yates in place.
    template <typename T>
    void shuffle(T* items, int count) {
        for (int i = count - 1; i > 0; --i) {
            const int j = rangeInt(0, i);
            T tmp = items[i];
            items[i] = items[j];
            items[j] = tmp;
        }
    }

    uint64_t state() const { return state_; }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 1;
};

/// Stable hash → float in [0,1). Used for per-entity variation that must stay
/// constant across frames (e.g. per-tree wind phase).
inline float hashToUnit(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<float>(x >> 11) * (1.0f / 9007199254740992.0f);
}

/// Stable 2D value noise in [0,1]. Cheap, smooth, deterministic per seed.
inline float valueNoise2D(float x, float y, uint64_t seed = 0) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float xf = x - static_cast<float>(xi);
    const float yf = y - static_cast<float>(yi);
    // Quintic smoothstep for C2 continuity (no visible grid creases).
    const float u = xf * xf * xf * (xf * (xf * 6.0f - 15.0f) + 10.0f);
    const float v = yf * yf * yf * (yf * (yf * 6.0f - 15.0f) + 10.0f);

    auto at = [&](int px, int py) {
        const uint64_t h = static_cast<uint64_t>(px) * 0x9E3779B97F4A7C15ULL ^
                           static_cast<uint64_t>(py) * 0xC2B2AE3D27D4EB4FULL ^ seed;
        return hashToUnit(h);
    };
    const float n00 = at(xi, yi),       n10 = at(xi + 1, yi);
    const float n01 = at(xi, yi + 1),   n11 = at(xi + 1, yi + 1);
    return lerpf(lerpf(n00, n10, u), lerpf(n01, n11, u), v);
}

/// Fractal Brownian motion over [valueNoise2D].
inline float fbm2D(float x, float y, int octaves = 4, float lacunarity = 2.0f,
                   float gain = 0.5f, uint64_t seed = 0) {
    float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise2D(x * freq, y * freq, seed + static_cast<uint64_t>(i) * 7919u) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

}  // namespace nova
