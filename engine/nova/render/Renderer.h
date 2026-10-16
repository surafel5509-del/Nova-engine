// The renderers: turn a camera plus a list of things into pixels.
//
// Both renderers sit on the same [Rasterizer]. Neither knows about the ECS,
// the scene graph or the window: they take a framebuffer and a camera and draw.
// That is deliberate -- it is what lets the editor render a preview, the player
// render the game and a test render one triangle, all through one code path.
#pragma once

#include <string>
#include <vector>

#include "input/Input.h"
#include "render/Camera.h"
#include "render/Image.h"
#include "render/Mesh.h"
#include "render/Rasterizer.h"

namespace nova {
namespace render {

/// One mesh instance to draw this frame.
struct DrawItem {
    const Mesh* mesh = nullptr;
    Mat4 model = Mat4::identity();
    Color color{1, 1, 1, 1};
    const Image* texture = nullptr;
    /// Overrides [color] when true: use the colour baked into the mesh.
    bool useVertexColor = false;
    bool castShadow = false;
    int renderOrder = 0;  ///< lower draws first within the same depth range
};

/// Lighting. Kept to what a CPU rasterizer can afford: one directional light,
/// ambient, and a half-Lambert wrap so unlit sides are not black.
struct Lighting {
    bool enabled = true;
    Vec3 lightDirection{-0.4f, -0.8f, -0.45f};  ///< travelling direction of the light
    Color lightColor{1.0f, 0.97f, 0.9f, 1.0f};
    float intensity = 1.0f;
    Color ambient{0.22f, 0.24f, 0.30f, 1.0f};
    /// 0 = Lambert (dark terminator), 1 = flat (no shading at all).
    float wrap = 0.35f;
    /// Fills [out] with the shaded colour of a surface.
    Color shade(const Vec3& worldNormal, const Color& albedo) const;
};

struct RenderStats {
    int itemsSubmitted = 0;
    int itemsDrawn = 0;
    int itemsCulled = 0;
    int trianglesSubmitted = 0;
    RasterStats raster;
    void reset() { *this = RenderStats{}; }
};

/// Shared configuration both renderers read.
struct RenderSettings {
    bool depthTest = true;
    CullMode cull = CullMode::Back;
    BlendMode blend = BlendMode::Replace;
    WrapMode textureWrap = WrapMode::Clamp;
    bool linearFilter = true;
    /// Draws a wireframe box around each item's bounds. Cheap way to see what
    /// the scene actually contains.
    bool drawBounds = false;
    Color boundsColor{0.2f, 1.0f, 0.4f, 0.6f};
    Lighting lighting;
};

/// Draws indexed 3D meshes through a camera.
class Renderer3D {
public:
    /// [fb] must outlive the renderer. [camera] may be replaced each frame.
    void begin(Framebuffer* fb, const Camera& camera, const RenderSettings& settings);
    void submit(const DrawItem& item);
    /// Culls against the frustum, shades and rasterizes everything submitted.
    void end();
    /// begin/submit/end in one call.
    void render(Framebuffer& fb, const Camera& camera, const RenderSettings& settings,
                const std::vector<DrawItem>& items);

    const RenderStats& stats() const { return stats_; }
    const Frustum& frustum() const { return frustum_; }
    size_t pending() const { return items_.size(); }

    /// World-space AABB of everything submitted this frame, for a minimap or
    /// for "frame all".
    Aabb3 submittedBounds() const { return submittedBounds_; }

private:
    struct Prepared {
        const Mesh* mesh = nullptr;
        Mat4 model = Mat4::identity();
        Mat4 normalMatrix = Mat4::identity();
        Color color{1, 1, 1, 1};
        const Image* texture = nullptr;
        bool useVertexColor = false;
        int order = 0;
    };

    void flush();

    Framebuffer* fb_ = nullptr;
    Rasterizer raster_;
    Camera camera_;
    RenderSettings settings_;
    Frustum frustum_;
    std::vector<Prepared> items_;
    Aabb3 submittedBounds_{};
    RenderStats stats_;
};

/// Per-sprite override of the renderer's texture wrap. Tiling backgrounds need
/// Repeat while sprites meant to stretch need Clamp, and those cannot both come
/// from one renderer-wide setting.
enum class SpriteWrap { Inherit, Clamp, Repeat };

/// One quad or sprite in world space.
struct SpriteItem {
    Vec2 center{0, 0};
    Vec2 size{1, 1};
    float rotationDeg = 0.0f;
    Color color{1, 1, 1, 1};
    const Image* texture = nullptr;
    /// Lower draws first. This is the painter's-algorithm order 2D uses; there
    /// is no perspective to sort by.
    float sortKey = 0.0f;
    bool useVertexColor = false;
    SpriteWrap wrap = SpriteWrap::Inherit;
    /// Repeats the texture across the sprite. (1,1) shows it once, stretched;
    /// (4,2) tiles it four times across and twice up, which is what a tiled
    /// background needs. Has no visible effect with a Clamp wrap.
    Vec2 tile{1, 1};
};

/// Draws sorted 2D quads through an orthographic camera.
class Renderer2D {
public:
    void begin(Framebuffer* fb, const Camera& camera, const RenderSettings& settings);
    void submit(const SpriteItem& item);
    void end();
    void render(Framebuffer& fb, const Camera& camera, const RenderSettings& settings,
                const std::vector<SpriteItem>& items);

    const RenderStats& stats() const { return stats_; }
    size_t pending() const { return items_.size(); }

private:
    void flush();

    Framebuffer* fb_ = nullptr;
    Rasterizer raster_;
    Camera camera_;
    RenderSettings settings_;
    std::vector<SpriteItem> items_;
    RenderStats stats_;
};

/// Draws [mesh]'s triangles as thin quads facing the camera. Debug geometry has
/// no area, so a rasterizer that only handles triangles needs something solid
/// to work with; this is that something.
void drawLineSegments(Framebuffer& fb, const Camera& camera,
                      const std::vector<MeshVertex>& segments, Color color);

/// Clears [fb] to the camera's clear colour and sets up the rasterizer state.
void beginFrame(Framebuffer& fb, const Camera& camera, const RenderSettings& settings,
                Rasterizer& raster);

}  // namespace render
}  // namespace nova