#include "net/Base64.h"

namespace nova {
namespace net {

namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// Value of a base64 character, or -1. Accepts both the standard and the URL
/// safe alphabets, because a browser will happily hand back either.
int decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

inline bool isSkippable(char c) {
    return c == '\n' || c == '\r' || c == ' ' || c == '\t';
}

}  // namespace

std::string base64Encode(const uint8_t* data, size_t size) {
    std::string out;
    if (!data || size == 0) return out;
    out.reserve(((size + 2) / 3) * 4);

    size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        const uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                           (static_cast<uint32_t>(data[i + 1]) << 8) |
                           static_cast<uint32_t>(data[i + 2]);
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += kAlphabet[(v >> 6) & 0x3F];
        out += kAlphabet[v & 0x3F];
    }
    const size_t left = size - i;
    if (left == 1) {
        const uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += "==";
    } else if (left == 2) {
        const uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                           (static_cast<uint32_t>(data[i + 1]) << 8);
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += kAlphabet[(v >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

std::string base64Encode(const std::vector<uint8_t>& data) {
    return base64Encode(data.data(), data.size());
}

std::string base64Encode(const std::string& text) {
    return base64Encode(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

bool base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    // A failed decode must leave nothing behind: callers that ignore the
    // return value would otherwise read half a message.
    out.clear();
    auto fail = [&out]() {
        out.clear();
        return false;
    };
    out.reserve(text.size() / 4 * 3);

    uint32_t buffer = 0;
    int bits = 0;
    size_t consumed = 0;
    bool sawPadding = false;
    for (char c : text) {
        if (isSkippable(c)) continue;
        if (c == '=') {
            sawPadding = true;
            continue;
        }
        if (sawPadding) return fail();  // data after padding is malformed
        const int v = decodeChar(c);
        if (v < 0) return fail();
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        ++consumed;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    // A well-formed group is 4 characters wide; a remainder of one is impossible.
    if (consumed % 4 == 1) return fail();
    return true;
}

bool base64Decode(const std::string& text, std::string& out) {
    std::vector<uint8_t> bytes;
    if (!base64Decode(text, bytes)) {
        out.clear();
        return false;
    }
    out.assign(bytes.begin(), bytes.end());
    return true;
}

}  // namespace net
}  // namespace nova