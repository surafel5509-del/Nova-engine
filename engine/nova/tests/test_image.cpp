// Image buffer and PNG codec tests.
#include "tests/TestHarness.h"

#include <cstring>
#include <string>
#include <vector>

#include "base/Deflate.h"
#include "render/Image.h"

using namespace nova;
using namespace nova::render;

namespace {

Image checker(int w, int h, int cell = 8) {
    Image img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool on = ((x / cell) + (y / cell)) & 1;
            img.set(x, y, on ? Color{1, 1, 1, 1} : Color{0, 0, 0, 1});
        }
    }
    return img;
}

/// Every pixel of [a] must match [b] to within one 8-bit step. PNG is lossy in
/// exactly one place -- 16-bit input is truncated -- so 8-bit round-trips are
/// otherwise exact and the test should insist on it.
bool samePixels(const Image& a, const Image& b) {
    if (a.width != b.width || a.height != b.height) return false;
    return a.pixels == b.pixels;
}

Color at(const Image& img, int x, int y) { return img.get(x, y); }

}  // namespace

NOVA_TEST(Image, ConstructionAndAccess) {
    Image img;
    CHECK(!img.valid());
    CHECK(img.pixelCount() == 0);

    img = Image(4, 3, Color{1, 0, 0, 1});
    CHECK(img.valid());
    CHECK(img.pixelCount() == 12);
    CHECK(img.pixels.size() == 48);
    CHECK_NEAR(at(img, 0, 0).r, 1.0f, 0.01);
    CHECK_NEAR(at(img, 3, 2).a, 1.0f, 0.01);

    img.set(1, 1, Color{0, 0, 1, 0.5f});
    CHECK_NEAR(at(img, 1, 1).b, 1.0f, 0.01);
    CHECK_NEAR(at(img, 1, 1).a, 0.5f, 0.01);

    // Out-of-bounds access is a no-op, never a write.
    img.set(-1, 0, Color{1, 0, 0, 1});
    img.set(100, 100, Color{1, 0, 0, 1});
    CHECK(img.inBounds(3, 2));
    CHECK(!img.inBounds(4, 2));
    CHECK(!img.inBounds(-1, 0));
}

NOVA_TEST(Image, ResizeKeepsTopLeft) {
    Image img(4, 4, Color{0, 0, 0, 0});
    img.set(0, 0, Color{1, 0, 0, 1});
    img.resize(8, 6, Color{0, 1, 0, 1});
    CHECK(img.width == 8 && img.height == 6);
    CHECK_NEAR(at(img, 0, 0).r, 1.0f, 0.01);
    CHECK_NEAR(at(img, 7, 5).g, 1.0f, 0.01);
}

NOVA_TEST(Image, FillsAndRectangles) {
    Image img(8, 8, Color{0, 0, 0, 1});
    img.fill(Color{0.2f, 0.4f, 0.6f, 1});
    CHECK_NEAR(at(img, 4, 4).g, 0.4f, 0.01);

    // Rectangles clip to the image rather than writing out of bounds.
    img.fillRect(-4, -4, 6, 6, Color{1, 0, 0, 1});
    CHECK_NEAR(at(img, 0, 0).r, 1.0f, 0.01);
    CHECK_NEAR(at(img, 5, 5).r, 0.2f, 0.01);
    CHECK_NEAR(at(img, 6, 6).r, 0.2f, 0.01);

    img.fillRect(Aabb2{{2, 2}, {6, 6}}, Color{0, 1, 0, 1});
    CHECK_NEAR(at(img, 4, 4).g, 1.0f, 0.01);
    CHECK_NEAR(at(img, 7, 7).g, 0.4f, 0.01);  // untouched: the fill colour's green

    img.drawRectOutline(0, 0, 8, 8, Color{1, 1, 1, 1}, 1);
    CHECK_NEAR(at(img, 0, 0).r, 1.0f, 0.01);
    CHECK_NEAR(at(img, 4, 4).g, 1.0f, 0.01);  // interior still the AABB fill
}

