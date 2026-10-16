#include "render/Renderer.h"

#include <algorithm>
#include <cmath>

namespace nova {
namespace render {

Color Lighting::shade(const Vec3& worldNormal, const Color& albedo) const {
    if (!enabled) return albedo;
    const Vec3 n = worldNormal.lengthSq() > 1e-12f ? worldNormal.normalized() : Vec3{0, 1, 0};
    Vec3 l = lightDirection;
    const float ll = l.length();
    if (ll > 1e-6f) {
        l = l / ll;
    } else {
        return Color{albedo.r * ambient.r, albedo.g * ambient.g, albedo.b * ambient.b,
                     albedo.a};
    }
    float ndotl = -n.dot(l);
    // Half-Lambert: remap [-1,1] to [0,1] and blend back towards flat by
    // [wrap]. Without it the unlit half of every object is pure black, which
    // reads as missing geometry rather than as shadow.
    const float w = clampf(wrap, 0.0f, 1.0f);
    const float lambert = std::max(0.0f, ndotl);
    const float half = ndotl * 0.5f + 0.5f;
    const float diffuse = lambert * (1.0f - w) + half * w;
    const float lit = clampf(diffuse * intensity, 0.0f, 4.0f);

    Color out;
    out.r = albedo.r * (ambient.r + lightColor.r * lit);
    out.g = albedo.g * (ambient.g + lightColor.g * lit);
    out.b = albedo.b * (ambient.b + lightColor.b * lit);
    out.a = albedo.a;
    return out;
}

void beginFrame(Framebuffer& fb, const Camera& camera, const RenderSettings& settings,
                Rasterizer& raster) {
    if (!fb.valid()) return;
    fb.clearColor(camera.clearColor);
    fb.clearDepth(1.0f);
    raster.setTarget(&fb);
    raster.resetViewport();
    raster.resetModelTransform();
    raster.setViewProjection(camera.viewProjection());
    raster.setCull(settings.cull);
    raster.setDepthTest(settings.depthTest);
    raster.setBlend(settings.blend);
    raster.setTextureWrap(settings.textureWrap);
    raster.setTextureFiltering(settings.linearFilter);
    raster.setTexture(nullptr);
}

// --- Renderer3D -------------------------------------------------------------

void Renderer3D::begin(Framebuffer* fb, const Camera& camera, const RenderSettings& settings) {
    fb_ = fb;
    camera_ = camera;
    settings_ = settings;
    items_.clear();
    submittedBounds_ = Aabb3{};
    stats_.reset();
    if (!fb_ || !fb_->valid()) return;
    camera_.aspect = static_cast<float>(fb_->color.width) /
                     std::max(1.0f, static_cast<float>(fb_->color.height));
    beginFrame(*fb_, camera_, settings_, raster_);
    frustum_.setFromViewProjection(camera_.viewProjection());
}

void Renderer3D::submit(const DrawItem& item) {
    if (!item.mesh || item.mesh->empty()) return;
    Prepared p;
    p.mesh = item.mesh;
    p.model = item.model;
    p.color = item.color;
    p.texture = item.texture;
    p.useVertexColor = item.useVertexColor;
    p.order = item.renderOrder;
    items_.push_back(p);
    ++stats_.itemsSubmitted;
    stats_.trianglesSubmitted += static_cast<int>(item.mesh->triangleCount());
}

void Renderer3D::end() {
    flush();
    stats_.raster = raster_.stats();
    items_.clear();
}

void Renderer3D::render(Framebuffer& fb, const Camera& camera, const RenderSettings& settings,
                        const std::vector<DrawItem>& items) {
    begin(&fb, camera, settings);
    for (const DrawItem& item : items) submit(item);
    end();
}

void Renderer3D::flush() {
    if (!fb_ || !fb_->valid() || items_.empty()) return;

    // Painter's order: renderOrder first, then front to back. Front-to-back
    // matters on a CPU rasterizer because the depth test then rejects most
    // fragments before any shading work happens.
    std::vector<Vec3> centers(items_.size());
    for (size_t i = 0; i < items_.size(); ++i) {
        centers[i] = items_[i].model.transformPoint(items_[i].mesh->bounds().center());
    }
    std::vector<size_t> order(items_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    const Camera cam = camera_;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (items_[a].order != items_[b].order) return items_[a].order < items_[b].order;
        const float da = (centers[a] - cam.position).lengthSq();
        const float db = (centers[b] - cam.position).lengthSq();
        return da < db;
    });

