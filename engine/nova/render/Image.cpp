#include "render/Image.h"

#include <cstdio>
#include <cstring>

#include "base/Deflate.h"
#include "base/FileSystem.h"

namespace nova {
namespace render {
namespace {

inline uint8_t to8(float v) { return static_cast<uint8_t>(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); }
// Byte -> float without a half-step offset. The +0.5 would push 0 up to 1/255,
// which then rounds straight back to 1 and makes an 8-bit round-trip lossy.
inline float from8(uint8_t v) { return static_cast<float>(v) / 255.0f; }

/// Combines one source texel into a destination texel.
inline void blend(uint8_t* dst, float sr, float sg, float sb, float sa, BlendMode mode) {
    const float dr = from8(dst[0]), dg = from8(dst[1]), db = from8(dst[2]), da = from8(dst[3]);
    switch (mode) {
        case BlendMode::Replace:
            dst[0] = to8(sr);
            dst[1] = to8(sg);
            dst[2] = to8(sb);
            dst[3] = to8(sa);
            return;
        case BlendMode::AlphaOnly:
            dst[0] = to8(sr);
            dst[1] = to8(sg);
            dst[2] = to8(sb);
            return;
        case BlendMode::Additive: {
            dst[0] = to8(dr + sr * sa);
            dst[1] = to8(dg + sg * sa);
            dst[2] = to8(db + sb * sa);
            dst[3] = to8(da + sa * (1.0f - da));
            return;
        }
        case BlendMode::Multiply: {
            dst[0] = to8(dr * sr);
            dst[1] = to8(dg * sg);
            dst[2] = to8(db * sb);
            dst[3] = to8(da * sa);
            return;
        }
        case BlendMode::Alpha:
        default: {
            // "over": the source sits on top of the destination.
            const float outA = sa + da * (1.0f - sa);
            if (outA <= 0.0f) {
                dst[0] = dst[1] = dst[2] = dst[3] = 0;
                return;
            }
            dst[0] = to8((sr * sa + dr * da * (1.0f - sa)) / outA);
            dst[1] = to8((sg * sa + dg * da * (1.0f - sa)) / outA);
            dst[2] = to8((sb * sa + db * da * (1.0f - sa)) / outA);
            dst[3] = to8(outA);
            return;
        }
    }
}

// --- PNG chunk plumbing ----------------------------------------------------

void appendChunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t size) {
    const uint32_t len = static_cast<uint32_t>(size);
    out.push_back(static_cast<uint8_t>(len >> 24));
    out.push_back(static_cast<uint8_t>(len >> 16));
    out.push_back(static_cast<uint8_t>(len >> 8));
    out.push_back(static_cast<uint8_t>(len));
    const size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    if (size) out.insert(out.end(), data, data + size);
    const uint32_t crc = crc32(out.data() + crcStart, out.size() - crcStart);
    out.push_back(static_cast<uint8_t>(crc >> 24));
    out.push_back(static_cast<uint8_t>(crc >> 16));
    out.push_back(static_cast<uint8_t>(crc >> 8));
    out.push_back(static_cast<uint8_t>(crc));
}

inline uint32_t readBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

/// Reverses the PNG per-scanline filters in place. [raw] holds
/// (1 + bytesPerPixel) bytes of filter tag plus data for every row.
bool unfilter(std::vector<uint8_t>& raw, int width, int height, int channels, int bitDepth) {
    const int bpp = std::max(1, channels * bitDepth / 8);
    const size_t stride = (static_cast<size_t>(width) * channels * bitDepth + 7) / 8;
    if (raw.size() < (stride + 1) * static_cast<size_t>(height)) return false;

    std::vector<uint8_t> out(stride * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        const uint8_t filter = raw[static_cast<size_t>(y) * (stride + 1)];
        const uint8_t* src = raw.data() + static_cast<size_t>(y) * (stride + 1) + 1;
        uint8_t* dst = out.data() + static_cast<size_t>(y) * stride;
        const uint8_t* prior = y > 0 ? out.data() + static_cast<size_t>(y - 1) * stride : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= static_cast<size_t>(bpp) ? dst[i - bpp] : 0;
            const int b = prior ? prior[i] : 0;
            const int c = (prior && i >= static_cast<size_t>(bpp)) ? prior[i - bpp] : 0;
            int v = src[i];
            switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: {
                    // Paeth predictor.
                    const int p = a + b - c;
                    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                    v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    break;
                }
                default: return false;  // unknown filter
            }
            dst[i] = static_cast<uint8_t>(v & 0xFF);
        }
    }
    raw.swap(out);
    return true;
}

