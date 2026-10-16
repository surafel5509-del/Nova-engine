// Software rasterizer tests: clipping, culling, depth, interpolation, blending.
#include "tests/TestHarness.h"

#include <cmath>
#include <algorithm>
#include <vector>

#include "render/Rasterizer.h"

using namespace nova;
using namespace nova::render;

namespace {

/// A framebuffer plus a rasterizer already pointed at it, using an ortho
/// projection so world units map to pixels 1:1 with the origin at the centre.
struct Stage {
    Framebuffer fb;
    Rasterizer raster;

    explicit Stage(int w = 64, int h = 64, bool yUp = true) {
        fb.resize(w, h);
        fb.clear(Color{0, 0, 0, 1}, 1.0f);
        raster.setTarget(&fb);
        // NDC y maps to -1 at the bottom of the image; screen y increases
        // downward, so a world +y is a smaller row index.
        raster.setViewProjection(Mat4::ortho(-w * 0.5f, w * 0.5f, -h * 0.5f, h * 0.5f, -1000.0f,
                                             1000.0f));
        raster.setCull(CullMode::None);
        raster.setBlend(BlendMode::Replace);
        raster.setDepthTest(false);
        (void)yUp;
    }

    Image& image() { return fb.color; }
    const Image& image() const { return fb.color; }
};

Vertex flat(Vec3 p, Color c) { return Vertex::at(p, c); }

Color pixel(const Stage& s, int x, int y) { return s.image().get(x, y); }

/// How many pixels differ from [reference] by more than [tol] in any channel.
int differingPixels(const Image& a, const Image& b, float tol = 0.01f) {
    if (a.width != b.width || a.height != b.height) return a.pixelCount() + b.pixelCount();
    int n = 0;
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            const Color ca = a.get(x, y), cb = b.get(x, y);
            if (std::fabs(ca.r - cb.r) > tol || std::fabs(ca.g - cb.g) > tol ||
                std::fabs(ca.b - cb.b) > tol || std::fabs(ca.a - cb.a) > tol) {
                ++n;
            }
        }
    }
    return n;
}

}  // namespace

NOVA_TEST(Raster, FramebufferBasics) {
    Framebuffer fb;
    CHECK(!fb.valid());
    fb.resize(8, 4);
    CHECK(fb.valid());
    CHECK(fb.color.width == 8 && fb.color.height == 4);
    CHECK(fb.depth.size() == 32);

    fb.clear(Color{0.25f, 0.5f, 0.75f, 1.0f}, 0.5f);
    CHECK_NEAR(fb.color.get(0, 0).r, 0.25f, 0.01);
    CHECK_NEAR(fb.depthAt(7, 3), 0.5f, 1e-6);
    CHECK_NEAR(fb.depthAt(-1, 0), 0.0f, 1e-6);  // out of range reads are safe

    fb.clearColor(Color{0, 0, 0, 1});
    CHECK_NEAR(fb.depthAt(0, 0), 0.5f, 1e-6);  // colour clear leaves depth alone
    fb.clearDepth(0.1f);
    CHECK_NEAR(fb.depthAt(0, 0), 0.1f, 1e-6);
}

NOVA_TEST(Raster, DrawsTriangleWithGouraudInterpolation) {
    // The stage's ortho spans [-w/2, w/2], so these corners land on
    // screen (0,32), (32,32) and (16,0) exactly.
    Stage s(32, 32);
    s.raster.drawTriangleWorld(flat({-16, -16, 0}, Color{1, 0, 0, 1}),
                               flat({16, -16, 0}, Color{0, 1, 0, 1}),
                               flat({0, 16, 0}, Color{0, 0, 1, 1}));

    // Near each corner the matching channel dominates.
    CHECK(pixel(s, 4, 30).r > 0.55f);
    CHECK(pixel(s, 4, 30).g < 0.45f);
    CHECK(pixel(s, 28, 30).g > 0.55f);
    CHECK(pixel(s, 28, 30).r < 0.45f);
    CHECK(pixel(s, 16, 4).b > 0.55f);

    // The centroid is an even blend of all three.
    const Color mid = pixel(s, 16, 21);
    CHECK_NEAR(mid.r, 1.0f / 3.0f, 0.06);
    CHECK_NEAR(mid.g, 1.0f / 3.0f, 0.06);
    CHECK_NEAR(mid.b, 1.0f / 3.0f, 0.06);

    // Nothing outside the triangle was touched.
    CHECK_NEAR(pixel(s, 0, 0).r, 0.0f, 0.01);
    CHECK_NEAR(pixel(s, 31, 0).r, 0.0f, 0.01);
    CHECK_NEAR(pixel(s, 0, 10).g, 0.0f, 0.01);
    CHECK_NEAR(pixel(s, 31, 10).b, 0.0f, 0.01);
    CHECK(s.raster.stats().triangles == 1);
    CHECK(s.raster.stats().pixelsShaded > 200);
}

