#pragma once

#include <cmath>

namespace nova {

/** Interleaved vertex layout consumed by the sprite shader. */
struct SpriteVertex {
    float x, y;      // world position
    float u, v;      // texture coordinates
    float r, g, b, a;
};

/**
 * Computes the four world-space corners of a sprite (bottom-left,
 * bottom-right, top-right, top-left) after scale, rotation and translation.
 * Pure math — unit-tested on the host.
 */
inline void computeQuadCorners(
    float x, float y, float rotationDeg,
    float scaleX, float scaleY, float width, float height,
    float outX[4], float outY[4])
{
    const float hw = width * scaleX * 0.5f;
    const float hh = height * scaleY * 0.5f;
    const float rad = rotationDeg * 0.01745329251994329577f; // deg -> rad
    const float c = std::cos(rad);
    const float s = std::sin(rad);

    const float lx[4] = { -hw,  hw, hw, -hw };
    const float ly[4] = { -hh, -hh, hh,  hh };
    for (int i = 0; i < 4; ++i) {
        outX[i] = x + lx[i] * c - ly[i] * s;
        outY[i] = y + lx[i] * s + ly[i] * c;
    }
}

} // namespace nova
