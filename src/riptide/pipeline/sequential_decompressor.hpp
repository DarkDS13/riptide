#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace riptide {

/// Single-threaded reference decompressor built on the same DEFLATE decoder as the
/// parallel pipeline. It is the T1 baseline for speedup measurements and decodes
/// every member of a multi-member gzip file. Throws DecodeError on invalid input.
[[nodiscard]] std::vector<std::uint8_t> decompress_sequential(std::span<const std::uint8_t> gzip,
                                                              bool verify = true);

}  // namespace riptide