NOVA_TEST(Raster, TriangleIsFilledWithoutCracksOrDoubleDraw) {
    Stage s(40, 40);
    s.raster.drawTriangleWorld(flat({-15, -15, 0}, Color{1, 1, 1, 1}),
                               flat({15, -15, 0}, Color{1, 1, 1, 1}),
                               flat({0, 15, 0}, Color{1, 1, 1, 1}));

    // Half the bounding box is triangle, half is background, give or take a
    // one-pixel edge. Anything wildly off means the edge functions are wrong.
    int lit = 0;
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            if (pixel(s, x, y).r > 0.5f) ++lit;
        }
    }
    CHECK_MSG(lit > 380 && lit < 480, "lit=" + std::to_string(lit));
}

NOVA_TEST(Raster, BackfaceCulling) {
    Stage s(32, 32);
    const Vertex front[3] = {flat({-8, -8, 0}, Color{1, 0, 0, 1}), flat({8, -8, 0}, Color{1, 0, 0, 1}),
                             flat({0, 8, 0}, Color{1, 0, 0, 1})};
    const Vertex back[3] = {flat({0, 8, 0}, Color{0, 1, 0, 1}), flat({8, -8, 0}, Color{0, 1, 0, 1}),
                            flat({-8, -8, 0}, Color{0, 1, 0, 1})};

    s.raster.setCull(CullMode::None);
    s.raster.drawTriangleWorld(front[0], front[1], front[2]);
    s.raster.drawTriangleWorld(back[0], back[1], back[2]);
    // Both windings drawn: the back one overwrote the front one.
    CHECK(pixel(s, 16, 22).g > 0.5f);

    Stage t(32, 32);
    t.raster.setCull(CullMode::Back);
    t.raster.drawTriangleWorld(front[0], front[1], front[2]);
    t.raster.drawTriangleWorld(back[0], back[1], back[2]);
    // Back culled: only the front-facing one survives.
    CHECK(pixel(t, 16, 22).r > 0.5f);
    CHECK(pixel(t, 16, 22).g < 0.5f);
    CHECK(t.raster.stats().triangles == 1);
    CHECK(t.raster.stats().trianglesCulled == 1);

    Stage u(32, 32);
    u.raster.setCull(CullMode::Front);
    u.raster.drawTriangleWorld(front[0], front[1], front[2]);
    u.raster.drawTriangleWorld(back[0], back[1], back[2]);
    CHECK(pixel(u, 16, 22).g > 0.5f);
    CHECK(pixel(u, 16, 22).r < 0.5f);
}

