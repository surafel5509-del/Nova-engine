// DEFLATE/zlib codec tests.
//
// Interoperability is the property that actually matters here: PNG files go to
// browsers and image tools that use a real zlib. So the suite carries reference
// streams produced by zlib itself and requires the engine's decoder to read
// them -- covering stored, fixed and dynamic Huffman blocks -- alongside
// round-trips of the engine's own encoder.
#include "tests/TestHarness.h"

#include <cstring>
#include <string>
#include <vector>

#include "base/Deflate.h"

namespace {

// a 200-byte run of 'A' followed by "tail" (dynamic Huffman).
const uint8_t kRunStream[] = {
    0x78, 0xda, 0x73, 0x74, 0x1c, 0x1e, 0xa0, 0x24, 0x31, 0x33, 0x07, 0x00,
    0xc0, 0xcd, 0x34, 0x73,
};
const uint8_t kRunStreamPlain[] = {
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x74, 0x61, 0x69, 0x6c,
};
constexpr size_t kRunStreamSize = sizeof(kRunStream);
constexpr size_t kRunStreamPlainSize = sizeof(kRunStreamPlain);

// 48 incompressible bytes (stored blocks).
const uint8_t kStoredStream[] = {
    0x78, 0x01, 0x01, 0x30, 0x00, 0xcf, 0xff, 0x78, 0x9b, 0x34, 0xca, 0xf5,
    0x4f, 0x2e, 0x22, 0x0a, 0xcd, 0x94, 0x1e, 0x71, 0xb8, 0x8d, 0x58, 0x36,
    0x86, 0x6d, 0x0d, 0x85, 0x8b, 0x63, 0x54, 0x9e, 0x94, 0xbe, 0x2c, 0xac,
    0xc6, 0x7f, 0x5b, 0x7e, 0xf2, 0x8f, 0x2d, 0x99, 0x03, 0x95, 0x9f, 0x63,
    0xd3, 0xd8, 0x93, 0xdc, 0xe7, 0x52, 0x77, 0x21, 0x75, 0x17, 0xcb,
};
const uint8_t kStoredStreamPlain[] = {
    0x78, 0x9b, 0x34, 0xca, 0xf5, 0x4f, 0x2e, 0x22, 0x0a, 0xcd, 0x94, 0x1e,
    0x71, 0xb8, 0x8d, 0x58, 0x36, 0x86, 0x6d, 0x0d, 0x85, 0x8b, 0x63, 0x54,
    0x9e, 0x94, 0xbe, 0x2c, 0xac, 0xc6, 0x7f, 0x5b, 0x7e, 0xf2, 0x8f, 0x2d,
    0x99, 0x03, 0x95, 0x9f, 0x63, 0xd3, 0xd8, 0x93, 0xdc, 0xe7, 0x52, 0x77,
};
constexpr size_t kStoredStreamSize = sizeof(kStoredStream);
constexpr size_t kStoredStreamPlainSize = sizeof(kStoredStreamPlain);

// a short repeating string.
const uint8_t kShortStream[] = {
    0x78, 0xda, 0x4b, 0x4c, 0x4a, 0x4e, 0x44, 0x45, 0x00, 0x41, 0x7c, 0x06,
    0xe5,
};
const uint8_t kShortStreamPlain[] = {
    0x61, 0x62, 0x63, 0x61, 0x62, 0x63, 0x61, 0x62, 0x63, 0x61, 0x62, 0x63,
    0x61, 0x62, 0x63, 0x61, 0x62, 0x63,
};
constexpr size_t kShortStreamSize = sizeof(kShortStream);
constexpr size_t kShortStreamPlainSize = sizeof(kShortStreamPlain);

// a repeated English sentence.
const uint8_t kTextStream[] = {
    0x78, 0xda, 0x0b, 0xc9, 0x48, 0x55, 0x28, 0x2c, 0xcd, 0x4c, 0xce, 0x56,
    0x48, 0x2a, 0xca, 0x2f, 0xcf, 0x53, 0x48, 0xcb, 0xaf, 0x50, 0xc8, 0x2a,
    0xcd, 0x2d, 0x28, 0x56, 0xc8, 0x2f, 0x4b, 0x2d, 0x52, 0x28, 0x01, 0x4a,
    0xe7, 0x24, 0x56, 0x55, 0x2a, 0xa4, 0xe4, 0xa7, 0xeb, 0x29, 0x84, 0x0c,
    0x41, 0xc5, 0x00, 0xd2, 0x8c, 0x50, 0xc4,
};
const uint8_t kTextStreamPlain[] = {
    0x54, 0x68, 0x65, 0x20, 0x71, 0x75, 0x69, 0x63, 0x6b, 0x20, 0x62, 0x72,
    0x6f, 0x77, 0x6e, 0x20, 0x66, 0x6f, 0x78, 0x20, 0x6a, 0x75, 0x6d, 0x70,
    0x73, 0x20, 0x6f, 0x76, 0x65, 0x72, 0x20, 0x74, 0x68, 0x65, 0x20, 0x6c,
    0x61, 0x7a, 0x79, 0x20, 0x64, 0x6f, 0x67, 0x2e, 0x20, 0x54, 0x68, 0x65,
    0x20, 0x71, 0x75, 0x69, 0x63, 0x6b, 0x20, 0x62, 0x72, 0x6f, 0x77, 0x6e,
    0x20, 0x66, 0x6f, 0x78, 0x20, 0x6a, 0x75, 0x6d, 0x70, 0x73, 0x20, 0x6f,
    0x76, 0x65, 0x72, 0x20, 0x74, 0x68, 0x65, 0x20, 0x6c, 0x61, 0x7a, 0x79,
    0x20, 0x64, 0x6f, 0x67, 0x2e, 0x20, 0x54, 0x68, 0x65, 0x20, 0x71, 0x75,
    0x69, 0x63, 0x6b, 0x20, 0x62, 0x72, 0x6f, 0x77, 0x6e, 0x20, 0x66, 0x6f,
    0x78, 0x20, 0x6a, 0x75, 0x6d, 0x70, 0x73, 0x20, 0x6f, 0x76, 0x65, 0x72,
    0x20, 0x74, 0x68, 0x65, 0x20, 0x6c, 0x61, 0x7a, 0x79, 0x20, 0x64, 0x6f,
    0x67, 0x2e, 0x20, 0x54, 0x68, 0x65, 0x20, 0x71, 0x75, 0x69, 0x63, 0x6b,
    0x20, 0x62, 0x72, 0x6f, 0x77, 0x6e, 0x20, 0x66, 0x6f, 0x78, 0x20, 0x6a,
    0x75, 0x6d, 0x70, 0x73, 0x20, 0x6f, 0x76, 0x65, 0x72, 0x20, 0x74, 0x68,
    0x65, 0x20, 0x6c, 0x61, 0x7a, 0x79, 0x20, 0x64, 0x6f, 0x67, 0x2e, 0x20,
    0x54, 0x68, 0x65, 0x20, 0x71, 0x75, 0x69, 0x63, 0x6b, 0x20, 0x62, 0x72,
    0x6f, 0x77, 0x6e, 0x20, 0x66, 0x6f, 0x78, 0x20, 0x6a, 0x75, 0x6d, 0x70,
    0x73, 0x20, 0x6f, 0x76, 0x65, 0x72, 0x20, 0x74, 0x68, 0x65, 0x20, 0x6c,
    0x61, 0x7a, 0x79, 0x20, 0x64, 0x6f, 0x67, 0x2e, 0x20,
};
constexpr size_t kTextStreamSize = sizeof(kTextStream);
constexpr size_t kTextStreamPlainSize = sizeof(kTextStreamPlain);


void roundTrip(const std::vector<uint8_t>& data) {
    const std::vector<uint8_t> packed = nova::zlibCompress(data.data(), data.size());
    const std::vector<uint8_t> back = nova::zlibDecompress(packed.data(), packed.size());
    CHECK(back.size() == data.size());
    // memcmp is declared non-null, so an empty buffer must not reach it.
    if (!data.empty() && back.size() == data.size()) {
        CHECK(std::memcmp(back.data(), data.data(), data.size()) == 0);
    }
}

std::vector<uint8_t> repeated(uint8_t value, size_t n) { return std::vector<uint8_t>(n, value); }

std::vector<uint8_t> gradient(size_t n) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>((i * 37 + (i >> 3)) & 0xFF);
    return v;
}