    for (size_t idx : order) {
        Prepared& p = items_[idx];
        const Aabb3 worldBounds = p.mesh->bounds().transformed(p.model);
        submittedBounds_ = (submittedBounds_.valid()) ? submittedBounds_.merged(worldBounds)
                                                     : worldBounds;
        if (!frustum_.intersectsAabb(worldBounds)) {
            ++stats_.itemsCulled;
            continue;
        }

        raster_.setModelTransform(p.model);
        raster_.setTexture(p.texture);

        const size_t triCount = p.mesh->triangleCount();
        for (size_t t = 0; t < triCount; ++t) {
            const uint32_t i0 = p.mesh->indices[t * 3];
            const uint32_t i1 = p.mesh->indices[t * 3 + 1];
            const uint32_t i2 = p.mesh->indices[t * 3 + 2];
            if (i0 >= p.mesh->vertices.size() || i1 >= p.mesh->vertices.size() ||
                i2 >= p.mesh->vertices.size()) {
                continue;
            }
            const MeshVertex& m0 = p.mesh->vertices[i0];
            const MeshVertex& m1 = p.mesh->vertices[i1];
            const MeshVertex& m2 = p.mesh->vertices[i2];

            // Normals go through the model matrix. The lighting is computed
            // per vertex because the rasterizer interpolates colour, which is
            // exactly what Gouraud shading is.
            const Vec3 wn0 = p.model.transformDirection(m0.normal).normalized();
            const Vec3 wn1 = p.model.transformDirection(m1.normal).normalized();
            const Vec3 wn2 = p.model.transformDirection(m2.normal).normalized();

            const Color base0 = p.useVertexColor ? m0.color : p.color;
            const Color base1 = p.useVertexColor ? m1.color : p.color;
            const Color base2 = p.useVertexColor ? m2.color : p.color;

            Vertex v0, v1, v2;
            v0.position = Vec4(p.model.transformPoint(m0.position), 1.0f);
            v1.position = Vec4(p.model.transformPoint(m1.position), 1.0f);
            v2.position = Vec4(p.model.transformPoint(m2.position), 1.0f);
            v0.uv = m0.uv;
            v1.uv = m1.uv;
            v2.uv = m2.uv;
            v0.color = settings_.lighting.shade(wn0, base0);
            v1.color = settings_.lighting.shade(wn1, base1);
            v2.color = settings_.lighting.shade(wn2, base2);
            raster_.drawTriangleWorld(v0, v1, v2);
        }

        if (settings_.drawBounds) {
            raster_.setTexture(nullptr);
            raster_.setDepthTest(false);
            const std::vector<MeshVertex> edges = p.mesh->boundsWireframe(settings_.boundsColor);
            drawLineSegments(*fb_, camera_, edges, settings_.boundsColor);
            raster_.setDepthTest(settings_.depthTest);
        }
        ++stats_.itemsDrawn;
    }
}

// --- Renderer2D -------------------------------------------------------------

void Renderer2D::begin(Framebuffer* fb, const Camera& camera, const RenderSettings& settings) {
    fb_ = fb;
    camera_ = camera;
    settings_ = settings;
    items_.clear();
    stats_.reset();
    if (!fb_ || !fb_->valid()) return;
    camera_.aspect = static_cast<float>(fb_->color.width) /
                     std::max(1.0f, static_cast<float>(fb_->color.height));
    // 2D has no perspective to resolve, so layering is decided entirely by
    // [sortKey]. Leaving the depth test on would make every sprite after the
    // first fail against an identical depth and silently drop, and a flat quad
    // has no meaningful facing for a winding test to get right.
    RenderSettings flat = settings;
    flat.depthTest = false;
    flat.cull = CullMode::None;
    beginFrame(*fb_, camera_, flat, raster_);
}

void Renderer2D::submit(const SpriteItem& item) { items_.push_back(item); }

void Renderer2D::end() {
    flush();
    stats_.raster = raster_.stats();
    items_.clear();
}

void Renderer2D::render(Framebuffer& fb, const Camera& camera, const RenderSettings& settings,
                        const std::vector<SpriteItem>& items) {
    begin(&fb, camera, settings);
    for (const SpriteItem& item : items) submit(item);
    end();
}

void Renderer2D::flush() {
    if (!fb_ || !fb_->valid() || items_.empty()) return;

    // 2D has no perspective to sort by, so [sortKey] is the whole story. The
    // sort has to be stable: two sprites on the same layer keep submission
    // order, which is what an artist's layer list means.
    std::vector<const SpriteItem*> sorted;
    sorted.reserve(items_.size());
    for (const SpriteItem& item : items_) sorted.push_back(&item);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const SpriteItem* a, const SpriteItem* b) {
                         return a->sortKey < b->sortKey;
                     });

    for (const SpriteItem* item : sorted) {
        raster_.setTexture(item->texture);
        raster_.setTextureWrap(item->wrap == SpriteWrap::Repeat   ? WrapMode::Repeat
                               : item->wrap == SpriteWrap::Clamp  ? WrapMode::Clamp
                                                                    : settings_.textureWrap);
        const float hw = item->size.x * 0.5f;
        const float hh = item->size.y * 0.5f;
        const float r = degToRad(item->rotationDeg);
        const float cs = std::cos(r), sn = std::sin(r);
        auto corner = [&](float lx, float ly, float u, float v) {
            Vertex vert;
            vert.position = Vec4(item->center.x + lx * cs - ly * sn,
                                 item->center.y + lx * sn + ly * cs, 0.0f, 1.0f);
            vert.uv = {u * item->tile.x, v * item->tile.y};
            vert.color = item->color;
            return vert;
        };
        const Vertex quad[4] = {
            corner(-hw, -hh, 0.0f, 0.0f),
            corner(hw, -hh, 1.0f, 0.0f),
            corner(hw, hh, 1.0f, 1.0f),
            corner(-hw, hh, 0.0f, 1.0f),
        };
        raster_.drawQuadWorld(quad);
        ++stats_.itemsDrawn;
        stats_.trianglesSubmitted += 2;
    }
}