NOVA_TEST(Raster, NearPlaneClippingDoesNotMirrorTheScreen) {
    // A triangle straddling the camera must be cut, not wrapped around. Without
    // the clip, the behind-camera vertex flips sign through the divide and the
    // triangle smears across the entire frame. Only a perspective projection
    // puts anything behind the near plane, so that is what this needs.
    Stage s(32, 32);
    s.raster.setCull(CullMode::None);
    s.raster.setViewProjection(Mat4::perspective(60.0f, 1.0f, 0.1f, 100.0f));
    // The two front vertices sit at NDC y = -0.5, which is row 24; the third is
    // behind the eye, so the clip turns the triangle into the wedge above that
    // edge. Without the clip the behind vertex would flip through the divide and
    // smear red downwards instead, filling rows the geometry never covered.
    s.raster.drawTriangleWorld(flat({-0.5f, -0.5774f, -2}, Color{1, 0, 0, 1}),
                               flat({0.5f, -0.5774f, -2}, Color{1, 0, 0, 1}),
                               flat({0.5f, 3.0f, 2}, Color{1, 0, 0, 1}));
    CHECK(s.raster.stats().trianglesClipped == 1);

    // Nothing below the front edge is covered.
    for (int y = 24; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) CHECK_NEAR(pixel(s, x, y).r, 0.0f, 0.01);
    }
    // The wedge above it is filled, and it widens as it goes: the right edge is
    // the line from the front-right vertex toward the vanishing point.
    CHECK_NEAR(pixel(s, 16, 23).r, 1.0f, 0.05);
    CHECK_NEAR(pixel(s, 16, 12).r, 1.0f, 0.05);
    int maxFilled = 0;
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 32; ++x) {
            if (pixel(s, x, y).r > 0.5f) maxFilled = std::max(maxFilled, x);
            // The left edge is the front-left vertex extended: nothing to its
            // left may ever be touched.
            CHECK_MSG(x >= 9 || pixel(s, x, y).r < 0.5f, "leaked left at row " + std::to_string(y));
        }
    }
    // The wedge reaches the right of the frame near the top, where the right
    // edge has run away from the front-right vertex.
    CHECK_MSG(maxFilled > 24, "maxFilled=" + std::to_string(maxFilled));
    CHECK(s.raster.stats().pixelsShaded > 200);
}

NOVA_TEST(Raster, FullyBehindCameraDrawsNothing) {
    Stage s(32, 32);
    s.raster.setViewProjection(Mat4::perspective(60.0f, 1.0f, 0.1f, 100.0f));
    s.raster.drawTriangleWorld(flat({-5, -5, 10}, Color{1, 0, 0, 1}),
                               flat({5, -5, 10}, Color{1, 0, 0, 1}),
                               flat({0, 5, 10}, Color{1, 0, 0, 1}));
    CHECK(s.raster.stats().triangles == 0);
    CHECK(s.raster.stats().pixelsShaded == 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) CHECK_NEAR(pixel(s, x, y).r, 0.0f, 0.01);
    }
}

NOVA_TEST(Raster, DepthBufferOrdersOverlappingTriangles) {
    // An orthographic camera, so a depth buffer keyed on 1/w would be constant
    // and every fragment would tie.
    Stage s(32, 32);
    s.raster.setDepthTest(true);
    s.raster.setDepthFunc(DepthFunc::Less);

    // With near = -1000 and far = 1000 on z, larger z is nearer here.
    s.raster.drawTriangleWorld(flat({-10, -10, -5}, Color{1, 0, 0, 1}),
                               flat({10, -10, -5}, Color{1, 0, 0, 1}),
                               flat({0, 10, -5}, Color{1, 0, 0, 1}));
    s.raster.drawTriangleWorld(flat({-10, -10, 5}, Color{0, 0, 1, 1}),
                               flat({10, -10, 5}, Color{0, 0, 1, 1}),
                               flat({0, 10, 5}, Color{0, 0, 1, 1}));
    CHECK(pixel(s, 16, 20).b > 0.5f);
    CHECK(pixel(s, 16, 20).r < 0.5f);
    // The written depth really is the near one, not the first one drawn.
    CHECK(s.raster.target()->depthAt(16, 20) < 0.51f);

    // With the depth test off, the last one drawn simply wins.
    Stage t(32, 32);
    t.raster.setDepthTest(false);
    t.raster.drawTriangleWorld(flat({-10, -10, 5}, Color{1, 0, 0, 1}),
                               flat({10, -10, 5}, Color{1, 0, 0, 1}),
                               flat({0, 10, 5}, Color{1, 0, 0, 1}));
    t.raster.drawTriangleWorld(flat({-10, -10, -5}, Color{0, 0, 1, 1}),
                               flat({10, -10, -5}, Color{0, 0, 1, 1}),
                               flat({0, 10, -5}, Color{0, 0, 1, 1}));
    CHECK(pixel(t, 16, 20).b > 0.5f);
}