inline int paethPredictor(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

}  // namespace

// --- Image ------------------------------------------------------------------

Image::Image(int w, int h, Color fill) {
    resize(w, h, fill);
}

void Image::resize(int w, int h, Color fill) {
    const int nw = std::max(0, w), nh = std::max(0, h);
    const size_t n = static_cast<size_t>(nw) * static_cast<size_t>(nh) * 4;
    if (nw == width && nh == height && pixels.size() == n) return;

    // Growing a framebuffer mid-frame must not erase what is already in it, so
    // the old content is carried across and only the newly exposed area is
    // filled. Clearing everything would silently blank the frame.
    const int keepW = std::min(width, nw), keepH = std::min(height, nh);
    std::vector<uint8_t> old;
    old.swap(pixels);
    width = nw;
    height = nh;
    pixels.assign(n, 0);
    if (n) this->fill(fill);

    const size_t dstStride = static_cast<size_t>(width) * 4;
    const size_t srcStride = static_cast<size_t>(std::max(0, static_cast<int>(old.size()) /
                                                           (std::max(1, height) * 4))) * 4;
    for (int y = 0; y < keepH; ++y) {
        std::memcpy(pixels.data() + static_cast<size_t>(y) * dstStride,
                    old.data() + static_cast<size_t>(y) * srcStride,
                    static_cast<size_t>(keepW) * 4);
    }
}

void Image::fill(Color c) {
    const uint8_t rgba[4] = {to8(c.r), to8(c.g), to8(c.b), to8(c.a)};
    for (size_t i = 0; i < pixelCount(); ++i) {
        std::memcpy(pixels.data() + i * 4, rgba, 4);
    }
}

void Image::fillVerticalGradient(Color top, Color bottom) {
    if (height <= 0) return;
    for (int y = 0; y < height; ++y) {
        const float t = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 0.0f;
        const Color c = top.lerpTo(bottom, t);
        const uint8_t rgba[4] = {to8(c.r), to8(c.g), to8(c.b), to8(c.a)};
        for (int x = 0; x < width; ++x) std::memcpy(at(x, y), rgba, 4);
    }
}

void Image::fillRect(int x, int y, int w, int h, Color c) {
    const int x0 = std::max(0, x), y0 = std::max(0, y);
    const int x1 = std::min(width, x + w), y1 = std::min(height, y + h);
    const uint8_t rgba[4] = {to8(c.r), to8(c.g), to8(c.b), to8(c.a)};
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) std::memcpy(at(px, py), rgba, 4);
    }
}

void Image::fillRect(const Aabb2& r, Color c) {
    fillRect(static_cast<int>(r.min.x), static_cast<int>(r.min.y),
             static_cast<int>(r.max.x - r.min.x), static_cast<int>(r.max.y - r.min.y), c);
}

void Image::drawRectOutline(int x, int y, int w, int h, Color c, int thickness) {
    const int t = std::max(1, thickness);
    fillRect(x, y, w, t, c);
    fillRect(x, y + h - t, w, t, c);
    fillRect(x, y, t, h, c);
    fillRect(x + w - t, y, t, h, c);
}

void Image::drawLine(Vec2 a, Vec2 b, Color c, int thickness) {
    // Integer DDA: exact, no gaps, and immune to the divide-by-zero a naive
    // slope-based loop hits on axis-aligned lines.
    const int half = std::max(0, thickness / 2);
    int x0 = static_cast<int>(std::lround(a.x)), y0 = static_cast<int>(std::lround(a.y));
    const int x1 = static_cast<int>(std::lround(b.x)), y1 = static_cast<int>(std::lround(b.y));
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 1 << 22; ++guard) {
        for (int oy = -half; oy <= half; ++oy) {
            for (int ox = -half; ox <= half; ++ox) {
                if (inBounds(x0 + ox, y0 + oy)) set(x0 + ox, y0 + oy, c);
            }
        }
        if (x0 == x1 && y0 == y1) break;
        const int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Image::drawCircle(Vec2 center, float radius, Color c) {
    const int r = static_cast<int>(std::ceil(radius));
    const int cx = static_cast<int>(std::lround(center.x));
    const int cy = static_cast<int>(std::lround(center.y));
    const float rr = radius * radius;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            const float d2 = static_cast<float>(dx * dx + dy * dy);
            if (d2 <= rr + radius) set(cx + dx, cy + dy, c);
        }
    }
}