/// Something with the long-range structure of real screen content: flat bands
/// interrupted by a horizontal gradient.
std::vector<uint8_t> fakeFrame(size_t w, size_t h) {
    std::vector<uint8_t> v(w * h * 4);
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            const size_t i = (y * w + x) * 4;
            const bool band = ((y / 16) & 1) != 0;
            v[i + 0] = band ? 40 : 200;
            v[i + 1] = static_cast<uint8_t>((x * 255) / (w ? w : 1));
            v[i + 2] = static_cast<uint8_t>((y * 255) / (h ? h : 1));
            v[i + 3] = 255;
        }
    }
    return v;
}

std::vector<uint8_t> noise(size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t s = seed;
    for (auto& b : v) {
        s = s * 1664525u + 1013904223u;
        b = static_cast<uint8_t>(s >> 24);
    }
    return v;
}

}  // namespace

NOVA_TEST(Deflate, ChecksumsMatchKnownVectors) {
    // Adler-32 and CRC-32 have published test vectors; a codec whose checksums
    // are wrong produces files that every other tool silently rejects.
    const uint8_t wiki[] = {'W', 'i', 'k', 'i', 'p', 'e', 'd', 'i', 'a'};
    CHECK(nova::adler32(wiki, sizeof(wiki)) == 0x11E60398u);
    const uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(nova::crc32(digits, sizeof(digits)) == 0xCBF43926u);
    const uint8_t one[1] = {0};
    CHECK(nova::crc32(one, 0) == 0u);      // nothing hashed is the identity
    CHECK(nova::adler32(one, 0) == 1u);   // ...and Adler's seed is 1

    // The running form has to match the one-shot form.
    const std::vector<uint8_t> data = gradient(5000);
    uint32_t a = 1, c = 0;
    for (size_t i = 0; i < data.size(); i += 97) {
        const size_t n = std::min<size_t>(97, data.size() - i);
        a = nova::adler32(data.data() + i, n, a);
        c = nova::crc32(data.data() + i, n, c);
    }
    CHECK(a == nova::adler32(data.data(), data.size()));
    CHECK(c == nova::crc32(data.data(), data.size()));
}