NOVA_TEST(Raster, PerspectiveDepthIsNotLinearInZ) {
    Stage s(64, 64);
    s.raster.setDepthTest(true);
    // Two coplanar-in-XZ quads at different depths: the nearer must occlude.
    // A depth buffer keyed on world z instead of 1/w would still pass here, so
    // also check that a rotated quad keeps its depth ordering.
    s.raster.setViewProjection(Mat4::perspective(60.0f, 1.0f, 0.1f, 100.0f));
    s.raster.setCull(CullMode::None);
    Vertex near[4] = {
        Vertex::at({-1, -1, -3}, {0, 1, 0, 1}),
        Vertex::at({1, -1, -3}, {0, 1, 0, 1}),
        Vertex::at({1, 1, -3}, {0, 1, 0, 1}),
        Vertex::at({-1, 1, -3}, {0, 1, 0, 1}),
    };
    near[0].uv = {0, 0};
    near[1].uv = {1, 0};
    near[2].uv = {1, 1};
    near[3].uv = {0, 1};
    Vertex far = Vertex::at({0, 0, -10}, {1, 0, 0, 1});
    s.raster.drawTriangleWorld(far, far, far);
    s.raster.drawQuadWorld(near);
    CHECK_NEAR(pixel(s, 32, 32).g, 1.0f, 0.05);
    CHECK_NEAR(pixel(s, 32, 32).r, 0.0f, 0.05);
}

