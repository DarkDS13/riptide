#pragma once

// Constants of the DEFLATE format (RFC 1951) and of the marker encoding used
// for two-stage decoding.

#include <array>
#include <cstddef>
#include <cstdint>

namespace riptide {

/// Maximum back-reference distance, and therefore the size of a window.
inline constexpr std::size_t kWindowSize = 32 * 1024;

/// Decoded symbols below this value are literal bytes. A symbol `kMarkerBase + j`
/// is a marker: "byte j of the 32 KiB window that precedes this chunk".
inline constexpr std::uint16_t kMarkerBase = 0x8000;
static_assert(kMarkerBase + kWindowSize - 1 <= 0xFFFF, "markers must fit in 16 bits");

inline constexpr bool is_marker(std::uint16_t symbol) noexcept {
    return symbol >= kMarkerBase;
}

inline constexpr unsigned kMaxCodeLength = 15;
inline constexpr unsigned kEndOfBlock = 256;
inline constexpr unsigned kMaxLiteralLengthCodes = 286;
inline constexpr unsigned kMaxDistanceCodes = 30;
inline constexpr unsigned kCodeLengthCodes = 19;
inline constexpr unsigned kMaxMatchLength = 258;

/// Block types stored in the BTYPE field of a block header.
enum class BlockType : std::uint8_t { kStored = 0, kFixed = 1, kDynamic = 2, kReserved = 3 };

/// Order in which code-length code lengths appear in a dynamic block header.
inline constexpr std::array<std::uint8_t, kCodeLengthCodes> kCodeLengthOrder = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/// Base match length and number of extra bits for length symbols 257..285.
inline constexpr std::array<std::uint16_t, 29> kLengthBase = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
inline constexpr std::array<std::uint8_t, 29> kLengthExtraBits = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

/// Base distance and number of extra bits for distance symbols 0..29.
inline constexpr std::array<std::uint16_t, 30> kDistanceBase = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
inline constexpr std::array<std::uint8_t, 30> kDistanceExtraBits = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

}  // namespace riptide