void Image::drawCircleOutline(Vec2 center, float radius, Color c, int thickness) {
    const int r = static_cast<int>(std::ceil(radius));
    const int cx = static_cast<int>(std::lround(center.x));
    const int cy = static_cast<int>(std::lround(center.y));
    const float inner = std::max(0.0f, radius - thickness) * (radius - thickness);
    const float outer = (radius + 0.5f) * (radius + 0.5f);
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            const float d2 = static_cast<float>(dx * dx + dy * dy);
            if (d2 <= outer && d2 >= inner) set(cx + dx, cy + dy, c);
        }
    }
}

void Image::drawTriangle(Vec2 a, Vec2 b, Vec2 cc, Color c) {
    const float minX = std::min({a.x, b.x, cc.x}), maxX = std::max({a.x, b.x, cc.x});
    const float minY = std::min({a.y, b.y, cc.y}), maxY = std::max({a.y, b.y, cc.y});
    const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
    const int x1 = std::min(width - 1, static_cast<int>(std::ceil(maxX)));
    const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(maxY)));
    const float area = (b.x - a.x) * (cc.y - a.y) - (b.y - a.y) * (cc.x - a.x);
    if (std::fabs(area) < 1e-7f) return;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float w0 = ((b.x - px) * (cc.y - py) - (b.y - py) * (cc.x - px)) / area;
            const float w1 = ((cc.x - px) * (a.y - py) - (cc.y - py) * (a.x - px)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
            set(x, y, c);
        }
    }
}

void Image::blit(const Image& src, int dx, int dy, BlendMode mode) {
    blitSubRect(src, Aabb2{{0, 0}, {static_cast<float>(src.width), static_cast<float>(src.height)}},
                Aabb2{{static_cast<float>(dx), static_cast<float>(dy)},
                      {static_cast<float>(dx + src.width), static_cast<float>(dy + src.height)}},
                mode);
}

void Image::blitSubRect(const Image& src, const Aabb2& srcRect, const Aabb2& dst, BlendMode mode) {
    const int dw = static_cast<int>(std::lround(dst.max.x - dst.min.x));
    const int dh = static_cast<int>(std::lround(dst.max.y - dst.min.y));
    if (dw <= 0 || dh <= 0) return;
    const float sw = srcRect.max.x - srcRect.min.x;
    const float sh = srcRect.max.y - srcRect.min.y;
    if (sw <= 0.0f || sh <= 0.0f) return;

    for (int y = 0; y < dh; ++y) {
        const int ty = static_cast<int>(std::lround(dst.min.y)) + y;
        if (ty < 0 || ty >= height) continue;
        // Sample at the texel centre so a straight 1:1 blit is exact.
        const float v = (static_cast<float>(y) + 0.5f) * sh / static_cast<float>(dh) - 0.5f;
        for (int x = 0; x < dw; ++x) {
            const int tx = static_cast<int>(std::lround(dst.min.x)) + x;
            if (tx < 0 || tx >= width) continue;
            const float u = (static_cast<float>(x) + 0.5f) * sw / static_cast<float>(dw) - 0.5f;
            const Color s = src.sampleBilinear(u, v);
            blend(at(tx, ty), s.r, s.g, s.b, s.a, mode);
        }
    }
}

void Image::blitScaled(const Image& src, const Aabb2& dst, BlendMode mode) {
    blitSubRect(src, Aabb2{{0, 0}, {static_cast<float>(src.width), static_cast<float>(src.height)}},
                dst, mode);
}

Image Image::subImage(int x, int y, int w, int h) const {
    Image out(std::max(0, w), std::max(0, h));
    for (int j = 0; j < out.height; ++j) {
        const int sy = y + j;
        if (sy < 0 || sy >= height) continue;
        for (int i = 0; i < out.width; ++i) {
            const int sx = x + i;
            if (sx < 0 || sx >= width) continue;
            std::memcpy(out.at(i, j), at(sx, sy), 4);
        }
    }
    return out;
}

Image Image::flippedX() const {
    Image out(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) std::memcpy(out.at(width - 1 - x, y), at(x, y), 4);
    }
    return out;
}

Image Image::flippedY() const {
    Image out(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) std::memcpy(out.at(x, height - 1 - y), at(x, y), 4);
    }
    return out;
}

Color Image::sampleNearest(float x, float y) const {
    if (width <= 0 || height <= 0) return Color{0, 0, 0, 0};
    const int xi = clampi(static_cast<int>(std::lround(x)), 0, width - 1);
    const int yi = clampi(static_cast<int>(std::lround(y)), 0, height - 1);
    return get(xi, yi);
}

