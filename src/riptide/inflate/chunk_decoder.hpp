#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <riptide/common/deflate_constants.hpp>

namespace riptide {

/// Position at which a chunk starts decoding.
///
/// For dynamic and fixed blocks `bit` is the offset of the 3-bit block header. A
/// stored block is padded to a byte boundary after its header, so the header bit
/// cannot be recovered unambiguously from the data; such blocks are identified by
/// the byte-aligned offset of their LEN field instead, with `stored_payload` set.
/// The same convention is used for the end of a chunk, so the end of chunk k and the
/// start of chunk k + 1 compare equal exactly when they denote the same block.
struct BlockBoundary {
    std::size_t bit = 0;
    bool stored_payload = false;

    friend bool operator==(const BlockBoundary&, const BlockBoundary&) = default;
};

/// When a chunk should stop decoding. Decoding only ever stops in front of a
/// non-final dynamic or stored block, because those are the only blocks the block
/// finder searches for.
struct ChunkLimits {
    /// Stop at the first such block whose boundary bit is at or after this offset.
    std::size_t stop_bit = std::numeric_limits<std::size_t>::max();
    /// Stop at the first such block once this many bytes have been produced.
    std::size_t output_budget = std::numeric_limits<std::size_t>::max();
};

enum class StopReason {
    kBoundary,      ///< Reached `stop_bit`.
    kOutputBudget,  ///< Produced at least `output_budget` bytes (split-on-overflow).
    kStreamEnd,     ///< Decoded the final block of the DEFLATE stream.
};

/// Output of one chunk.
///
/// While the 32 KiB window preceding the chunk is unknown, output is stored as
/// 16-bit symbols in `marked`: literal bytes, or markers (see kMarkerBase) for bytes
/// copied out of the unknown window. `marked` starts with kWindowSize identity
/// markers standing in for that window. As soon as the last 32 KiB of output contain
/// no markers, later back-references can no longer reach the unknown window, and
/// decoding continues in 8-bit form in `plain`, whose first `plain_prefix` bytes
/// repeat the last 32 KiB of `marked` so that back-references still resolve.
struct ChunkData {
    std::vector<std::uint16_t> marked;
    std::vector<std::uint8_t> plain;
    std::size_t plain_prefix = 0;
    bool uses_markers = false;

    /// Number of decoded symbols held in `marked` (excluding the identity prefix).
    [[nodiscard]] std::size_t marked_size() const noexcept {
        return uses_markers ? marked.size() - kWindowSize : 0;
    }
    /// Number of decoded bytes held in `plain` (excluding the prefix).
    [[nodiscard]] std::size_t plain_size() const noexcept { return plain.size() - plain_prefix; }
    /// Total decoded size of the chunk in bytes.
    [[nodiscard]] std::size_t size() const noexcept { return marked_size() + plain_size(); }
};

struct ChunkResult {
    BlockBoundary start;
    BlockBoundary end;           ///< Boundary of the block where decoding stopped (unless kStreamEnd).
    std::size_t end_bit = 0;     ///< Bit offset where decoding stopped.
    StopReason reason = StopReason::kStreamEnd;
    std::size_t block_count = 0;
    ChunkData data;
};

/// First stage of two-stage decoding: decodes a chunk without knowing the window
/// that precedes it, writing markers for bytes that refer to that window.
/// Throws DecodeError if the data starting at `start` is not valid DEFLATE.
[[nodiscard]] ChunkResult decode_chunk_with_markers(std::span<const std::uint8_t> input,
                                                    BlockBoundary start,
                                                    const ChunkLimits& limits);

/// Conventional decoding with a known preceding window, which may be empty at the
/// start of a stream. The result never uses markers.
[[nodiscard]] ChunkResult decode_chunk_with_window(std::span<const std::uint8_t> input,
                                                   BlockBoundary start,
                                                   std::span<const std::uint8_t> window,
                                                   const ChunkLimits& limits);

}  // namespace riptide