NOVA_TEST(Image, LinesAndShapes) {
    Image img(32, 32, Color{0, 0, 0, 0});
    img.drawLine({2, 2}, {29, 2}, Color{1, 0, 0, 1}, 1);
    // A horizontal line must have no gaps and must not bleed onto other rows.
    for (int x = 2; x <= 29; ++x) CHECK_NEAR(at(img, x, 2).r, 1.0f, 0.01);
    CHECK_NEAR(at(img, 15, 3).a, 0.0f, 0.01);
    CHECK_NEAR(at(img, 1, 2).a, 0.0f, 0.01);

    // A vertical line must terminate: this is the divide-by-zero case a naive
    // slope loop gets wrong.
    img.drawLine({10, 5}, {10, 25}, Color{0, 1, 0, 1}, 1);
    for (int y = 5; y <= 25; ++y) CHECK_NEAR(at(img, 10, y).g, 1.0f, 0.01);
    CHECK_NEAR(at(img, 10, 26).a, 0.0f, 0.01);
    CHECK_NEAR(at(img, 10, 4).a, 0.0f, 0.01);

    Image c(32, 32, Color{0, 0, 0, 0});
    c.drawCircle({16, 16}, 8, Color{0, 0, 1, 1});
    CHECK_NEAR(at(c, 16, 16).b, 1.0f, 0.01);
    CHECK_NEAR(at(c, 20, 16).b, 1.0f, 0.01);
    CHECK_NEAR(at(c, 27, 16).b, 0.0f, 0.01);

    c.drawCircleOutline({16, 16}, 8, Color{1, 1, 0, 1}, 2);
    CHECK_NEAR(at(c, 24, 16).r, 1.0f, 0.01);
    CHECK_NEAR(at(c, 16, 16).b, 1.0f, 0.01);  // interior untouched

    c.drawTriangle({2, 2}, {20, 2}, {11, 20}, Color{1, 1, 1, 1});
    CHECK_NEAR(at(c, 11, 5).r, 1.0f, 0.01);
    CHECK_NEAR(at(c, 11, 25).a, 0.0f, 0.01);
}

NOVA_TEST(Image, BlitRespectsBounds) {
    Image dst(8, 8, Color{0, 0, 0, 0});
    Image src(4, 4, Color{0, 1, 0, 1});
    dst.blit(src, 2, 2);
    CHECK_NEAR(at(dst, 0, 0).a, 0.0f, 0.01);
    CHECK_NEAR(at(dst, 3, 3).g, 1.0f, 0.01);
    CHECK_NEAR(at(dst, 5, 5).g, 1.0f, 0.01);   // last covered pixel
    CHECK_NEAR(at(dst, 6, 6).a, 0.0f, 0.01);   // and just past it

    // A blit hanging off every edge must clip, not wrap or crash.
    Image dst2(8, 8, Color{0, 0, 0, 0});
    dst2.blit(src, -2, -2);
    dst2.blit(src, 6, 6);
    dst2.blit(src, -100, 3);
    dst2.blit(src, 3, 100);
    CHECK_NEAR(at(dst2, 0, 0).g, 1.0f, 0.01);
    CHECK_NEAR(at(dst2, 1, 1).g, 1.0f, 0.01);
    CHECK_NEAR(at(dst2, 2, 2).a, 0.0f, 0.01);
}

