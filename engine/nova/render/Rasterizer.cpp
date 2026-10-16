#include "render/Rasterizer.h"

#include <algorithm>

namespace nova {
namespace render {
namespace {

inline uint8_t toByte(float v) { return static_cast<uint8_t>(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); }
// Multiply, not divide: x / 255.0f is a hardware divide, and the blend path
// needs four of them per fragment. 1/255 is not exactly representable, so this
// can differ from the division by one ulp -- far below the half-step that
// toByte() rounds away.
constexpr float kInv255 = 1.0f / 255.0f;
inline float fromByte(uint8_t v) { return static_cast<float>(v) * kInv255; }

// The blend is a template parameter rather than a runtime switch: a branch per
// pixel per channel is exactly the kind of cost a software rasterizer cannot
// afford, and every arm of the old switch ended up inlined into the same loop
// body anyway.
template <BlendMode Mode>
inline void blendPixel(uint8_t* dst, float sr, float sg, float sb, float sa) {
    if (Mode == BlendMode::Replace) {
        dst[0] = toByte(sr);
        dst[1] = toByte(sg);
        dst[2] = toByte(sb);
        dst[3] = toByte(sa);
    } else if (Mode == BlendMode::AlphaOnly) {
        dst[0] = toByte(sr);
        dst[1] = toByte(sg);
        dst[2] = toByte(sb);
    } else if (Mode == BlendMode::Additive) {
        dst[0] = toByte(fromByte(dst[0]) + sr * sa);
        dst[1] = toByte(fromByte(dst[1]) + sg * sa);
        dst[2] = toByte(fromByte(dst[2]) + sb * sa);
        dst[3] = toByte(fromByte(dst[3]) + sa * (1.0f - fromByte(dst[3])));
    } else if (Mode == BlendMode::Multiply) {
        dst[0] = toByte(fromByte(dst[0]) * sr);
        dst[1] = toByte(fromByte(dst[1]) * sg);
        dst[2] = toByte(fromByte(dst[2]) * sb);
        dst[3] = toByte(fromByte(dst[3]) * sa);
    } else {
        const float da = fromByte(dst[3]);
        const float outA = sa + da * (1.0f - sa);
        if (outA <= 0.0f) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0;
            return;
        }
        const float dr = fromByte(dst[0]), dg = fromByte(dst[1]), db = fromByte(dst[2]);
        // One reciprocal, three multiplies: the three divides this replaced were
        // the single largest cost in the fragment loop.
        const float invOutA = 1.0f / outA;
        dst[0] = toByte((sr * sa + dr * da * (1.0f - sa)) * invOutA);
        dst[1] = toByte((sg * sa + dg * da * (1.0f - sa)) * invOutA);
        dst[2] = toByte((sb * sa + db * da * (1.0f - sa)) * invOutA);
        dst[3] = toByte(outA);
    }
}

/// Twice the signed area of a screen-space triangle. Positive when the winding
/// is clockwise on screen (NDC is y-up, the framebuffer is y-down).
inline float signedArea(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

}  // namespace

// --- Framebuffer ------------------------------------------------------------

void Framebuffer::resize(int w, int h) {
    const int nw = std::max(0, w), nh = std::max(0, h);
    color.resize(nw, nh);
    depth.assign(static_cast<size_t>(nw) * static_cast<size_t>(nh), 0.0f);
}

void Framebuffer::clear(Color c, float depthValue) {
    clearColor(c);
    clearDepth(depthValue);
}

void Framebuffer::clearColor(Color c) { color.fill(c); }

void Framebuffer::clearDepth(float depthValue) { std::fill(depth.begin(), depth.end(), depthValue); }

// --- Rasterizer -------------------------------------------------------------

void Rasterizer::setViewport(int x, int y, int w, int h) {
    viewportX_ = x;
    viewportY_ = y;
    viewportW_ = std::max(0, w);
    viewportH_ = std::max(0, h);
    viewportDirty_ = true;
}

void Rasterizer::resetViewport() {
    viewportX_ = viewportY_ = 0;
    viewportW_ = viewportH_ = 0;
    viewportDirty_ = true;
}

void Rasterizer::ensureViewport() {
    if (!viewportDirty_ || !target_) return;
    if (viewportW_ <= 0 || viewportH_ <= 0) {
        viewportX_ = 0;
        viewportY_ = 0;
        viewportW_ = target_->color.width;
        viewportH_ = target_->color.height;
    }
    viewportDirty_ = false;
}

ScreenVertex Rasterizer::toScreen(const Vertex& v) const {
    const float invW = 1.0f / v.position.w;
    const float ndcX = v.position.x * invW;
    const float ndcY = v.position.y * invW;
    ScreenVertex out;
    out.x = viewportX_ + (ndcX * 0.5f + 0.5f) * static_cast<float>(viewportW_);
    out.y = viewportY_ + (0.5f - ndcY * 0.5f) * static_cast<float>(viewportH_);
    out.invW = invW;
    out.zOverW = v.position.z * invW;
    // Attributes ride along pre-multiplied by 1/w; the raster step divides the
    // interpolated result back out, which is what makes the interpolation
    // perspective-correct.
    out.uOverW = v.uv.x * invW;
    out.vOverW = v.uv.y * invW;
    out.rOverW = v.color.r * invW;
    out.gOverW = v.color.g * invW;
    out.bOverW = v.color.b * invW;
    out.aOverW = v.color.a * invW;
    return out;
}

void Rasterizer::drawTriangleWorld(const Vertex& a, const Vertex& b, const Vertex& c) {
    ++stats_.drawCalls;
    ++stats_.trianglesInput;
    const Mat4 vp = viewProjection_ * model_;
    drawTriangleClip(Vertex{vp.project({a.position.x, a.position.y, a.position.z}), a.uv, a.color},
                     Vertex{vp.project({b.position.x, b.position.y, b.position.z}), b.uv, b.color},
                     Vertex{vp.project({c.position.x, c.position.y, c.position.z}), c.uv, c.color});
}

void Rasterizer::drawQuadWorld(const Vertex corners[4]) {
    drawTriangleWorld(corners[0], corners[1], corners[2]);
    drawTriangleWorld(corners[0], corners[2], corners[3]);
}

void Rasterizer::drawTriangleClip(const Vertex& in0, const Vertex& in1, const Vertex& in2) {
    if (!target_ || !target_->valid()) return;
    ensureViewport();
    if (viewportW_ <= 0 || viewportH_ <= 0) return;

    // Near-plane clip in homogeneous space. A vertex behind the eye has a w that
    // flips sign, so dividing by it mirrors the triangle across the screen --
    // clipping has to happen before the divide, never after.
    Vertex poly[4];
    int count = 0;
    bool clipped = false;
    const Vertex* input[3] = {&in0, &in1, &in2};
    for (int i = 0; i < 3; ++i) {
        const Vertex& cur = *input[i];
        const Vertex& nxt = *input[(i + 1) % 3];
        // The near plane is z + w = 0, not z = 0. Under a perspective
        // projection w equals the view depth and a perfectly visible vertex
        // has a positive clip z, so testing z alone throws away most of the
        // scene and keeps only what is behind the camera.
        const float curIn = cur.position.z + cur.position.w - kNearEpsilon;
        if (curIn < 0.0f) clipped = true;
        const float nxtIn = nxt.position.z + nxt.position.w - kNearEpsilon;
        if (curIn >= 0.0f && count < 4) poly[count++] = cur;
        if ((curIn >= 0.0f) != (nxtIn >= 0.0f) && count < 4) {
            const float t = curIn / (curIn - nxtIn);
            Vertex v;
            v.position = cur.position + (nxt.position - cur.position) * t;
            v.uv = cur.uv + (nxt.uv - cur.uv) * t;
            v.color = cur.color.lerpTo(nxt.color, t);
            poly[count++] = v;
        }
    }
    if (count < 3) {
        ++stats_.trianglesCulled;
        return;
    }
    if (clipped) ++stats_.trianglesClipped;

    // Fan-triangulate whatever survived the clip.
    for (int i = 1; i + 1 < count; ++i) emit(toScreen(poly[0]), toScreen(poly[i]), toScreen(poly[i + 1]));
}

void Rasterizer::emit(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c) {
    const float area = signedArea(a, b, c);
    if (std::fabs(area) < 1e-7f) {
        ++stats_.trianglesCulled;
        return;
    }
    if ((cull_ == CullMode::Back && area > 0.0f) ||
        (cull_ == CullMode::Front && area < 0.0f)) {
        ++stats_.trianglesCulled;
        return;
    }
    raster(a, b, c, area);
}

void Rasterizer::raster(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c,
                        float area) {
    // Once per triangle, not once per pixel: the dispatch collapses away and
    // every instantiation below runs a straight-line loop.
    switch (blend_) {
        case BlendMode::Replace:
            linearFilter_ ? rasterImpl<BlendMode::Replace, true>(a, b, c, area)
                          : rasterImpl<BlendMode::Replace, false>(a, b, c, area);
            return;
        case BlendMode::AlphaOnly:
            linearFilter_ ? rasterImpl<BlendMode::AlphaOnly, true>(a, b, c, area)
                          : rasterImpl<BlendMode::AlphaOnly, false>(a, b, c, area);
            return;
        case BlendMode::Additive:
            linearFilter_ ? rasterImpl<BlendMode::Additive, true>(a, b, c, area)
                          : rasterImpl<BlendMode::Additive, false>(a, b, c, area);
            return;
        case BlendMode::Multiply:
            linearFilter_ ? rasterImpl<BlendMode::Multiply, true>(a, b, c, area)
                          : rasterImpl<BlendMode::Multiply, false>(a, b, c, area);
            return;
        case BlendMode::Alpha:
        default:
            linearFilter_ ? rasterImpl<BlendMode::Alpha, true>(a, b, c, area)
                          : rasterImpl<BlendMode::Alpha, false>(a, b, c, area);
            return;
    }
}

template <BlendMode Mode, bool Linear>
void Rasterizer::rasterImpl(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c,
                            float area) {
    Framebuffer& fb = *target_;
    Image& img = fb.color;

    const int minX =
        std::max(viewportX_, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int maxX = std::min(viewportX_ + viewportW_ - 1,
                              static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int minY =
        std::max(viewportY_, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int maxY = std::min(viewportY_ + viewportH_ - 1,
                              static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    if (minX > maxX || minY > maxY) return;

    ++stats_.triangles;
    const float invArea = 1.0f / area;

    // Edge functions are affine in (px, py), so each row and each step within a
    // row is a single add instead of a fresh dot product. This is the whole
    // difference between a rasterizer that keeps up with a game loop and one
    // that does not: the multiply count per pixel drops from twelve to none.
    const float dwdx0 = -(c.y - b.y) * invArea;
    const float dwdy0 = (c.x - b.x) * invArea;
    const float dwdx1 = -(a.y - c.y) * invArea;
    const float dwdy1 = (a.x - c.x) * invArea;

    // An orthographic (or otherwise affine) triangle has the same 1/w at every
    // vertex, so the perspective divide is a constant. Hoisting it out of the
    // pixel loop turns six divides per fragment into six multiplies -- and
    // because invW is exactly equal across the vertices, multiplying by its
    // reciprocal is bit-identical to dividing.
    const bool affine = (a.invW == b.invW) && (a.invW == c.invW) && (a.invW != 0.0f);
    const float affineScale = affine ? (1.0f / a.invW) : 1.0f;

    // Images are always RGBA8, so the channel count is a compile-time constant
    // rather than a load from the image on every fragment.
    const int channels = 4;
    uint8_t* const pixels = img.pixels.data();
    // Fragment counters live in registers for the duration of the triangle;
    // writing them through the stats object once per fragment is a read-modify-
    // write to memory on every pixel.
    int shaded = 0, rejected = 0;
    float* const depthBuf = fb.depth.data();
    const int stride = img.width * channels;

    for (int y = minY; y <= maxY; ++y) {
        const float py = static_cast<float>(y) + 0.5f;
        const float baseX = static_cast<float>(minX) + 0.5f;
        // w0 and w1 at the left edge of this row, then stepped per pixel.
        float w0Row = ((c.x - b.x) * (py - b.y) - (c.y - b.y) * (baseX - b.x)) * invArea;
        float w1Row = ((a.x - c.x) * (py - c.y) - (a.y - c.y) * (baseX - c.x)) * invArea;
        uint8_t* row = pixels + static_cast<size_t>(y) * static_cast<size_t>(stride);

        for (int x = minX; x <= maxX; ++x, w0Row += dwdx0, w1Row += dwdx1) {
            float w0 = w0Row;
            float w1 = w1Row;
            float recipW = 0.0f;

            // Edge functions: zero on the opposite edge, and the sign matches
            // [area] inside the triangle.
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

            // Interpolating z/w is what makes the depth buffer meaningful, but
            // it is four multiply-adds plus a rescale that a 2D painter's-
            // algorithm stack throws away. Only pay for it when a depth test is
            // actually going to look at it.
            float fragmentDepth = 0.0f;
            const size_t index =
                static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x);
            if (depthTest_) {
                const float invW = w0 * a.invW + w1 * b.invW + w2 * c.invW;
                if (invW <= 0.0f) continue;
                // NDC z is exactly the screen-linear interpolation of z/w: the
                // perspective divide cancels between the interpolated z and the
                // interpolated w, so it must NOT be applied a second time here.
                // Interpolating raw 1/w instead would be linear in view depth,
                // which is identically 1 under an orthographic projection and
                // would make every fragment tie.
                const float ndcZ = w0 * a.zOverW + w1 * b.zOverW + w2 * c.zOverW;
                fragmentDepth = ndcZ * 0.5f + 0.5f;

                const float existing = depthBuf[index];
                const bool pass = depthFunc_ == DepthFunc::Always ||
                                  (depthFunc_ == DepthFunc::Less ? fragmentDepth < existing
                                                                  : fragmentDepth <= existing);
                if (!pass) {
                    ++rejected;
                    continue;
                }
                recipW = 1.0f / invW;
            } else if (!affine) {
                const float invW = w0 * a.invW + w1 * b.invW + w2 * c.invW;
                if (invW <= 0.0f) continue;
                recipW = 1.0f / invW;
            }

            // Perspective-correct attributes: interpolate attr/w and divide by
            // the interpolated 1/w. Interpolating them directly is the classic
            // "texture slides across the surface" bug.
            const float recip = affine ? affineScale : recipW;
            const float u = (w0 * a.uOverW + w1 * b.uOverW + w2 * c.uOverW) * recip;
            const float v = (w0 * a.vOverW + w1 * b.vOverW + w2 * c.vOverW) * recip;
            float r = (w0 * a.rOverW + w1 * b.rOverW + w2 * c.rOverW) * recip;
            float g = (w0 * a.gOverW + w1 * b.gOverW + w2 * c.gOverW) * recip;
            float bl = (w0 * a.bOverW + w1 * b.bOverW + w2 * c.bOverW) * recip;
            const float al = (w0 * a.aOverW + w1 * b.aOverW + w2 * c.aOverW) * recip;

            uint8_t* const dst = row + static_cast<size_t>(x) * static_cast<size_t>(channels);
            if (texture_) {
                const Color t = texture_->sampleNormalized(u, v, textureWrap_, Linear);
                r *= t.r;
                g *= t.g;
                bl *= t.b;
                blendPixel<Mode>(dst, r, g, bl, al * t.a);
            } else {
                blendPixel<Mode>(dst, r, g, bl, al);
            }

            if (depthTest_) depthBuf[index] = fragmentDepth;
            ++shaded;
        }
    }
    stats_.pixelsShaded += shaded;
    stats_.pixelsRejected += rejected;
}

void Rasterizer::drawSprite(const Image& texture, Vec2 center, Vec2 size, float rotationDeg,
                            Color tint) {
    const float hw = size.x * 0.5f, hh = size.y * 0.5f;
    const float r = degToRad(rotationDeg);
    const float cs = std::cos(r), sn = std::sin(r);
    auto corner = [&](float lx, float ly, float u, float v) {
        Vertex vert;
        vert.position = Vec4{center.x + lx * cs - ly * sn, center.y + lx * sn + ly * cs, 0.0f, 1.0f};
        vert.uv = {u, v};
        vert.color = tint;
        return vert;
    };
    const Vertex quad[4] = {
        corner(-hw, -hh, 0.0f, 0.0f),
        corner(hw, -hh, 1.0f, 0.0f),
        corner(hw, hh, 1.0f, 1.0f),
        corner(-hw, hh, 0.0f, 1.0f),
    };
    const Image* previous = texture_;
    setTexture(&texture);
    drawQuadWorld(quad);
    setTexture(previous);
}

void Rasterizer::drawTriangle2D(Vec2 a, Vec2 b, Vec2 c, Color color) {
    if (!target_ || !target_->valid()) return;
    ensureViewport();
    if (viewportW_ <= 0 || viewportH_ <= 0) return;
    ++stats_.drawCalls;
    ++stats_.trianglesInput;

    // Screen pixels -> clip space, so UI overlays share the one rasterizer.
    auto toClip = [&](Vec2 p) -> Vec4 {
        const float ndcX = (p.x - static_cast<float>(viewportX_)) /
                               std::max(1.0f, static_cast<float>(viewportW_)) * 2.0f - 1.0f;
        const float ndcY = 1.0f - (p.y - static_cast<float>(viewportY_)) /
                                         std::max(1.0f, static_cast<float>(viewportH_)) * 2.0f;
        return Vec4{ndcX, ndcY, 0.0f, 1.0f};
    };
    drawTriangleClip(Vertex{toClip(a), {0, 0}, color}, Vertex{toClip(b), {0, 0}, color},
                     Vertex{toClip(c), {0, 0}, color});
}

bool Rasterizer::projectToScreen(Vec3 world, float& outX, float& outY) const {
    Rasterizer* self = const_cast<Rasterizer*>(this);
    self->ensureViewport();
    if (viewportW_ <= 0 || viewportH_ <= 0) return false;
    const Vec4 clip = (viewProjection_ * model_).project(world);
    if (clip.w <= kNearEpsilon) return false;  // behind the camera
    const float invW = 1.0f / clip.w;
    outX = viewportX_ + (clip.x * invW * 0.5f + 0.5f) * static_cast<float>(viewportW_);
    outY = viewportY_ + (0.5f - clip.y * invW * 0.5f) * static_cast<float>(viewportH_);
    return true;
}


// The dispatcher above is the only caller, so the loop body is instantiated
// exactly here: ten copies, one per (blend, filter) pair.
#define NOVA_RASTER_CASE(MODE)                                              \
    template void Rasterizer::rasterImpl<MODE, true>(                       \
        const ScreenVertex&, const ScreenVertex&, const ScreenVertex&, float); \
    template void Rasterizer::rasterImpl<MODE, false>(                      \
        const ScreenVertex&, const ScreenVertex&, const ScreenVertex&, float);

NOVA_RASTER_CASE(BlendMode::Replace)
NOVA_RASTER_CASE(BlendMode::AlphaOnly)
NOVA_RASTER_CASE(BlendMode::Additive)
NOVA_RASTER_CASE(BlendMode::Multiply)
NOVA_RASTER_CASE(BlendMode::Alpha)
#undef NOVA_RASTER_CASE

}  // namespace render
}  // namespace nova