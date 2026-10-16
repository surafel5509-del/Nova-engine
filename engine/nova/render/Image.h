// CPU-side image and the PNG codec.
//
// Everything the engine draws ends up here: the software rasterizer writes
// into an Image, the editor host encodes it to PNG and streams it to the
// browser, and the loader decodes whatever an artist hands over. Keeping the
// pixel format fixed at RGBA8 removes a whole class of conversion bugs -- there
// is exactly one byte layout in the engine, and the codec converts at the edge.
#ifndef NOVA_RENDER_IMAGE_H
#define NOVA_RENDER_IMAGE_H

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "math/Math.h"

namespace nova {
namespace render {

/// What happens to texture coordinates outside [0,1].
enum class WrapMode {
    /// Stretch the edge texel outwards. Right for a one-off sprite atlas.
    Clamp,
    /// Tile the texture. Right for anything tiled, like a ground plane.
    Repeat,
    /// Tile, flipping every other repeat so the seam is not visible.
    Mirror,
};

/// How a source pixel is combined with what is already in the target.
enum class BlendMode {
    /// Source replaces destination entirely. The default for clears and for
    /// compositing a finished frame.
    Replace,
    /// Standard "over" operator, source alpha weighted by coverage.
    Alpha,
    /// Colours are added. Used for emissive and bloom-style passes.
    Additive,
    /// Destination is multiplied by source; how a sprite stamps a shadow.
    Multiply,
    /// Coverage only: the source colour replaces the destination but the
    /// destination keeps its alpha, so a mask can be drawn without erasing.
    AlphaOnly,
};

/// Row-major RGBA8 image, top-down, no row padding.
// Texture addressing lives in the header because the rasterizer's fragment
// loop calls it four times per bilinear texel. Out of line, that call plus the
// struct return cost more than the arithmetic around them.
namespace detail {

inline int wrapTexel(int i, int n, WrapMode mode) {
    if (n <= 0) return 0;
    // Two compares for the in-range case; the modulo below is a hardware
    // divide and would dominate the loop otherwise.
    if (static_cast<unsigned int>(i) < static_cast<unsigned int>(n)) {
        if (mode != WrapMode::Mirror) return i;
    }
    if (mode == WrapMode::Repeat) {
        const int m = i % n;
        return m < 0 ? m + n : m;
    }
    if (mode == WrapMode::Mirror) {
        const int period = 2 * n;
        int m = i % period;
        if (m < 0) m += period;
        return m < n ? m : period - 1 - m;
    }
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

}  // namespace detail


struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  ///< width * height * 4

    Image() = default;
    Image(int w, int h, Color fill = Color{0, 0, 0, 0});

    bool valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    }
    void invalidate() {
        width = height = 0;
        pixels.clear();
    }
    size_t pixelCount() const { return static_cast<size_t>(width) * static_cast<size_t>(height); }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