NOVA_TEST(Raster, PerspectiveCorrectTexturing) {
    // A checkerboard on a ground quad. Under affine interpolation the texel
    // density would visibly change across the surface; under the correct one
    // the pattern is regular all the way to the horizon.
    Image checker(8, 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const bool on = ((x / 2) + (y / 2)) & 1;
            checker.set(x, y, on ? Color{1, 1, 1, 1} : Color{0, 0, 0, 1});
        }
    }

    Vertex ground[4] = {
        Vertex::at({-4, 0, -2}, {1, 1, 1, 1}),
        Vertex::at({4, 0, -2}, {1, 1, 1, 1}),
        Vertex::at({4, 0, -14}, {1, 1, 1, 1}),
        Vertex::at({-4, 0, -14}, {1, 1, 1, 1}),
    };
    ground[0].uv = {0, 0};
    ground[1].uv = {1, 0};
    ground[2].uv = {1, 1};
    ground[3].uv = {0, 1};
    // The camera sits above the ground; with it on the plane the whole quad
    // would project onto the horizon line and there would be nothing to sample.
    const Mat4 view = Mat4::lookAt({0, 4, 2}, {0, 0, -6}, {0, 1, 0});
    const Mat4 proj = Mat4::perspective(70.0f, 1.0f, 0.1f, 100.0f);

    Stage textured(64, 64);
    textured.raster.setCull(CullMode::None);
    textured.raster.setViewProjection(proj * view);
    textured.raster.setTexture(&checker);
    // Tile it across the surface rather than stretching one copy.
    textured.raster.setTextureWrap(WrapMode::Repeat);
    textured.raster.drawQuadWorld(ground);

    Stage flat(64, 64);
    flat.raster.setCull(CullMode::None);
    flat.raster.setViewProjection(proj * view);
    flat.raster.drawQuadWorld(ground);

    CHECK_MSG(textured.raster.stats().pixelsShaded > 400,
              "shaded=" + std::to_string(textured.raster.stats().pixelsShaded));

    // Restrict the comparison to pixels the ground actually covers: the quad
    // does not fill the frame, and its silhouette would otherwise register as
    // a texture "transition" in the untextured control too.
    auto covered = [&](const Stage& s, int x, int y) { return s.image().get(x, y).r > 0.5f; };

    // The checker has to actually show up as alternating texels.
    int transitions = 0;
    int coveredPixels = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (!covered(flat, x, y)) continue;
            ++coveredPixels;
            if (x > 0 && covered(flat, x - 1, y) &&
                std::fabs(textured.image().get(x, y).r - textured.image().get(x - 1, y).r) > 0.2f) {
                ++transitions;
            }
        }
    }
    CHECK_MSG(coveredPixels > 600, "covered=" + std::to_string(coveredPixels));
    CHECK_MSG(transitions > 8, "transitions=" + std::to_string(transitions));

    // The untextured control is one flat colour, so nothing alternates inside it.
    int flatTransitions = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 1; x < 64; ++x) {
            if (covered(flat, x, y) && covered(flat, x - 1, y) &&
                std::fabs(flat.image().get(x, y).r - flat.image().get(x - 1, y).r) > 0.2f) {
                ++flatTransitions;
            }
        }
    }
    CHECK(flatTransitions == 0);

    // Perspective correction, not affine. Feed clip coordinates straight
    // through an identity view-projection so the screen positions are exact.
    // The triangle's corners land on screen at (-1,-1), (1/3,-1/3) and (1,1),
    // so the centroid is (1/9, -1/9), which lands on pixel (18, 20) of a 36x36
    // frame. The barycentrics of that pixel's centre are
    //   (0.40279, 0.25, 0.34721)
    // giving two very different answers for u:
    //   affine           = 0 + 0.25 + 0                   = 0.25
    //   perspective corr = (0.25/3) / (0.40279+0.25/3+0.34721) = 0.1
    // Sampling a 16x1 ramp there separates them by three whole texels.
    Image ramp(16, 1, Color{0, 0, 0, 1});
    for (int x = 0; x < 16; ++x) {
        ramp.set(x, 0, Color{static_cast<float>(x) / 15.0f, 0, 0, 1});
    }
    Stage skewed(36, 36);
    skewed.raster.setCull(CullMode::None);
    skewed.raster.setViewProjection(Mat4::identity());
    skewed.raster.setTexture(&ramp);
    skewed.raster.setTextureFiltering(false);
    Vertex sa = Vertex::at({-1, -1, -0.5f}, {1, 1, 1, 1});
    Vertex sb = Vertex::at({1, -1, -2.5f}, {1, 1, 1, 1});
    Vertex sc = Vertex::at({1, 1, -0.5f}, {1, 1, 1, 1});
    sa.position.w = 1.0f;
    sb.position.w = 3.0f;
    sc.position.w = 1.0f;
    sa.uv = {0, 0};
    sb.uv = {1, 0};
    sc.uv = {0, 0};
    skewed.raster.drawTriangleWorld(sa, sb, sc);

    const float atCentroid = skewed.image().get(18, 20).r;
    // Affine interpolation would have landed on 4/15 here.
    CHECK_MSG(atCentroid < 0.15f,
              "affine interpolation leaked through: u=" + std::to_string(atCentroid));
    CHECK_NEAR(atCentroid, 1.0f / 15.0f, 0.04);
}

