#include "base/Deflate.h"

#include <cstring>

namespace nova {
namespace {

// --- checksums -------------------------------------------------------------

uint32_t g_crcTable[256];
bool g_crcTableReady = false;

void initCrcTable() {
    if (g_crcTableReady) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crcTable[i] = c;
    }
    g_crcTableReady = true;
}

// --- DEFLATE tables (RFC 1951 section 3.2.5) --------------------------------

constexpr int kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr int kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr int kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                              33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                              1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
constexpr int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                               6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/// Fixed literal/length code for [symbol], returned as (code, bit length).
void fixedLitCode(int symbol, uint32_t& code, int& bits) {
    if (symbol < 144) {
        code = 0x30u + static_cast<uint32_t>(symbol);
        bits = 8;
    } else if (symbol < 256) {
        code = 0x190u + static_cast<uint32_t>(symbol - 144);
        bits = 9;
    } else if (symbol < 280) {
        code = static_cast<uint32_t>(symbol - 256);
        bits = 7;
    } else {
        code = 0xC0u + static_cast<uint32_t>(symbol - 280);
        bits = 8;
    }
}

int lengthCode(int length) {
    for (int i = 28; i >= 0; --i) {
        if (length >= kLengthBase[i]) return i;
    }
    return 0;
}

int distanceCode(int distance) {
    for (int i = 29; i >= 0; --i) {
        if (distance >= kDistBase[i]) return i;
    }
    return 0;
}

// --- bit writer -------------------------------------------------------------

/// DEFLATE packs bits least-significant-first within each byte, but Huffman
/// codes are stored most-significant-bit-first. The writer handles both.
class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}

    void bits(uint32_t value, int count) {
        for (int i = 0; i < count; ++i) {
            buffer_ |= static_cast<uint32_t>((value >> i) & 1u) << bitCount_;
            if (++bitCount_ == 8) {
                out_.push_back(static_cast<uint8_t>(buffer_));
                buffer_ = 0;
                bitCount_ = 0;
            }
        }
    }

    void code(uint32_t value, int count) {
        // Huffman codes go out most-significant bit first.
        for (int i = count - 1; i >= 0; --i) bits((value >> i) & 1u, 1);
    }

    void flush() {
        if (bitCount_ > 0) {
            out_.push_back(static_cast<uint8_t>(buffer_));
            buffer_ = 0;
            bitCount_ = 0;
        }
    }

private:
    std::vector<uint8_t>& out_;
    uint32_t buffer_ = 0;
    int bitCount_ = 0;
};

// --- bit reader -------------------------------------------------------------

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool bits(int count, uint32_t& out) {
        uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            if (bitCount_ == 0) {
                if (pos_ >= size_) return false;
                bitBuffer_ = data_[pos_++];
                bitCount_ = 8;
            }
            value |= static_cast<uint32_t>(bitBuffer_ & 1u) << i;
            bitBuffer_ >>= 1;
            --bitCount_;
        }
        out = value;
        return true;
    }

    /// Huffman codes arrive most-significant bit first.
    bool decode(uint32_t count, int& out) {
        uint32_t value = 0;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t bit;
            if (!bits(1, bit)) return false;
            value = (value << 1) | bit;
        }
        out = static_cast<int>(value);
        return true;
    }

    void alignToByte() { bitCount_ = 0; }

    bool readBytes(uint8_t* dst, size_t n) {
        if (pos_ + n > size_) return false;
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    bool skip(size_t n) {
        if (pos_ + n > size_) return false;
        pos_ += n;
        return true;
    }

    size_t position() const { return pos_; }
    bool exhausted() const { return pos_ >= size_ && bitCount_ == 0; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    uint32_t bitBuffer_ = 0;
    int bitCount_ = 0;
};

/// Canonical Huffman decoding table built from a code-length vector.
struct HuffmanTable {
    std::vector<uint16_t> counts;   ///< codes of each bit length
    std::vector<uint16_t> symbols;  ///< symbols ordered by code

    bool build(const uint8_t* lengths, int n) {
        counts.assign(16, 0);
        for (int i = 0; i < n; ++i) counts[lengths[i]]++;
        counts[0] = 0;
        // Reject over-subscribed sets; an incomplete set is legal (a distance
        // tree with a single code), an over-subscribed one is corrupt.
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= counts[len];
            if (left < 0) return false;
        }
        std::vector<uint16_t> offsets(16, 0);
        for (int len = 1; len < 15; ++len) offsets[len + 1] = offsets[len] + counts[len];
        symbols.assign(static_cast<size_t>(n), 0);
        for (int i = 0; i < n; ++i) {
            if (lengths[i]) symbols[offsets[lengths[i]]++] = static_cast<uint16_t>(i);
        }
        return true;
    }

    bool decode(BitReader& in, int& out) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            uint32_t bit;
            if (!in.bits(1, bit)) return false;
            code |= static_cast<int>(bit);
            const int count = counts[len];
            if (code - first < count) {
                out = symbols[index + (code - first)];
                return true;
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return false;
    }
};