Color Image::sampleBilinear(float x, float y) const {
    if (width <= 0 || height <= 0) return Color{0, 0, 0, 0};
    const float fx = clampf(x, 0.0f, static_cast<float>(width - 1));
    const float fy = clampf(y, 0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float tx = fx - static_cast<float>(x0), ty = fy - static_cast<float>(y0);
    const Color c00 = get(x0, y0), c10 = get(x1, y0);
    const Color c01 = get(x0, y1), c11 = get(x1, y1);
    return c00.lerpTo(c10, tx).lerpTo(c01.lerpTo(c11, tx), ty);
}

// --- PNG --------------------------------------------------------------------

std::vector<uint8_t> encodePng(const Image& image) {
    std::vector<uint8_t> out;
    if (!image.valid()) return out;

    // Drop the alpha channel when nothing is transparent: one byte per pixel
    // less, and a quarter less to compress.
    bool opaque = true;
    for (size_t i = 3; i < image.pixels.size(); i += 4) {
        if (image.pixels[i] != 255) {
            opaque = false;
            break;
        }
    }
    const int channels = opaque ? 3 : 4;

    // Filter every scanline with all five filter types and keep whichever gives
    // the smallest sum of absolute differences. The spec calls this the
    // "minimum sum of absolute differences" heuristic and it is worth a large
    // chunk of the final size on flat art.
    const size_t stride = static_cast<size_t>(image.width) * static_cast<size_t>(channels);
    std::vector<uint8_t> filtered((stride + 1) * static_cast<size_t>(image.height));
    std::vector<uint8_t> current(stride), previous(stride, 0), candidate(stride), best(stride);

    // Pack the RGBA source down to the channel count the PNG will declare. The
    // filters all work on packed bytes, so this has to happen first -- reading
    // the source rows as if they were already packed is silently wrong.
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const uint8_t* src = image.at(x, y);
            uint8_t* dst = current.data() + static_cast<size_t>(x) * static_cast<size_t>(channels);
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            if (channels == 4) dst[3] = src[3];
        }

        // Filter every scanline with all five filter types and keep whichever
        // gives the smallest sum of absolute differences. This is the
        // heuristic the PNG spec recommends and it is worth a large chunk of the
        // final size on flat art.
        int bestFilter = 0;
        long bestScore = -1;
        const size_t bpp = static_cast<size_t>(channels);
        for (int f = 0; f < 5; ++f) {
            long score = 0;
            for (size_t i = 0; i < stride; ++i) {
                const int raw = current[i];
                const int a = i >= bpp ? current[i - bpp] : 0;
                const int b = previous[i];
                const int c = (i >= bpp) ? previous[i - bpp] : 0;
                int v;
                switch (f) {
                    case 0: v = raw; break;
                    case 1: v = raw - a; break;
                    case 2: v = raw - b; break;
                    case 3: v = raw - (a + b) / 2; break;
                    default: v = raw - paethPredictor(a, b, c); break;
                }
                candidate[i] = static_cast<uint8_t>(v & 0xFF);
                // Signed-magnitude sum, which is what actually predicts how well
                // the value compresses.
                score += candidate[i] < 128 ? candidate[i] : 256 - candidate[i];
            }
            if (bestScore < 0 || score < bestScore) {
                bestScore = score;
                bestFilter = f;
                best.swap(candidate);
            }
        }
        filtered[static_cast<size_t>(y) * (stride + 1)] = static_cast<uint8_t>(bestFilter);
        std::memcpy(&filtered[static_cast<size_t>(y) * (stride + 1) + 1], best.data(), stride);
        previous.swap(current);
    }

    const std::vector<uint8_t> packed = zlibCompress(filtered.data(), filtered.size());

    static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out.insert(out.end(), kSignature, kSignature + 8);

    uint8_t ihdr[13];
    const auto w32 = static_cast<uint32_t>(image.width);
    const auto h32 = static_cast<uint32_t>(image.height);
    ihdr[0] = static_cast<uint8_t>(w32 >> 24);
    ihdr[1] = static_cast<uint8_t>(w32 >> 16);
    ihdr[2] = static_cast<uint8_t>(w32 >> 8);
    ihdr[3] = static_cast<uint8_t>(w32);
    ihdr[4] = static_cast<uint8_t>(h32 >> 24);
    ihdr[5] = static_cast<uint8_t>(h32 >> 16);
    ihdr[6] = static_cast<uint8_t>(h32 >> 8);
    ihdr[7] = static_cast<uint8_t>(h32);
    ihdr[8] = 8;                                  // bit depth
    ihdr[9] = static_cast<uint8_t>(opaque ? 2 : 6);  // colour type
    ihdr[10] = 0;                                 // deflate
    ihdr[11] = 0;                                 // adaptive filtering
    ihdr[12] = 0;                                 // no interlace
    appendChunk(out, "IHDR", ihdr, sizeof(ihdr));
    appendChunk(out, "IDAT", packed.data(), packed.size());
    appendChunk(out, "IEND", nullptr, 0);
    return out;
}