NOVA_TEST(Deflate, DecodesRealZlibStreams) {
    // Each of these was produced by zlib; between them they cover all three
    // block types the format allows.
    const struct {
        const uint8_t* packed;
        size_t packedSize;
        const uint8_t* plain;
        size_t plainSize;
        const char* what;
    } cases[] = {
        {kRunStream, kRunStreamSize, kRunStreamPlain, kRunStreamPlainSize, "dynamic Huffman"},
        {kStoredStream, kStoredStreamSize, kStoredStreamPlain, kStoredStreamPlainSize, "stored"},
        {kShortStream, kShortStreamSize, kShortStreamPlain, kShortStreamPlainSize, "short"},
        {kTextStream, kTextStreamSize, kTextStreamPlain, kTextStreamPlainSize, "text"},
    };
    for (const auto& c : cases) {
        const std::vector<uint8_t> back =
            nova::zlibDecompress(c.packed, c.packedSize, c.plainSize);
        CHECK_MSG(back.size() == c.plainSize,
                  std::string(c.what) + ": got " + std::to_string(back.size()) + " want " +
                      std::to_string(c.plainSize));
        if (back.size() == c.plainSize) {
            CHECK(std::memcmp(back.data(), c.plain, c.plainSize) == 0);
        }
    }
}

NOVA_TEST(Deflate, EmptyAndTinyInputs) {
    roundTrip({});
    roundTrip({42});
    roundTrip({1, 2, 3});
    // One byte and two bytes are the degenerate lengths for the match finder.
    const std::vector<uint8_t> tiny = {0, 255, 0, 255, 1};
    roundTrip(tiny);
}

NOVA_TEST(Deflate, HighlyCompressibleInput) {
    const std::vector<uint8_t> run = repeated(0x41, 100000);
    roundTrip(run);
    // A long run forces the encoder to emit length/distance pairs rather than
    // literals, so check it really shrank rather than merely round-tripping.
    const std::vector<uint8_t> packed = nova::zlibCompress(run.data(), run.size());
    CHECK(packed.size() < 2000);
}

NOVA_TEST(Deflate, IncompressibleInput) {
    const std::vector<uint8_t> n = noise(50000, 7);
    roundTrip(n);
    // Nothing to match means every byte costs ~9 bits, so the output is larger
    // than the input. That is expected, not a defect.
    const std::vector<uint8_t> packed = nova::zlibCompress(n.data(), n.size());
    CHECK(packed.size() > n.size());
}

NOVA_TEST(Deflate, StructuredAndMixedData) {
    roundTrip(gradient(70000));
    roundTrip(fakeFrame(320, 200));
    roundTrip(fakeFrame(641, 379));  // odd widths exercise the scan tail
    // A buffer that alternates between runs and noise defeats greedy matching;
    // this is where a naive LZ77 falls over.
    std::vector<uint8_t> mixed;
    for (int i = 0; i < 40; ++i) {
        const std::vector<uint8_t> chunk = (i & 1) ? repeated(static_cast<uint8_t>(i), 900)
                                                   : noise(900, static_cast<uint32_t>(i));
        mixed.insert(mixed.end(), chunk.begin(), chunk.end());
    }
    roundTrip(mixed);
}

