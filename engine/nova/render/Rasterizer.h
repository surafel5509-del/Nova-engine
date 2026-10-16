// Software rasterizer.
//
// There is no GPU in this environment and none on the headless hosts the engine
// is tested on, so the renderer is a real scanline rasterizer writing into CPU
// memory. That is not a toy: it is the pipeline a GPU runs -- transform,
// near-plane clip, perspective divide, edge functions, perspective-correct
// attribute interpolation, depth test, textured blend -- minus the part that
// needs hardware. Everything above it (the scene graph, cameras, materials) is
// the same code a GPU backend would consume.
#ifndef NOVA_RENDER_RASTERIZER_H
#define NOVA_RENDER_RASTERIZER_H

#include <cstdint>
#include <vector>

#include "math/Math.h"
#include "render/Image.h"

namespace nova {
namespace render {

/// Colour plus depth. Depth is the NDC z remapped to [0,1], interpolated
/// linearly across the screen, so 0 is the near plane and 1 the far plane.
/// Keying it on 1/w instead would be linear in view depth but would collapse to
/// a constant under an orthographic projection, silently disabling the test.
/// perspective-correct interpolation already computes.
struct Framebuffer {
    Image color;
    std::vector<float> depth;  ///< 0 = near plane, 1 = far plane

    void resize(int w, int h);
    void clear(Color c, float depthValue = 1.0f);
    void clearColor(Color c);
    void clearDepth(float depthValue = 1.0f);
    bool valid() const { return color.valid() && depth.size() == color.pixelCount(); }
    float depthAt(int x, int y) const {
        if (!valid() || !color.inBounds(x, y)) return 0.0f;
        return depth[static_cast<size_t>(y) * static_cast<size_t>(color.width) +
                     static_cast<size_t>(x)];
    }
};

struct Vertex {
    /// w is carried explicitly because the clip stage needs it: dividing by w
    /// is the perspective divide, and losing w before that is unrecoverable.
    Vec4 position{0, 0, 0, 1};  ///< world space (w = 1) for *World, clip for *Clip
    Vec2 uv{0, 0};
    Color color{1, 1, 1, 1};

    static Vertex at(Vec3 p, Color c = {1, 1, 1, 1}) { return Vertex{{p.x, p.y, p.z, 1}, {0, 0}, c}; }
};

enum class CullMode {
    None,   ///< draw both windings
    Back,   ///< drop triangles facing away from the camera
    Front,
};

enum class DepthFunc {
    Always,
    Less,        ///< default
    LessEqual,
};

struct RasterStats {
    int drawCalls = 0;
    int trianglesInput = 0;
    int triangles = 0;       ///< survived clipping and culling
    int trianglesClipped = 0;///< needed near-plane clipping
    int trianglesCulled = 0;
    int pixelsShaded = 0;
    int pixelsRejected = 0;
    void reset() { *this = RasterStats{}; }
};

/// Slack on the near plane (clip space z + w). Anything at or behind the eye
/// has no meaningful projection, so it is clipped instead of divided by.
inline constexpr float kNearEpsilon = 1e-4f;

/// A vertex after the perspective divide: screen pixels plus attributes
/// pre-multiplied by 1/w.
struct ScreenVertex {
    float x = 0, y = 0;
    float invW = 0;
    float zOverW = 0;  ///< clip z / w; linear in screen space, so this *is* NDC z
    float uOverW = 0, vOverW = 0;
    float rOverW = 0, gOverW = 0, bOverW = 0, aOverW = 0;
};

class Rasterizer {
public:
    void setTarget(Framebuffer* target) { target_ = target; }
    Framebuffer* target() const { return target_; }

    /// Combined view-projection applied by the *World entry points.
    void setViewProjection(const Mat4& m) { viewProjection_ = m; }
    const Mat4& viewProjection() const { return viewProjection_; }
    /// Model transform applied between the view-projection and the vertex.
    void setModelTransform(const Mat4& m) { model_ = m; }
    void resetModelTransform() { model_ = Mat4::identity(); }

    /// Pixel rectangle inside the target the image maps onto. Defaults to the
    /// whole target; [w] or [h] of zero restores that default.
    void setViewport(int x, int y, int w, int h);
    void resetViewport();

    void setCull(CullMode m) { cull_ = m; }
    /// How the bound texture behaves outside [0,1]. Clamp by default, which is
    /// what a standalone sprite wants.
    void setTextureWrap(WrapMode m) { textureWrap_ = m; }
    WrapMode textureWrap() const { return textureWrap_; }

    void setDepthTest(bool on) { depthTest_ = on; }
    void setDepthFunc(DepthFunc f) { depthFunc_ = f; }
    void setBlend(BlendMode m) { blend_ = m; }
    /// Bound texture, or null for untextured. Also selects filtering.
    void setTexture(const Image* t) { texture_ = t; }
    void setTextureFiltering(bool linear) { linearFilter_ = linear; }

    /// Triangle in world space.
    void drawTriangleWorld(const Vertex& a, const Vertex& b, const Vertex& c);
    /// Triangle already in clip space -- the stage a GPU takes.
    void drawTriangleClip(const Vertex& a, const Vertex& b, const Vertex& c);
    /// Four corners in order, split into two triangles.
    void drawQuadWorld(const Vertex corners[4]);

    /// Textured, rotated sprite facing the camera. The most common draw in a 2D
    /// game, so it gets a direct path. The existing texture binding is restored.
    void drawSprite(const Image& texture, Vec2 center, Vec2 size, float rotationDeg, Color tint);
    /// Untextured triangle in *screen* pixels, going through the same pipeline
    /// so UI and debug overlays share one code path. Ignores the matrices.
    void drawTriangle2D(Vec2 a, Vec2 b, Vec2 c, Color color);

    const RasterStats& stats() const { return stats_; }
    void resetStats() { stats_.reset(); }

    /// Screen-space position of a world point, or false when it is behind the
    /// camera. Picking and HUD placement both need this.
    bool projectToScreen(Vec3 world, float& outX, float& outY) const;

private:
    Framebuffer* target_ = nullptr;
    Mat4 viewProjection_;
    Mat4 model_;
    const Image* texture_ = nullptr;
    CullMode cull_ = CullMode::Back;
    WrapMode textureWrap_ = WrapMode::Clamp;
    DepthFunc depthFunc_ = DepthFunc::Less;
    BlendMode blend_ = BlendMode::Alpha;
    bool depthTest_ = true;
    bool linearFilter_ = true;
    int viewportX_ = 0, viewportY_ = 0, viewportW_ = 0, viewportH_ = 0;
    bool viewportDirty_ = true;
    RasterStats stats_;

    void ensureViewport();
    ScreenVertex toScreen(const Vertex& v) const;
    void emit(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c);
    void raster(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c, float area);
    // The fragment loop is templated on the blend mode and the filter, and
    // dispatched once per triangle rather than once per pixel. Leaving the
    // choice inside the loop compiles every blend mode and both filters into
    // one body: ~1000 instructions, branch-heavy, and thrashing the instruction
    // cache on a frame that is mostly background.
    template <BlendMode Mode, bool Linear>
    void rasterImpl(const ScreenVertex& a, const ScreenVertex& b, const ScreenVertex& c,
                    float area);
};

}  // namespace render
}  // namespace nova

#endif  // NOVA_RENDER_RASTERIZER_H