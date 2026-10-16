// Base64 (RFC 4648). The WebSocket handshake needs it and nothing else in the
// engine does, so it lives here rather than dragging in a crypto library.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nova {
namespace net {

/// Encodes [data] as standard base64 with '=' padding and no line breaks.
std::string base64Encode(const uint8_t* data, size_t size);
std::string base64Encode(const std::vector<uint8_t>& data);
std::string base64Encode(const std::string& text);

/// Decodes standard or URL-safe base64. Whitespace and padding are optional;
/// anything else is a decode failure, reported by returning false and leaving
/// [out] empty rather than throwing.
bool base64Decode(const std::string& text, std::vector<uint8_t>& out);
bool base64Decode(const std::string& text, std::string& out);

}  // namespace net
}  // namespace nova