bool decodePng(const uint8_t* data, size_t size, Image& out) {
    static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!data || size < 8 || std::memcmp(data, kSignature, 8) != 0) return false;

    size_t pos = 8;
    uint32_t width = 0, height = 0;
    int bitDepth = 8, colorType = 6, interlace = 0;
    std::vector<uint8_t> idat;
    std::vector<uint8_t> palette;
    std::vector<uint8_t> paletteAlpha;
    bool sawHeader = false;
    bool sawEnd = false;

    while (pos + 8 <= size) {
        const uint32_t len = readBE32(data + pos);
        if (pos + 12 + len > size) return false;
        const char* type = reinterpret_cast<const char*>(data + pos + 4);
        const uint8_t* payload = data + pos + 8;

        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len < 13) return false;
            width = readBE32(payload);
            height = readBE32(payload + 4);
            bitDepth = payload[8];
            colorType = payload[9];
            interlace = payload[12];
            sawHeader = true;
            // Interlaced and sub-byte images are rare enough that supporting
            // them is not worth the code; reject loudly instead of guessing.
            if (interlace != 0 || width == 0 || height == 0) return false;
            if (bitDepth != 8 && bitDepth != 16) return false;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(payload, payload + len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            if (colorType == 3) paletteAlpha.assign(payload, payload + len);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), payload, payload + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            sawEnd = true;
            break;
        }
        pos += 12 + len;
    }

    // A stream that never reached IEND was cut short. Decoding it anyway would
    // hand back a plausible-looking but truncated image.
    if (!sawHeader || !sawEnd || idat.empty()) return false;
    if (width > 16384 || height > 16384) return false;  // refuse absurd allocations

    int channels;
    switch (colorType) {
        case 0: channels = 1; break;  // greyscale
        case 2: channels = 3; break;  // truecolour
        case 3: channels = 1; break;  // indexed
        case 4: channels = 2; break;  // greyscale + alpha
        case 6: channels = 4; break;  // truecolour + alpha
        default: return false;
    }
    if (colorType == 3 && palette.size() < 3) return false;

    std::vector<uint8_t> raw = zlibDecompress(idat.data(), idat.size());
    if (raw.empty()) return false;
    if (!unfilter(raw, static_cast<int>(width), static_cast<int>(height), channels, bitDepth)) {
        return false;
    }

    const size_t stride = (static_cast<size_t>(width) * static_cast<size_t>(channels) *
                           static_cast<size_t>(bitDepth) + 7) / 8;
    const int sampleBytes = bitDepth / 8;

    out.resize(static_cast<int>(width), static_cast<int>(height));
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = raw.data() + static_cast<size_t>(y) * stride;
        for (uint32_t x = 0; x < width; ++x) {
            // Channels are [sampleBytes] apart, so the component index has to
            // be scaled or a 16-bit image reads the wrong bytes -- and the last
            // pixel reads past the row entirely.
            const uint8_t* s = row + static_cast<size_t>(x) *
                                          static_cast<size_t>(channels) *
                                          static_cast<size_t>(sampleBytes);
            auto component = [&](int index) { return from8(s[index * sampleBytes]); };
            Color c;
            switch (colorType) {
                case 0: c = {component(0), component(0), component(0), 1.0f}; break;
                case 2: c = {component(0), component(1), component(2), 1.0f}; break;
                case 3: {
                    const size_t index = s[0];
                    const size_t o = index * 3;
                    if (o + 2 >= palette.size()) return false;
                    c = {from8(palette[o]), from8(palette[o + 1]), from8(palette[o + 2]), 1.0f};
                    if (index < paletteAlpha.size()) c.a = from8(paletteAlpha[index]);
                    break;
                }
                case 4: c = {component(0), component(0), component(0), component(1)}; break;
                default: c = {component(0), component(1), component(2), component(3)}; break;
            }
            out.set(static_cast<int>(x), static_cast<int>(y), c);
        }
    }
    return true;
}

bool writePng(const std::string& path, const Image& image) {
    const std::vector<uint8_t> bytes = encodePng(image);
    if (bytes.empty()) return false;
    return defaultFs().writeBinary(path, bytes.data(), bytes.size());
}

bool readPng(const std::string& path, Image& out) {
    std::vector<uint8_t> bytes;
    if (!defaultFs().readBinary(path, bytes)) return false;
    return decodePng(bytes.data(), bytes.size(), out);
}

}  // namespace render
}  // namespace nova