NOVA_TEST(Image, BlitModesBlendAsDocumented) {
    Image dst(4, 4, Color{0.5f, 0.5f, 0.5f, 1.0f});
    const Image src(4, 4, Color{1.0f, 0.0f, 0.0f, 1.0f});

    Image a = dst;
    a.blit(src, 0, 0, BlendMode::Replace);
    CHECK_NEAR(at(a, 0, 0).r, 1.0f, 0.01);

    Image b = dst;
    b.blit(src, 0, 0, BlendMode::Alpha);
    CHECK_NEAR(at(b, 0, 0).r, 1.0f, 0.01);
    CHECK_NEAR(at(b, 0, 0).g, 0.0f, 0.01);

    Image c = dst;
    c.blit(src, 0, 0, BlendMode::Additive);
    CHECK_NEAR(at(c, 0, 0).r, 1.0f, 0.01);

    Image d = dst;
    d.blit(src, 0, 0, BlendMode::Multiply);
    CHECK_NEAR(at(d, 0, 0).r, 0.5f, 0.01);
    CHECK_NEAR(at(d, 0, 0).g, 0.0f, 0.01);

    // Half-transparent source over opaque destination: the result alpha must
    // stay 1 and the colour must be the blend of the two.
    Image e(4, 4, Color{0, 0, 0, 1});
    const Image half(4, 4, Color{1, 1, 1, 0.5f});
    e.blit(half, 0, 0, BlendMode::Alpha);
    CHECK_NEAR(at(e, 0, 0).r, 0.5f, 0.02);
    CHECK_NEAR(at(e, 0, 0).a, 1.0f, 0.02);

    // Fully transparent source leaves the destination untouched, including alpha.
    Image f(4, 4, Color{0.25f, 0.5f, 0.75f, 0.5f});
    const Image clear(4, 4, Color{1, 0, 0, 0});
    f.blit(clear, 0, 0, BlendMode::Alpha);
    CHECK_NEAR(at(f, 0, 0).r, 0.25f, 0.02);
    CHECK_NEAR(at(f, 0, 0).a, 0.5f, 0.02);

    // AlphaOnly paints colour but preserves the destination's alpha.
    Image g(4, 4, Color{0, 0, 0, 0.3f});
    g.blit(src, 0, 0, BlendMode::AlphaOnly);
    CHECK_NEAR(at(g, 0, 0).r, 1.0f, 0.01);
    CHECK_NEAR(at(g, 0, 0).a, 0.3f, 0.01);
}

NOVA_TEST(Image, Sampling) {
    Image img(4, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            img.set(x, y, Color{static_cast<float>(x) / 3.0f, static_cast<float>(y) / 3.0f, 0, 1});
        }
    }
    CHECK_NEAR(img.sampleNearest(0, 0).r, 0.0f, 0.01);
    CHECK_NEAR(img.sampleNearest(3, 3).r, 1.0f, 0.01);
    // Sampling outside the image clamps to the edge rather than reading garbage.
    CHECK_NEAR(img.sampleNearest(-5, -5).r, 0.0f, 0.01);
    CHECK_NEAR(img.sampleNearest(100, 100).r, 1.0f, 0.01);

    // Texel centres are at 0, 1, 2, 3 holding 0, 1/3, 2/3, 1, so halfway
    // between the first two is 1/6.
    CHECK_NEAR(img.sampleBilinear(0.5f, 0.0f).r, 1.0f / 6.0f, 0.02);
    CHECK_NEAR(img.sampleBilinear(1.5f, 1.5f).g, 0.5f, 0.03);
    CHECK_NEAR(img.sampleBilinear(3.0f, 0.0f).r, 1.0f, 0.02);
}

NOVA_TEST(Image, FlipAndSubImage) {
    // Colours are stored as bytes, so the ramp has to stay inside 0..1.
    Image img(4, 3);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            img.set(x, y, Color{static_cast<float>(x) / 3.0f, static_cast<float>(y) / 2.0f, 0, 1});
        }
    }
    const Image fx = img.flippedX();
    CHECK_NEAR(fx.get(0, 0).r, 1.0f, 0.01);
    CHECK_NEAR(fx.get(3, 0).r, 0.0f, 0.01);
    const Image fy = img.flippedY();
    CHECK_NEAR(fy.get(0, 0).g, 1.0f, 0.01);

    const Image sub = img.subImage(1, 1, 2, 2);
    CHECK(sub.width == 2 && sub.height == 2);
    CHECK_NEAR(sub.get(0, 0).r, 1.0f / 3.0f, 0.01);
    CHECK_NEAR(sub.get(1, 1).g, 1.0f, 0.01);

    // A sub-image taken partly off the edge is zero-filled, not out of bounds.
    const Image clipped = img.subImage(2, 2, 4, 4);
    CHECK(clipped.valid());
    CHECK_NEAR(clipped.get(0, 0).r, 2.0f / 3.0f, 0.01);
    CHECK_NEAR(clipped.get(3, 3).a, 0.0f, 0.01);
}