NOVA_TEST(Raster, SpriteDrawIsOrientedAndTinted) {
    Stage s(64, 64);
    Image tex(2, 2, Color{1, 0, 0, 1});
    s.raster.drawSprite(tex, {0, 0}, {20, 10}, 0.0f, Color{1, 1, 1, 1});
    // Centred, half-width 10 and half-height 5, so the red area is exactly
    // rows 27..37 and columns 22..41.
    CHECK_NEAR(pixel(s, 32, 32).r, 1.0f, 0.05);
    CHECK_NEAR(pixel(s, 32, 28).r, 1.0f, 0.05);
    CHECK_NEAR(pixel(s, 32, 36).r, 1.0f, 0.05);
    CHECK_NEAR(pixel(s, 24, 32).r, 1.0f, 0.05);
    CHECK_NEAR(pixel(s, 40, 32).r, 1.0f, 0.05);
    // Just outside every edge the framebuffer keeps its cleared colour.
    CHECK_NEAR(pixel(s, 32, 26).r, 0.0f, 0.05);
    CHECK_NEAR(pixel(s, 32, 38).r, 0.0f, 0.05);
    CHECK_NEAR(pixel(s, 21, 32).r, 0.0f, 0.05);
    CHECK_NEAR(pixel(s, 42, 32).r, 0.0f, 0.05);

    // A tint multiplies the texture: a red texel under a cyan tint stays black,
    // because the tint has no red to contribute.
    Stage t(64, 64);
    t.raster.drawSprite(tex, {0, 0}, {20, 10}, 0.0f, Color{0, 1, 1, 1});
    CHECK_NEAR(pixel(t, 32, 32).r, 0.0f, 0.05);
    CHECK_NEAR(pixel(t, 32, 32).g, 0.0f, 0.05);
    Stage tinted(64, 64);
    Image blue(2, 2, Color{0, 0, 1, 1});
    tinted.raster.drawSprite(blue, {0, 0}, {20, 10}, 0.0f, Color{1, 1, 1, 1});
    CHECK_NEAR(pixel(tinted, 32, 32).b, 1.0f, 0.05);

    // Rotating by 90 degrees swaps the footprint.
    Stage u(64, 64);
    u.raster.drawSprite(tex, {0, 0}, {20, 10}, 90.0f, Color{1, 1, 1, 1});
    CHECK_NEAR(pixel(u, 32, 32).r, 1.0f, 0.05);
    CHECK_NEAR(pixel(u, 36, 32).r, 1.0f, 0.05);
    CHECK_NEAR(pixel(u, 28, 32).r, 1.0f, 0.05);
    // The half-extents swapped: short and wide, not tall and narrow.
    CHECK_NEAR(pixel(u, 32, 41).r, 1.0f, 0.05);
    CHECK_NEAR(pixel(u, 32, 43).r, 0.0f, 0.05);
    CHECK_NEAR(pixel(u, 25, 32).r, 0.0f, 0.05);
}

NOVA_TEST(Raster, SpriteOffscreenIsClippedNotCrashed) {
    Stage s(32, 32);
    Image tex(4, 4, Color{0, 1, 0, 1});
    s.raster.drawSprite(tex, {-100, -100}, {40, 40}, 0, Color{1, 1, 1, 1});
    s.raster.drawSprite(tex, {100, 100}, {40, 40}, 0, Color{1, 1, 1, 1});
    s.raster.drawSprite(tex, {16, 0}, {200, 200}, 37.0f, Color{1, 1, 1, 1});
    // The last one overlaps the centre, so something must be visible.
    CHECK_NEAR(pixel(s, 16, 16).g, 1.0f, 0.05);
}

NOVA_TEST(Raster, BlendModes) {
    Stage s(16, 16);
    s.image().fill(Color{0.5f, 0.5f, 0.5f, 1.0f});

    s.raster.setBlend(BlendMode::Replace);
    s.raster.drawTriangleWorld(flat({-8, -8, 0}, Color{1, 0, 0, 1}),
                               flat({8, -8, 0}, Color{1, 0, 0, 1}),
                               flat({0, 8, 0}, Color{1, 0, 0, 1}));
    CHECK_NEAR(pixel(s, 8, 10).r, 1.0f, 0.05);
    CHECK_NEAR(pixel(s, 8, 10).g, 0.0f, 0.05);

    Stage a(16, 16);
    a.image().fill(Color{0, 0, 0, 1});
    a.raster.setBlend(BlendMode::Alpha);
    a.raster.drawTriangleWorld(flat({-8, -8, 0}, Color{1, 1, 1, 0.5f}),
                               flat({8, -8, 0}, Color{1, 1, 1, 0.5f}),
                               flat({0, 8, 0}, Color{1, 1, 1, 0.5f}));
    CHECK_NEAR(pixel(a, 8, 10).r, 0.5f, 0.03);
    CHECK_NEAR(pixel(a, 8, 10).a, 1.0f, 0.03);

    Stage m(16, 16);
    m.image().fill(Color{0.5f, 0.5f, 0.5f, 1.0f});
    m.raster.setBlend(BlendMode::Multiply);
    m.raster.drawTriangleWorld(flat({-8, -8, 0}, Color{0.5f, 1, 1, 1}),
                               flat({8, -8, 0}, Color{0.5f, 1, 1, 1}),
                               flat({0, 8, 0}, Color{0.5f, 1, 1, 1}));
    CHECK_NEAR(pixel(m, 8, 10).r, 0.25f, 0.03);
    CHECK_NEAR(pixel(m, 8, 10).g, 0.5f, 0.03);

    Stage add(16, 16);
    add.image().fill(Color{0.25f, 0.25f, 0.25f, 1.0f});
    add.raster.setBlend(BlendMode::Additive);
    add.raster.drawTriangleWorld(flat({-8, -8, 0}, Color{0.25f, 0.25f, 0.25f, 1}),
                                 flat({8, -8, 0}, Color{0.25f, 0.25f, 0.25f, 1}),
                                 flat({0, 8, 0}, Color{0.25f, 0.25f, 0.25f, 1}));
    CHECK_NEAR(pixel(add, 8, 10).r, 0.5f, 0.03);
}