bool inflateBlock(BitReader& in, const HuffmanTable& lit, const HuffmanTable& dist,
                  std::vector<uint8_t>& out) {
    for (;;) {
        int symbol;
        if (!lit.decode(in, symbol)) return false;
        if (symbol < 256) {
            out.push_back(static_cast<uint8_t>(symbol));
            continue;
        }
        if (symbol == 256) return true;  // end of block
        symbol -= 257;
        if (symbol >= 29) return false;
        uint32_t extra = 0;
        if (kLengthExtra[symbol] && !in.bits(kLengthExtra[symbol], extra)) return false;
        const int length = kLengthBase[symbol] + static_cast<int>(extra);

        int dsym;
        if (!dist.decode(in, dsym)) return false;
        if (dsym >= 30) return false;
        uint32_t dextra = 0;
        if (kDistExtra[dsym] && !in.bits(kDistExtra[dsym], dextra)) return false;
        const size_t distance = static_cast<size_t>(kDistBase[dsym]) + dextra;
        if (distance > out.size()) return false;

        // Byte-at-a-time so overlapping copies (run-length runs) work.
        size_t from = out.size() - distance;
        for (int i = 0; i < length; ++i) out.push_back(out[from + static_cast<size_t>(i)]);
    }
}

constexpr int kWindowBits = 15;
constexpr int kWindowSize = 1 << kWindowBits;
constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;
constexpr int kHashBits = 15;
constexpr int kHashSize = 1 << kHashBits;
/// Cap on the chain walk. Matches get marginally better with a longer chain and
/// massively slower; 32 is the usual sweet spot.
constexpr int kMaxChain = 32;

inline uint32_t hash3(const uint8_t* p) {
    return ((static_cast<uint32_t>(p[0]) << 10) ^ (static_cast<uint32_t>(p[1]) << 5) ^
            static_cast<uint32_t>(p[2])) &
           (kHashSize - 1);
}

}  // namespace

