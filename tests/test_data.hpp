#pragma once

// Test inputs and an independent reference: data generators with different
// statistics, gzip compression through zlib with any level and strategy, and zlib
// decompression to check riptide's output against.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace riptide::test {

using Bytes = std::vector<std::uint8_t>;

/// Uniformly random bytes (incompressible; zlib emits stored blocks).
Bytes random_bytes(std::size_t size, std::uint32_t seed);

/// Word-based pseudo text with a Zipf-like vocabulary (typical ratio about 3).
Bytes text_like(std::size_t size, std::uint32_t seed);

/// Long repeats of short patterns (very high ratio; overlapping copies).
Bytes repetitive(std::size_t size, std::uint32_t seed);

/// Alternating regions of the three kinds above, so expansion varies inside one file.
Bytes mixed(std::size_t size, std::uint32_t seed);

struct GzipSettings {
    int level = 6;
    int strategy = 0;              ///< Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED
    std::size_t flush_every = 0;   ///< If non-zero, Z_FULL_FLUSH every this many input bytes.
    std::string name;              ///< FNAME header field, if non-empty.
};

/// Settings for one compression call (avoids partially initialised aggregates).
GzipSettings settings(int level, int strategy = 0, std::size_t flush_every = 0);

/// Compresses `data` as one gzip member with zlib.
Bytes gzip_compress(std::span<const std::uint8_t> data, const GzipSettings& settings = {});

/// Decompresses every member with zlib.
Bytes zlib_decompress(std::span<const std::uint8_t> gzip);

/// Concatenates byte vectors (multi-member files).
Bytes concatenate(const std::vector<Bytes>& parts);

}  // namespace riptide::test