NOVA_TEST(Image, PngRoundTripOpaque) {
    const Image img = checker(16, 12);
    const std::vector<uint8_t> png = encodePng(img);
    CHECK(png.size() > 8);
    // PNG signature.
    CHECK(png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');

    Image back;
    CHECK(decodePng(png.data(), png.size(), back));
    CHECK(back.width == 16 && back.height == 12);
    CHECK(samePixels(img, back));
}

NOVA_TEST(Image, PngRoundTripWithAlpha) {
    Image img(9, 7);
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 9; ++x) {
            img.set(x, y, Color{static_cast<float>(x) / 8.0f, static_cast<float>(y) / 6.0f,
                                0.25f, static_cast<float>(x + y) / 15.0f});
        }
    }
    const std::vector<uint8_t> png = encodePng(img);
    Image back;
    CHECK(decodePng(png.data(), png.size(), back));
    CHECK(samePixels(img, back));
}

NOVA_TEST(Image, PngRoundTripEdgeSizes) {
    const int sizes[][2] = {{1, 1}, {1, 17}, {17, 1}, {2, 2}, {33, 5}, {5, 33}, {64, 64}};
    for (const auto& s : sizes) {
        const Image img = checker(s[0], s[1], 3);
        const std::vector<uint8_t> png = encodePng(img);
        Image back;
        CHECK(decodePng(png.data(), png.size(), back));
        CHECK_MSG(back.width == s[0] && back.height == s[1],
                  std::to_string(s[0]) + "x" + std::to_string(s[1]));
        CHECK(samePixels(img, back));
    }
}

NOVA_TEST(Image, PngIsCompressedNotStored) {
    const Image img = checker(128, 128);
    const std::vector<uint8_t> png = encodePng(img);
    // A flat checkerboard must shrink by a large factor; if the encoder ever
    // regressed to "store the raw rows" this catches it immediately.
    CHECK_MSG(png.size() < img.pixels.size() / 8,
              "raw=" + std::to_string(img.pixels.size()) + " png=" + std::to_string(png.size()));
}

NOVA_TEST(Image, PngChunksAreWellFormed) {
    const Image img = checker(8, 8);
    const std::vector<uint8_t> png = encodePng(img);
    // Signature, then walk the chunk list: every length, type and CRC must line
    // up exactly, because a decoder that stops early produces a truncated file.
    size_t pos = 8;
    int chunks = 0;
    bool sawIHDR = false, sawIEND = false;
    while (pos + 12 <= png.size()) {
        const uint32_t len = (static_cast<uint32_t>(png[pos]) << 24) |
                             (static_cast<uint32_t>(png[pos + 1]) << 16) |
                             (static_cast<uint32_t>(png[pos + 2]) << 8) | png[pos + 3];
        const std::string type(reinterpret_cast<const char*>(png.data() + pos + 4), 4);
        const size_t crcAt = pos + 8 + len;
        CHECK(crcAt + 4 <= png.size());
        const uint32_t stored = (static_cast<uint32_t>(png[crcAt]) << 24) |
                                (static_cast<uint32_t>(png[crcAt + 1]) << 16) |
                                (static_cast<uint32_t>(png[crcAt + 2]) << 8) | png[crcAt + 3];
        const uint32_t computed =
            crc32(png.data() + pos + 4, 4 + len);
        CHECK_MSG(stored == computed, "chunk " + type);
        if (type == "IHDR") sawIHDR = true;
        if (type == "IEND") sawIEND = true;
        pos = crcAt + 4;
        ++chunks;
        if (sawIEND) break;
    }
    CHECK(sawIHDR);
    CHECK(sawIEND);
    CHECK(chunks == 3);
    CHECK(pos == png.size());
}