uint32_t adler32(const uint8_t* data, size_t size, uint32_t seed) {
    if (size == 0) return seed;  // Adler-32 of nothing is its own seed
    uint32_t a = seed & 0xFFFFu, b = (seed >> 16) & 0xFFFFu;
    // 5552 is the largest run that cannot overflow the 32-bit accumulators.
    constexpr size_t kChunk = 5552;
    while (size > 0) {
        const size_t n = size < kChunk ? size : kChunk;
        for (size_t i = 0; i < n; ++i) {
            a += data[i];
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
        data += n;
        size -= n;
    }
    return (b << 16) | a;
}

uint32_t crc32(const uint8_t* data, size_t size, uint32_t seed) {
    if (size == 0) return 0;
    initCrcTable();
    uint32_t c = seed ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) c = g_crcTable[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> deflateRaw(const uint8_t* data, size_t size) {
    std::vector<uint8_t> out;
    out.reserve(size / 2 + 16);
    BitWriter writer(out);

    // One final block, fixed Huffman.
    writer.bits(1, 1);  // BFINAL
    writer.bits(1, 2);  // BTYPE = 01, fixed Huffman

    std::vector<int32_t> head(kHashSize, -1);
    std::vector<int32_t> prev(size, -1);

    size_t i = 0;
    while (i < size) {
        size_t bestLen = 0, bestDist = 0;
        if (i + kMinMatch <= size) {
            const uint32_t h = hash3(data + i);
            int32_t candidate = head[h];
            int chain = 0;
            const size_t maxLen = std::min<size_t>(kMaxMatch, size - i);
            while (candidate >= 0 && chain++ < kMaxChain) {
                const size_t dist = i - static_cast<size_t>(candidate);
                if (dist > kWindowSize) break;
                const uint8_t* a = data + i;
                const uint8_t* b = data + candidate;
                if (a[bestLen] == b[bestLen] && a[0] == b[0] && a[1] == b[1] && a[2] == b[2]) {
                    size_t len = 0;
                    while (len < maxLen && a[len] == b[len]) ++len;
                    if (len > bestLen) {
                        bestLen = len;
                        bestDist = dist;
                        if (len >= maxLen) break;
                    }
                }
                candidate = prev[static_cast<size_t>(candidate)];
            }
            prev[i] = head[h];
            head[h] = static_cast<int32_t>(i);
        }

        if (bestLen >= kMinMatch) {
            const int lc = lengthCode(static_cast<int>(bestLen));
            uint32_t code = 0;
            int bits = 0;
            fixedLitCode(257 + lc, code, bits);
            writer.code(code, bits);
            if (kLengthExtra[lc]) writer.bits(static_cast<uint32_t>(bestLen - kLengthBase[lc]),
                                              kLengthExtra[lc]);
            const int dc = distanceCode(static_cast<int>(bestDist));
            writer.code(static_cast<uint32_t>(dc), 5);
            if (kDistExtra[dc]) writer.bits(static_cast<uint32_t>(bestDist - kDistBase[dc]),
                                            kDistExtra[dc]);
            // Register the interior of the match so later positions can find it.
            for (size_t k = i + 1; k < i + bestLen && k + kMinMatch <= size; ++k) {
                const uint32_t hk = hash3(data + k);
                prev[k] = head[hk];
                head[hk] = static_cast<int32_t>(k);
            }
            i += bestLen;
        } else {
            uint32_t code = 0;
            int bits = 0;
            fixedLitCode(data[i], code, bits);
            writer.code(code, bits);
            ++i;
        }
    }

    uint32_t endCode = 0;
    int endBits = 0;
    fixedLitCode(256, endCode, endBits);  // end of block
    writer.code(endCode, endBits);
    writer.flush();
    return out;
}

std::vector<uint8_t> zlibCompress(const uint8_t* data, size_t size) {
    std::vector<uint8_t> out;
    out.push_back(0x78);  // CM = 8 (deflate), CINFO = 7 (32K window)
    out.push_back(0x9C);  // FCHECK so the header is a multiple of 31, FLEVEL = 2
    const std::vector<uint8_t> body = deflateRaw(data, size);
    out.insert(out.end(), body.begin(), body.end());
    const uint32_t sum = adler32(data, size);
    out.push_back(static_cast<uint8_t>(sum >> 24));
    out.push_back(static_cast<uint8_t>(sum >> 16));
    out.push_back(static_cast<uint8_t>(sum >> 8));
    out.push_back(static_cast<uint8_t>(sum));
    return out;
}

std::vector<uint8_t> inflateRaw(const uint8_t* data, size_t size, size_t sizeHint) {
    std::vector<uint8_t> out;
    if (sizeHint) out.reserve(sizeHint);
    BitReader in(data, size);

    for (;;) {
        uint32_t final = 0, type = 0;
        if (!in.bits(1, final)) return {};
        if (!in.bits(2, type)) return {};

        if (type == 0) {
            in.alignToByte();
            uint8_t header[4];
            if (!in.readBytes(header, 4)) return {};
            const size_t len = static_cast<size_t>(header[0]) | (static_cast<size_t>(header[1]) << 8);
            const size_t nlen =
                static_cast<size_t>(header[2]) | (static_cast<size_t>(header[3]) << 8);
            if ((len ^ 0xFFFFu) != nlen) return {};
            const size_t at = out.size();
            out.resize(at + len);
            if (len && !in.readBytes(out.data() + at, len)) return {};
        } else if (type == 1) {
            // Fixed Huffman tables are fixed by the format.
            uint8_t litLengths[288];
            for (int k = 0; k < 144; ++k) litLengths[k] = 8;
            for (int k = 144; k < 256; ++k) litLengths[k] = 9;
            for (int k = 256; k < 280; ++k) litLengths[k] = 7;
            for (int k = 280; k < 288; ++k) litLengths[k] = 8;
            uint8_t distLengths[30];
            for (int k = 0; k < 30; ++k) distLengths[k] = 5;
            HuffmanTable lit, dist;
            if (!lit.build(litLengths, 288) || !dist.build(distLengths, 30)) return {};
            if (!inflateBlock(in, lit, dist, out)) return {};
        } else if (type == 2) {
            uint32_t hlit = 0, hdist = 0, hclen = 0;
            if (!in.bits(5, hlit) || !in.bits(5, hdist) || !in.bits(4, hclen)) return {};
            const int litCount = static_cast<int>(hlit) + 257;
            const int distCount = static_cast<int>(hdist) + 1;
            const int clenCount = static_cast<int>(hclen) + 4;

            static constexpr int kOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                               11, 4,  12, 3, 13, 2, 14, 1, 15};
            uint8_t clens[19] = {0};
            for (int k = 0; k < clenCount; ++k) {
                uint32_t v;
                if (!in.bits(3, v)) return {};
                clens[kOrder[k]] = static_cast<uint8_t>(v);
            }
            HuffmanTable clTable;
            if (!clTable.build(clens, 19)) return {};

            uint8_t lengths[320] = {0};
            int index = 0;
            while (index < litCount + distCount) {
                int symbol;
                if (!clTable.decode(in, symbol)) return {};
                if (symbol < 16) {
                    lengths[index++] = static_cast<uint8_t>(symbol);
                } else if (symbol == 16) {
                    if (index == 0) return {};
                    uint32_t extra = 0;
                    if (!in.bits(2, extra)) return {};
                    const uint8_t prevLen = lengths[index - 1];
                    for (uint32_t k = 0; k < extra + 3; ++k) {
                        if (index >= litCount + distCount) return {};
                        lengths[index++] = prevLen;
                    }
                } else if (symbol == 17) {
                    uint32_t extra = 0;
                    if (!in.bits(3, extra)) return {};
                    for (uint32_t k = 0; k < extra + 3; ++k) {
                        if (index >= litCount + distCount) return {};
                        lengths[index++] = 0;
                    }
                } else {
                    uint32_t extra = 0;
                    if (!in.bits(7, extra)) return {};
                    for (uint32_t k = 0; k < extra + 11; ++k) {
                        if (index >= litCount + distCount) return {};
                        lengths[index++] = 0;
                    }
                }
            }
            HuffmanTable lit, dist;
            if (!lit.build(lengths, litCount)) return {};
            if (!dist.build(lengths + litCount, distCount)) return {};
            if (!inflateBlock(in, lit, dist, out)) return {};
        } else {
            return {};  // reserved block type
        }
        if (final) break;
    }
    return out;
}

std::vector<uint8_t> zlibDecompress(const uint8_t* data, size_t size, size_t sizeHint) {
    if (size < 6) return {};
    const uint8_t cmf = data[0];
    if ((cmf & 0x0F) != 8) return {};                    // must be deflate
    if (((static_cast<uint32_t>(cmf) << 8) | data[1]) % 31u != 0) return {};  // header check
    if (data[1] & 0x20) return {};                       // preset dictionary unsupported

    std::vector<uint8_t> out = inflateRaw(data + 2, size - 6, sizeHint);
    if (out.empty() && sizeHint > 0) return {};
    const uint32_t expected = (static_cast<uint32_t>(data[size - 4]) << 24) |
                              (static_cast<uint32_t>(data[size - 3]) << 16) |
                              (static_cast<uint32_t>(data[size - 2]) << 8) |
                              static_cast<uint32_t>(data[size - 1]);
    if (adler32(out.data(), out.size()) != expected) return {};
    return out;
}

}  // namespace nova