NOVA_TEST(Deflate, MatchAtExactWindowBoundary) {
    // A distance of exactly 32768 is the largest DEFLATE can encode; one more
    // must fall back to literals rather than emitting an invalid code.
    const std::vector<uint8_t> data = gradient(40000);
    roundTrip(data);
    std::vector<uint8_t> boundary(70000);
    for (size_t i = 0; i < boundary.size(); ++i) {
        boundary[i] = static_cast<uint8_t>((i / 70000) * 31 + (i % 251));
    }
    roundTrip(boundary);
}

NOVA_TEST(Deflate, MaximumLengthMatch) {
    // 258 is the longest match the format can encode, so a run far longer than
    // that has to be split across several pairs.
    roundTrip(repeated(0x5A, 100000));
}

NOVA_TEST(Deflate, MalformedInputIsRejectedNotCrashed) {
    // The contract for anything malformed is "empty vector" -- never a crash
    // and never partially decoded output.
    const uint8_t junk[] = {0x78, 0x9C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(nova::zlibDecompress(junk, sizeof(junk)).empty());

    const uint8_t truncated[] = {0x78, 0x9C, 0x63, 0x00};
    CHECK(nova::zlibDecompress(truncated, sizeof(truncated)).empty());

    // Reserved block type 3.
    const uint8_t reserved[] = {0x78, 0x9C, 0x07, 0x00, 0x00, 0x00, 0x00};
    CHECK(nova::zlibDecompress(reserved, sizeof(reserved)).empty());

    // Wrong compression method and a bad header check byte.
    const uint8_t badMethod[] = {0x79, 0x9C, 0x00, 0x00, 0x00, 0x00};
    CHECK(nova::zlibDecompress(badMethod, sizeof(badMethod)).empty());
    const uint8_t badCheck[] = {0x78, 0x9D, 0x00, 0x00, 0x00, 0x00};
    CHECK(nova::zlibDecompress(badCheck, sizeof(badCheck)).empty());

    CHECK(nova::zlibDecompress(nullptr, 0).empty());
    CHECK(nova::zlibDecompress(junk, 0).empty());

    // Half a valid stream must not resurrect partial output.
    const std::vector<uint8_t> good = nova::zlibCompress(repeated(7, 5000).data(), 5000);
    std::vector<uint8_t> cut(good.begin(), good.begin() + good.size() / 2);
    CHECK(nova::zlibDecompress(cut.data(), cut.size()).empty());

    // A back-reference that points before the start of the output.
    const uint8_t dangling[] = {0x78, 0x9C, 0x63, 0x60, 0x00, 0x00, 0x00, 0x00};
    CHECK(nova::zlibDecompress(dangling, sizeof(dangling)).empty());
}

NOVA_TEST(Deflate, CorruptChecksumIsCaught) {
    std::vector<uint8_t> packed = nova::zlibCompress(gradient(2000).data(), 2000);
    packed[packed.size() - 1] ^= 0xFF;
    CHECK(nova::zlibDecompress(packed.data(), packed.size()).empty());

    // Flipping a payload bit must be caught by the Adler-32 trailer.
    packed = nova::zlibCompress(gradient(2000).data(), 2000);
    packed[packed.size() / 2] ^= 0x01;
    CHECK(nova::zlibDecompress(packed.data(), packed.size()).empty());
}

NOVA_TEST(Deflate, CompressionRatioIsUseful) {
    const std::vector<uint8_t> frame = fakeFrame(640, 360);
    const std::vector<uint8_t> packed = nova::zlibCompress(frame.data(), frame.size());
    // Flat UI-ish content must compress meaningfully. Storing it raw would mean
    // streaming ~1 MB per frame, which defeats the point of the codec.
    CHECK_MSG(packed.size() * 4 < frame.size(),
              "raw=" + std::to_string(frame.size()) + " packed=" + std::to_string(packed.size()));
}

NOVA_TEST(Deflate, DeterministicOutput) {
    const std::vector<uint8_t> frame = fakeFrame(200, 150);
    const std::vector<uint8_t> a = nova::zlibCompress(frame.data(), frame.size());
    const std::vector<uint8_t> b = nova::zlibCompress(frame.data(), frame.size());
    CHECK(a == b);
}
