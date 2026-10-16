// SHA-1 (RFC 3174), used only to answer the WebSocket opening handshake.
//
// It is not a security primitive and nothing here relies on it being one: the
// handshake only needs a digest both ends can compute, and a browser will accept
// SHA-1 here. A real deployment terminates TLS elsewhere and would use a modern
// digest for anything that actually matters.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace nova {
namespace net {

/// 20-byte digest of [data].
void sha1(const uint8_t* data, size_t size, uint8_t out[20]);

/// The RFC 6455 opening handshake accept value for a client key.
std::string websocketAccept(const std::string& clientKey);

/// Constant-time comparison, for anything checked against a secret.
bool constantTimeEquals(const std::string& a, const std::string& b);

}  // namespace net
}  // namespace nova