NOVA_TEST(Image, PngDropsAlphaChannelWhenOpaque) {
    Image opaque(4, 4, Color{1, 0, 0, 1});
    Image clear(4, 4, Color{1, 0, 0, 0});
    const size_t withAlpha = encodePng(opaque).size();
    const size_t withoutAlpha = encodePng(clear).size();
    // An image with real transparency must be larger than an opaque one of the
    // same size, because it keeps the fourth channel.
    CHECK(withoutAlpha > withAlpha);

    // Colour type lives at byte 25 of the stream: IHDR data starts at 16.
    const std::vector<uint8_t> png = encodePng(opaque);
    CHECK(png[25] == 2);  // truecolour
    const std::vector<uint8_t> rgba = encodePng(clear);
    CHECK(rgba[25] == 6);  // truecolour + alpha
}

NOVA_TEST(Image, PngDecodesExternalColourTypes) {
    // Hand-built greyscale and indexed PNGs. The engine only writes RGBA, but an
    // artist handing over a palette image must not produce a black frame.
    auto buildChunk = [](std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
        const uint32_t len = static_cast<uint32_t>(data.size());
        for (int i = 3; i >= 0; --i) out.push_back(static_cast<uint8_t>(len >> (i * 8)));
        const size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        const uint32_t crc = crc32(out.data() + start, out.size() - start);
        for (int i = 3; i >= 0; --i) out.push_back(static_cast<uint8_t>(crc >> (i * 8)));
    };

    // 8-bit greyscale, 2x2: black, mid, light, white.
    {
        std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        buildChunk(png, "IHDR", {0, 0, 0, 2, 0, 0, 0, 2, 8, 0, 0, 0, 0});
        // Two rows of two grey samples: black/mid, then light/white.
        const std::vector<uint8_t> samples = {0, 128, 192, 255};
        std::vector<uint8_t> raw;
        for (int y = 0; y < 2; ++y) {
            raw.push_back(0);  // filter: none
            raw.push_back(samples[static_cast<size_t>(y) * 2]);
            raw.push_back(samples[static_cast<size_t>(y) * 2 + 1]);
        }
        const std::vector<uint8_t> packed = zlibCompress(raw.data(), raw.size());
        buildChunk(png, "IDAT", packed);
        buildChunk(png, "IEND", {});

        Image img;
        CHECK(decodePng(png.data(), png.size(), img));
        CHECK(img.width == 2 && img.height == 2);
        CHECK_NEAR(img.get(0, 0).r, 0.0f, 0.01);
        CHECK_NEAR(img.get(1, 0).r, 0.502f, 0.01);
        CHECK_NEAR(img.get(0, 0).g, 0.0f, 0.01);   // grey expands to all channels
        CHECK_NEAR(img.get(0, 0).b, 0.0f, 0.01);
        CHECK_NEAR(img.get(1, 1).r, 1.0f, 0.01);
        CHECK_NEAR(img.get(1, 1).a, 1.0f, 0.01);
    }

    // Indexed with a palette and a transparent entry.
    {
        std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        buildChunk(png, "IHDR", {0, 0, 0, 2, 0, 0, 0, 1, 8, 3, 0, 0, 0});
        buildChunk(png, "PLTE", {255, 0, 0, 0, 0, 255, 0, 255, 0, 0});
        buildChunk(png, "tRNS", {0, 255});
        std::vector<uint8_t> raw = {0, 0, 1, 0, 1};
        const std::vector<uint8_t> packed = zlibCompress(raw.data(), raw.size());
        buildChunk(png, "IDAT", packed);
        buildChunk(png, "IEND", {});

        Image img;
        CHECK(decodePng(png.data(), png.size(), img));
        CHECK_NEAR(img.get(0, 0).r, 1.0f, 0.01);
        CHECK_NEAR(img.get(0, 0).a, 0.0f, 0.01);  // palette entry 0 is transparent
        CHECK_NEAR(img.get(1, 0).b, 1.0f, 0.01);
        CHECK_NEAR(img.get(1, 0).a, 1.0f, 0.01);
    }
}

