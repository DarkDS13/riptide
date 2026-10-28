#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace riptide {

/// CRC-32 as used by gzip (reflected polynomial 0xEDB88320).
///
/// `crc32_update(0, data)` returns the same value as zlib's `crc32(0, data, size)`.
[[nodiscard]] std::uint32_t crc32_update(std::uint32_t crc, std::span<const std::uint8_t> data) noexcept;

/// Combines CRC(A) and CRC(B) into CRC(A || B), where `length_b` is the size of B.
///
/// This is what makes checksum computation parallel: independent workers compute
/// the CRC of their own piece, and the pieces are joined in O(log length_b) time
/// by multiplying with x^(8 * length_b) modulo the CRC polynomial.
[[nodiscard]] std::uint32_t crc32_combine(std::uint32_t crc_a, std::uint32_t crc_b,
                                          std::size_t length_b) noexcept;

}  // namespace riptide