    uint8_t* at(int x, int y) {
        return pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(width) +
                                static_cast<size_t>(x)) * 4;
    }
    const uint8_t* at(int x, int y) const {
        return pixels.data() + (static_cast<size_t>(y) * static_cast<size_t>(width) +
                                static_cast<size_t>(x)) * 4;
    }
    Color get(int x, int y) const {
        // An empty image reads as transparent black rather than walking off the
        // end of a zero-length buffer.
        if (!valid()) return Color{0, 0, 0, 0};
        const uint8_t* p = at(x, y);
        // Divided, not offset by half a step: the +0.5 form would map 0 to
        // 1/255, which no longer round-trips back to 0.
        const auto f = [](uint8_t v) { return static_cast<float>(v) / 255.0f; };
        return {f(p[0]), f(p[1]), f(p[2]), f(p[3])};
    }
    void set(int x, int y, Color c) {
        if (!inBounds(x, y)) return;
        uint8_t* p = at(x, y);
        const auto q = [](float v) { return static_cast<uint8_t>(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        p[0] = q(c.r);
        p[1] = q(c.g);
        p[2] = q(c.b);
        p[3] = q(c.a);
    }

    /// Resizes the image, carrying the overlapping content over to the new
    /// top-left corner and filling only the newly exposed area with [fill].
    /// This is reallocation, not scaling: use blitScaled to resample.
    void resize(int w, int h, Color fill = Color{0, 0, 0, 0});
    void fill(Color c);
    void fillRect(int x, int y, int w, int h, Color c);
    void fillRect(const Aabb2& r, Color c);
    void drawRectOutline(int x, int y, int w, int h, Color c, int thickness = 1);
    void drawLine(Vec2 a, Vec2 b, Color c, int thickness = 1);
    void drawCircle(Vec2 center, float radius, Color c);
    void drawCircleOutline(Vec2 center, float radius, Color c, int thickness = 1);
    void drawTriangle(Vec2 a, Vec2 b, Vec2 cc, Color c);

    /// Composites [src] at (dx, dy), clipping to both images.
    void blit(const Image& src, int dx, int dy, BlendMode mode = BlendMode::Alpha);
    /// Draws one texel of [src] scaled to fill the destination rectangle.
    void blitScaled(const Image& src, const Aabb2& dst, BlendMode mode = BlendMode::Alpha);
    /// Draws the sub-rectangle [sx, sy, sw, sh] of [src] scaled into [dst].
    void blitSubRect(const Image& src, const Aabb2& srcRect, const Aabb2& dst,
                     BlendMode mode = BlendMode::Alpha);
    /// Fills the whole image with a vertical two-colour gradient.
    void fillVerticalGradient(Color top, Color bottom);

    Image subImage(int x, int y, int w, int h) const;
    Image flippedX() const;
    Image flippedY() const;

    /// Linear-interpolated sample. Coordinates are in pixels, the origin is the
    /// centre of the top-left texel, and out-of-range reads clamp to the edge.
    Color sampleBilinear(float x, float y) const;
    /// Single texel fetch with the same coordinate convention, nearest.
    Color sampleNearest(float x, float y) const;

    /// Sample with the normalised coordinates geometry uses, where (0,0) is the
    /// top-left corner of the image and (1,1) the bottom-right. Texel centres
    /// land at ((i + 0.5) / width), which is what keeps a full-texture quad from
    /// sampling off the edge. The pixel-space samplers above cannot be used for
    /// this: a ground quad covers 1% of the texture but spans the whole surface.
    /// Samples at normalized coordinates. [linear] taps the four neighbouring
    /// texels; otherwise the nearest texel wins. Out-of-range coordinates follow
    /// [wrap].
    Color sampleNormalized(float u, float v, WrapMode wrap, bool linear) const {
        if (width <= 0 || height <= 0) return Color{0, 0, 0, 0};
        // Shift by half a texel so that u = 0 and u = 1 land on the outer edges
        // of the first and last texels rather than on their centres.
        const float fx = u * static_cast<float>(width) - 0.5f;
        const float fy = v * static_cast<float>(height) - 0.5f;
        // Raw texel reads: the indices are in range once wrapped, so the
        // per-call validity check and the repeated row multiply are overhead.
        const uint8_t* const base = pixels.data();
        const auto fetch = [&](int x, int y) {
            const uint8_t* p =
                base + (static_cast<size_t>(y) * static_cast<size_t>(width) +
                        static_cast<size_t>(x)) * 4;
            return Color{static_cast<float>(p[0]) / 255.0f, static_cast<float>(p[1]) / 255.0f,
                         static_cast<float>(p[2]) / 255.0f, static_cast<float>(p[3]) / 255.0f};
        };

        if (!linear) {
            return fetch(detail::wrapTexel(static_cast<int>(std::lround(fx)), width, wrap),
                         detail::wrapTexel(static_cast<int>(std::lround(fy)), height, wrap));
        }

        const float bx = std::floor(fx), by = std::floor(fy);
        const float tx = fx - bx, ty = fy - by;
        const int x0 = detail::wrapTexel(static_cast<int>(bx), width, wrap);
        const int y0 = detail::wrapTexel(static_cast<int>(by), height, wrap);
        const int x1 = detail::wrapTexel(static_cast<int>(bx) + 1, width, wrap);
        const int y1 = detail::wrapTexel(static_cast<int>(by) + 1, height, wrap);
        return fetch(x0, y0)
            .lerpTo(fetch(x1, y0), tx)
            .lerpTo(fetch(x0, y1).lerpTo(fetch(x1, y1), tx), ty);
    }
};

/// Encodes to a PNG byte stream. Uses 8-bit RGBA, or RGB when every pixel is
/// opaque, and picks the smaller of the per-row filter types the way a real
/// encoder does.
std::vector<uint8_t> encodePng(const Image& image);

/// Decodes a PNG. Supports bit depth 8 (and 16, truncated to 8) in greyscale,
/// greyscale+alpha, truecolour, truecolour+alpha, and indexed with palette and
/// transparency -- which covers everything produced by common tools.
bool decodePng(const uint8_t* data, size_t size, Image& out);

bool writePng(const std::string& path, const Image& image);
bool readPng(const std::string& path, Image& out);

}  // namespace render
}  // namespace nova

#endif  // NOVA_RENDER_IMAGE_H