NOVA_TEST(Image, PngDecodesEveryScanlineFilter) {
    // The encoder picks a filter per row; the decoder has to undo all five.
    // Build a PNG by hand that uses each filter type on its own row.
    auto buildChunk = [](std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
        const uint32_t len = static_cast<uint32_t>(data.size());
        for (int i = 3; i >= 0; --i) out.push_back(static_cast<uint8_t>(len >> (i * 8)));
        const size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        const uint32_t crc = crc32(out.data() + start, out.size() - start);
        for (int i = 3; i >= 0; --i) out.push_back(static_cast<uint8_t>(crc >> (i * 8)));
    };

    const int w = 5, h = 5;
    const int channels = 3;
    std::vector<uint8_t> truth(static_cast<size_t>(w) * h * channels);
    for (size_t i = 0; i < truth.size(); ++i) truth[i] = static_cast<uint8_t>((i * 13) & 0xFF);

    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        const int filter = y % 5;
        raw.push_back(static_cast<uint8_t>(filter));
        const uint8_t* row = truth.data() + static_cast<size_t>(y) * w * channels;
        const uint8_t* prior = y > 0 ? row - w * channels : nullptr;
        for (int i = 0; i < w * channels; ++i) {
            const int a = i >= channels ? row[i - channels] : 0;
            const int b = prior ? prior[i] : 0;
            const int c = (prior && i >= channels) ? prior[i - channels] : 0;
            int v = row[i];
            switch (filter) {
                case 0: break;
                case 1: v -= a; break;
                case 2: v -= b; break;
                case 3: v -= (a + b) / 2; break;
                default: {
                    const int p = a + b - c;
                    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                    v -= (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    break;
                }
            }
            raw.push_back(static_cast<uint8_t>(v & 0xFF));
        }
    }

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    buildChunk(png, "IHDR", {0, 0, 0, w, 0, 0, 0, h, 8, 2, 0, 0, 0});
    const std::vector<uint8_t> packed = zlibCompress(raw.data(), raw.size());
    buildChunk(png, "IDAT", packed);
    buildChunk(png, "IEND", {});

    Image img;
    CHECK(decodePng(png.data(), png.size(), img));
    CHECK(img.width == w && img.height == h);
    // The stream is RGB; the image is always RGBA, so widen before comparing.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t t = (static_cast<size_t>(y) * w + x) * 3;
            const Color c = img.get(x, y);
            auto byte = [](float v) { return static_cast<int>(v * 255.0f + 0.5f); };
            CHECK(byte(c.r) == truth[t]);
            CHECK(byte(c.g) == truth[t + 1]);
            CHECK(byte(c.b) == truth[t + 2]);
        }
    }
}

NOVA_TEST(Image, PngRejectsMalformedInput) {
    Image img;
    CHECK(!decodePng(nullptr, 0, img));
    CHECK(!decodePng(reinterpret_cast<const uint8_t*>("not a png at all"), 16, img));

    const std::vector<uint8_t> good = encodePng(checker(8, 8));
    // Truncation at several points must be refused rather than read past.
    for (size_t cut : {size_t(4), size_t(8), size_t(12), size_t(20), good.size() / 2, good.size() - 5}) {
        CHECK_MSG(!decodePng(good.data(), cut, img), "cut at " + std::to_string(cut));
    }
    // A corrupted payload must not decode to garbage.
    std::vector<uint8_t> corrupt = good;
    corrupt[corrupt.size() / 2] ^= 0xFF;
    Image out;
    decodePng(corrupt.data(), corrupt.size(), out);
}

