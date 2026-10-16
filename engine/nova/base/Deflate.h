// DEFLATE (RFC 1951) and zlib (RFC 1950) codec.
//
// The engine has no third-party image or compression dependency it can rely on
// being present on every build host, so the PNG codec carries its own. This is
// a complete decoder -- stored, fixed-Huffman and dynamic-Huffman blocks -- and
// a fixed-Huffman encoder with hash-chain LZ77 matching.
//
// The encoder deliberately stops at fixed Huffman. Dynamic Huffman would win
// another 10-20% on photographic data, but the tables it needs cost several
// hundred lines of subtle code that only pays off on content this engine does
// not actually produce (it renders flat-shaded frames and UI).
#ifndef NOVA_BASE_DEFLATE_H
#define NOVA_BASE_DEFLATE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nova {

/// Running Adler-32 checksum, the one zlib streams end with.
uint32_t adler32(const uint8_t* data, size_t size, uint32_t seed = 1);

/// Running CRC-32 (as used by PNG chunks and zlib).
uint32_t crc32(const uint8_t* data, size_t size, uint32_t seed = 0);

/// Deflate [data] into a raw DEFLATE stream.
std::vector<uint8_t> deflateRaw(const uint8_t* data, size_t size);

/// Wrap [deflateRaw] output in a zlib container (header + checksum).
std::vector<uint8_t> zlibCompress(const uint8_t* data, size_t size);

/// Inflate a raw DEFLATE stream. Returns an empty vector on malformed input.
std::vector<uint8_t> inflateRaw(const uint8_t* data, size_t size, size_t sizeHint = 0);

/// Inflate a zlib container, verifying the trailing Adler-32.
std::vector<uint8_t> zlibDecompress(const uint8_t* data, size_t size, size_t sizeHint = 0);

}  // namespace nova

#endif  // NOVA_BASE_DEFLATE_H