// --- lines ------------------------------------------------------------------

void drawLineSegments(Framebuffer& fb, const Camera& camera,
                      const std::vector<MeshVertex>& segments, Color color) {
    if (!fb.valid()) return;
    Rasterizer raster;
    beginFrame(fb, camera, RenderSettings{}, raster);
    raster.setDepthTest(false);
    raster.setCull(CullMode::None);
    raster.setBlend(BlendMode::Alpha);

    // Each segment becomes a camera-facing quad one pixel wide. Working in NDC
    // and dividing the offset back by w is what keeps that true at every depth:
    // a line far away stays one pixel wide instead of vanishing.
    auto toNdc = [&](const Vec3& p, float& x, float& y, float& w) {
        const Vec4 clip = camera.viewProjection().project(p);
        w = clip.w;
        const float iw = (std::fabs(w) > 1e-6f) ? 1.0f / w : 1.0f;
        x = clip.x * iw;
        y = clip.y * iw;
    };

    for (size_t i = 0; i + 1 < segments.size(); i += 2) {
        float ax, ay, aw, bx, by, bw;
        toNdc(segments[i].position, ax, ay, aw);
        toNdc(segments[i + 1].position, bx, by, bw);
        if (aw <= 1e-5f || bw <= 1e-5f) continue;

        // Half a pixel, in NDC units. NDC spans 2 across the frame, so one
        // pixel is 2/width, and the quad needs half that either side of the
        // centre line to come out one pixel wide.
        const float pxX = 2.0f / static_cast<float>(std::max(1, fb.color.width));
        const float pxY = 2.0f / static_cast<float>(std::max(1, fb.color.height));

        float dx = bx - ax, dy = by - ay;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-7f) continue;
        dx /= len;
        dy /= len;
        // Normal in screen space; half a pixel wide either side.
        const float ox = dy * pxX * 0.5f;
        const float oy = -dx * pxY * 0.5f;

        Vertex v[4];
        const float xs[4] = {ax - ox, ax + ox, bx + ox, bx - ox};
        const float ys[4] = {ay - oy, ay + oy, by + oy, by - oy};
        const float ws[4] = {aw, aw, bw, bw};
        for (int k = 0; k < 4; ++k) {
            v[k].position = Vec4{xs[k] * ws[k], ys[k] * ws[k], ws[k] * 0.999f, ws[k]};
            v[k].color = color;
        }
        // These are already clip-space vertices, so they go straight to the
        // clip stage. Routing them through *World would re-project them.
        raster.drawTriangleClip(v[0], v[1], v[2]);
        raster.drawTriangleClip(v[0], v[2], v[3]);
    }
}

}  // namespace render
}  // namespace nova