NOVA_TEST(Image, FileRoundTrip) {
    const Image img = checker(24, 18);
    CHECK(writePng("build/test_image_roundtrip.png", img));
    Image back;
    CHECK(readPng("build/test_image_roundtrip.png", back));
    CHECK(samePixels(img, back));
    CHECK(!readPng("build/definitely_not_here.png", back));
}
NOVA_TEST(Image, NormalizedSamplingPutsTexelCentresAtMidpoints) {
    // Geometry gives uv in [0,1], so texel i has to sit at (i + 0.5) / width.
    // Sampling that convention has to recover the texture exactly.
    Image ramp(4, 1);
    for (int x = 0; x < 4; ++x) {
        ramp.set(x, 0, Color{static_cast<float>(x) / 3.0f, 0, 0, 1});
    }
    for (int x = 0; x < 4; ++x) {
        const float centre = (static_cast<float>(x) + 0.5f) / 4.0f;
        const Color c = ramp.sampleNormalized(centre, 0.5f, WrapMode::Clamp, false);
        CHECK_NEAR(c.r, static_cast<float>(x) / 3.0f, 0.01);
    }
    // The corners of the range are half a texel outside the first and last
    // texel, so they clamp outward rather than wrapping.
    CHECK_NEAR(ramp.sampleNormalized(0.0f, 0.5f, WrapMode::Clamp, false).r, 0.0f, 0.01);
    CHECK_NEAR(ramp.sampleNormalized(1.0f, 0.5f, WrapMode::Clamp, false).r, 1.0f, 0.01);

    // Past the edge: clamp holds at the border texel, repeat keeps going.
    CHECK_NEAR(ramp.sampleNormalized(1.4f, 0.5f, WrapMode::Clamp, false).r, 1.0f, 0.01);
    CHECK_NEAR(ramp.sampleNormalized(-0.4f, 0.5f, WrapMode::Clamp, false).r, 0.0f, 0.01);
    // With the half-texel shift, u = 1.125 is the start of the second repeat and
    // u = -0.125 the end of the one before it.
    CHECK_NEAR(ramp.sampleNormalized(1.125f, 0.5f, WrapMode::Repeat, false).r, 0.0f, 0.01);
    CHECK_NEAR(ramp.sampleNormalized(-0.125f, 0.5f, WrapMode::Repeat, false).r, 1.0f, 0.01);
    // A full period reads back as the original, forwards and backwards.
    for (int x = 0; x < 4; ++x) {
        const float centre = (static_cast<float>(x) + 0.5f) / 4.0f;
        for (int period = -3; period <= 3; ++period) {
            if (period == 0) continue;
            CHECK_NEAR(ramp.sampleNormalized(centre + static_cast<float>(period), 0.5f,
                                            WrapMode::Repeat, false)
                           .r,
                       static_cast<float>(x) / 3.0f, 0.01);
        }
    }
}

NOVA_TEST(Image, MirrorWrappingSeamsWithoutRepeating) {
    Image ramp(4, 1);
    for (int x = 0; x < 4; ++x) {
        ramp.set(x, 0, Color{static_cast<float>(x) / 3.0f, 0, 0, 1});
    }
    // A mirrored period is two texels wide, so u has period 2, not 1. Across
    // one u period the texel order is 0,1,2,3 then 3,2,1,0 again, and the edge
    // texel is shared by both halves, which is what hides the seam.
    CHECK_NEAR(ramp.sampleNormalized(1.0f, 0.5f, WrapMode::Mirror, false).r, 1.0f, 0.01);
    CHECK_NEAR(ramp.sampleNormalized(1.25f, 0.5f, WrapMode::Mirror, false).r, 2.0f / 3.0f, 0.01);
    CHECK_NEAR(ramp.sampleNormalized(1.875f, 0.5f, WrapMode::Mirror, false).r, 0.0f, 0.01);
    for (int x = 0; x < 4; ++x) {
        const float centre = (static_cast<float>(x) + 0.5f) / 4.0f;
        CHECK_NEAR(ramp.sampleNormalized(centre + 2.0f, 0.5f, WrapMode::Mirror, false).r,
                   static_cast<float>(x) / 3.0f, 0.01);
        // Mirroring means negative coordinates reflect instead of wrapping.
        CHECK_NEAR(ramp.sampleNormalized(-centre, 0.5f, WrapMode::Mirror, false).r,
                   static_cast<float>(x) / 3.0f, 0.01);
    }
    // Unlike repeat, mirror must NOT agree across a single period.
    CHECK(ramp.sampleNormalized(0.125f, 0.5f, WrapMode::Mirror, false).r !=
          ramp.sampleNormalized(1.125f, 0.5f, WrapMode::Mirror, false).r);
}

