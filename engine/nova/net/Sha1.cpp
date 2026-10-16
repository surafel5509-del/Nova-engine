#include "net/Sha1.h"

#include <cstring>

#include "net/Base64.h"

namespace nova {
namespace net {

namespace {

inline uint32_t rotl(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const uint32_t temp = rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = temp;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

}  // namespace

void sha1(const uint8_t* data, size_t size, uint8_t out[20]) {
    uint32_t state[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    size_t i = 0;
    if (data && size > 0) {
        for (; i + 64 <= size; i += 64) transform(state, data + i);
    }

    // The tail: the remaining bytes, the 0x80 terminator, zeroes, and the
    // message length in bits as a big-endian 64-bit value. Zero initialising the
    // whole block first means a null [data] is a valid empty message.
    const size_t rest = (data && size > i) ? size - i : 0;
    uint8_t tail[128] = {0};
    if (rest > 0) std::memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    const size_t padTo = (rest < 56) ? 64 : 128;
    const uint64_t bits = static_cast<uint64_t>(size) * 8;
    for (int b = 0; b < 8; ++b) {
        tail[padTo - 1 - static_cast<size_t>(b)] = static_cast<uint8_t>((bits >> (8 * b)) & 0xFF);
    }
    transform(state, tail);
    if (padTo == 128) transform(state, tail + 64);

    for (int i2 = 0; i2 < 5; ++i2) {
        out[i2 * 4] = static_cast<uint8_t>((state[i2] >> 24) & 0xFF);
        out[i2 * 4 + 1] = static_cast<uint8_t>((state[i2] >> 16) & 0xFF);
        out[i2 * 4 + 2] = static_cast<uint8_t>((state[i2] >> 8) & 0xFF);
        out[i2 * 4 + 3] = static_cast<uint8_t>(state[i2] & 0xFF);
    }
}

std::string websocketAccept(const std::string& clientKey) {
    // RFC 6455: the fixed GUID is appended to the client key verbatim.
    static const char* kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const std::string combined = clientKey + kGuid;
    uint8_t digest[20];
    sha1(reinterpret_cast<const uint8_t*>(combined.data()), combined.size(), digest);
    return base64Encode(digest, 20);
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
    // Compare the full length regardless of what it is, so the time taken does
    // not leak how much of a guess was correct.
    size_t diff = a.size() ^ b.size();
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) diff |= static_cast<size_t>(a[i] ^ b[i]);
    return diff == 0;
}

}  // namespace net
}  // namespace nova