NOVA_TEST(Raster, ViewportRestrictsDrawing) {
    Stage s(64, 64);
    s.raster.setViewport(16, 16, 32, 32);
    s.raster.drawTriangleWorld(flat({-100, -100, 0}, Color{1, 1, 1, 1}),
                               flat({100, -100, 0}, Color{1, 1, 1, 1}),
                               flat({0, 100, 0}, Color{1, 1, 1, 1}));
    CHECK_NEAR(pixel(s, 32, 32).r, 1.0f, 0.05);
    CHECK_NEAR(pixel(s, 4, 4).r, 0.0f, 0.05);   // outside the viewport
    CHECK_NEAR(pixel(s, 60, 60).r, 0.0f, 0.05);
    CHECK_NEAR(pixel(s, 16, 16).r, 1.0f, 0.05);  // on its edge

    s.raster.resetViewport();
    Stage t(64, 64);
    t.raster.drawTriangleWorld(flat({-100, -100, 0}, Color{1, 1, 1, 1}),
                               flat({100, -100, 0}, Color{1, 1, 1, 1}),
                               flat({0, 100, 0}, Color{1, 1, 1, 1}));
    CHECK_NEAR(pixel(t, 4, 4).r, 1.0f, 0.05);
}

NOVA_TEST(Raster, TwoDOverlayUsesScreenPixels) {
    Stage s(64, 64);
    s.raster.drawTriangle2D({10, 10}, {50, 10}, {30, 50}, Color{0, 1, 0, 1});
    CHECK_NEAR(pixel(s, 30, 20).g, 1.0f, 0.05);
    CHECK_NEAR(pixel(s, 30, 40).g, 1.0f, 0.05);
    CHECK_NEAR(pixel(s, 5, 5).g, 0.0f, 0.05);
    CHECK_NEAR(pixel(s, 60, 60).g, 0.0f, 0.05);
}

NOVA_TEST(Raster, ProjectionToScreen) {
    Stage s(64, 64);
    float x = 0, y = 0;
    CHECK(s.raster.projectToScreen({0, 0, 0}, x, y));
    CHECK_NEAR(x, 32.0f, 0.5f);
    CHECK_NEAR(y, 32.0f, 0.5f);

    // +y in world is -y on screen.
    CHECK(s.raster.projectToScreen({0, 10, 0}, x, y));
    CHECK_NEAR(y, 22.0f, 0.5f);
    CHECK(s.raster.projectToScreen({10, 0, 0}, x, y));
    CHECK_NEAR(x, 42.0f, 0.5f);

    // A perspective camera looks down -z, so -1 is in front of it and +1 is not.
    Stage p(64, 64);
    p.raster.setViewProjection(Mat4::perspective(60.0f, 1.0f, 0.1f, 100.0f));
    CHECK(p.raster.projectToScreen({0, 0, -1}, x, y));
    CHECK_NEAR(x, 32.0f, 0.5f);
    CHECK_NEAR(y, 32.0f, 0.5f);
    CHECK(!p.raster.projectToScreen({0, 0, 1}, x, y));  // behind the view direction
    CHECK(!p.raster.projectToScreen({0, 0, 200}, x, y));  // beyond the far plane
}