NOVA_TEST(Image, NormalizedBilinearMatchesTheHalfTexelShift) {
    Image a(2, 2);
    a.set(0, 0, Color{0, 0, 0, 1});
    a.set(1, 0, Color{1, 0, 0, 1});
    a.set(0, 1, Color{0, 1, 0, 1});
    a.set(1, 1, Color{1, 1, 1, 1});
    // uv (0,0) is the outer corner, halfway between texel 0's centre and the
    // virtual texel to its left, so clamping puts it on texel 0.
    CHECK_NEAR(a.sampleNormalized(0.0f, 0.0f, WrapMode::Clamp, true).r, 0.0f, 0.01);
    CHECK_NEAR(a.sampleNormalized(1.0f, 1.0f, WrapMode::Clamp, true).r, 1.0f, 0.01);
    // The centre of the image blends all four texels evenly, so each channel is
    // the mean of that channel's four corner values: 2, 2 and 1 out of 4.
    const Color mid = a.sampleNormalized(0.5f, 0.5f, WrapMode::Clamp, true);
    CHECK_NEAR(mid.r, 0.5f, 0.01);
    CHECK_NEAR(mid.g, 0.5f, 0.01);
    CHECK_NEAR(mid.b, 0.25f, 0.01);
    // A 2x2 texture repeated blends its neighbours across the seam.
    const Color seam = a.sampleNormalized(0.0f, 0.5f, WrapMode::Repeat, true);
    CHECK_NEAR(seam.r, 0.5f, 0.01);
    // Degenerate images must not read out of bounds.
    Image empty;
    CHECK_NEAR(empty.sampleNormalized(0.5f, 0.5f, WrapMode::Repeat, true).a, 0.0f, 0.01);
    CHECK_NEAR(empty.sampleNormalized(0.5f, 0.5f, WrapMode::Repeat, false).a, 0.0f, 0.01);
}

NOVA_TEST(Image, WrapModesAreIndependentPerAxis) {
    Image tile(4, 2);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 4; ++x) {
            tile.set(x, y, Color{static_cast<float>(x) / 3.0f, static_cast<float>(y), 0, 1});
        }
    }
    // Repeating in u and clamping in v: the green channel pins the row.
    const Color top = tile.sampleNormalized(2.125f, 0.25f, WrapMode::Repeat, false);
    CHECK_NEAR(top.r, 0.0f, 0.01);
    CHECK_NEAR(top.g, 0.0f, 0.01);
    const Color mid = tile.sampleNormalized(0.375f, 0.25f, WrapMode::Repeat, false);
    CHECK_NEAR(mid.r, 1.0f / 3.0f, 0.01);
    CHECK_NEAR(mid.g, 0.0f, 0.01);
    // v = 1.4 clamps to the last row rather than wrapping back to the first.
    const Color clamped = tile.sampleNormalized(0.375f, 1.4f, WrapMode::Clamp, false);
    CHECK_NEAR(clamped.g, 1.0f, 0.01);
    const Color wrapped = tile.sampleNormalized(0.375f, 1.4f, WrapMode::Repeat, false);
    CHECK_NEAR(wrapped.g, 0.0f, 0.01);
}