NOVA_TEST(Raster, StatsTrackDrawCalls) {
    Stage s(32, 32);
    s.raster.resetStats();
    CHECK(s.raster.stats().drawCalls == 0);
    s.raster.drawTriangleWorld(flat({-8, -8, 0}, Color{1, 1, 1, 1}),
                               flat({8, -8, 0}, Color{1, 1, 1, 1}),
                               flat({0, 8, 0}, Color{1, 1, 1, 1}));
    CHECK(s.raster.stats().drawCalls == 1);
    CHECK(s.raster.stats().trianglesInput == 1);
    CHECK(s.raster.stats().triangles == 1);

    s.raster.resetStats();
    s.raster.drawTriangle2D({1, 1}, {10, 1}, {5, 10}, Color{1, 1, 1, 1});
    CHECK(s.raster.stats().drawCalls == 1);
}

NOVA_TEST(Raster, DegenerateTrianglesAreSafe) {
    Stage s(32, 32);
    // Zero area: three points on a line, two identical points, one NaN.
    s.raster.drawTriangleWorld(flat({0, 0, 0}, Color{1, 0, 0, 1}),
                               flat({10, 0, 0}, Color{1, 0, 0, 1}),
                               flat({20, 0, 0}, Color{1, 0, 0, 1}));
    s.raster.drawTriangleWorld(flat({5, 5, 0}, Color{1, 0, 0, 1}),
                               flat({5, 5, 0}, Color{1, 0, 0, 1}),
                               flat({5, 5, 0}, Color{1, 0, 0, 1}));
    const float nan = std::nanf("");
    s.raster.drawTriangleWorld(flat({nan, 0, 0}, Color{1, 0, 0, 1}),
                               flat({10, 10, 0}, Color{1, 0, 0, 1}),
                               flat({-10, 10, 0}, Color{1, 0, 0, 1}));
    // Nothing should have been rasterized, and nothing should have crashed.
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) CHECK_NEAR(pixel(s, x, y).r, 0.0f, 0.01);
    }
}

NOVA_TEST(Raster, NullTargetIsIgnored) {
    Rasterizer r;
    r.drawTriangleWorld(flat({0, 0, 0}, Color{1, 1, 1, 1}), flat({1, 0, 0}, Color{1, 1, 1, 1}),
                        flat({0, 1, 0}, Color{1, 1, 1, 1}));
    r.drawTriangle2D({0, 0}, {1, 0}, {0, 1}, Color{1, 1, 1, 1});
    r.drawSprite(Image(2, 2), {0, 0}, {4, 4}, 0, Color{1, 1, 1, 1});
    CHECK(r.stats().pixelsShaded == 0);
}

NOVA_TEST(Raster, RenderIsDeterministic) {
    auto render = []() {
        Stage s(48, 48);
        s.raster.setViewProjection(Mat4::perspective(60.0f, 1.0f, 0.1f, 100.0f));
        s.raster.setDepthTest(true);
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * 0.7f;
            Vertex v[4] = {
                Vertex::at({std::cos(a) * 5, std::sin(a) * 5, -4 - i}, {1, 0, 0, 1}),
                Vertex::at({std::cos(a) * 5 + 2, std::sin(a) * 5, -4 - i}, {0, 1, 0, 1}),
                Vertex::at({std::cos(a) * 5 + 2, std::sin(a) * 5 + 2, -4 - i}, {0, 0, 1, 1}),
                Vertex::at({std::cos(a) * 5, std::sin(a) * 5 + 2, -4 - i}, {1, 1, 0, 1}),
            };
            s.raster.drawQuadWorld(v);
        }
        return s.image();
    };
    const Image a = render();
    const Image b = render();
    CHECK(differingPixels(a, b) == 0);
}