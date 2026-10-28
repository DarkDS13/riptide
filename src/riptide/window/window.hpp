#pragma once

// Window maps: the algebra behind log-depth window resolution (M3).
//
// After the first decoding stage, the last 32 KiB of a chunk's output is a mix of
// literal bytes and markers that point into the window before the chunk. That makes
// it a map f from the previous window to the next one: W_k = f_k(W_{k-1}).
//
// Composing two such maps yields a map of the same form (every entry is still a
// literal or a pointer into an even earlier window), and composition of functions is
// associative. The chain W_1, W_2, ..., W_n is therefore a prefix computation, which
// a parallel scan evaluates in O(log n) rounds instead of n dependent steps.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <riptide/common/deflate_constants.hpp>
#include <riptide/inflate/chunk_decoder.hpp>

namespace riptide {

/// A fully known 32 KiB window. Byte 0 is the oldest byte.
using ByteWindow = std::array<std::uint8_t, kWindowSize>;

/// A window map: each entry is a literal byte or a marker into the previous window.
struct SymbolWindow {
    std::array<std::uint16_t, kWindowSize> symbols{};
    /// True if no entry is a marker, i.e. the map ignores its input.
    bool concrete = false;
};

using SymbolWindowPtr = std::shared_ptr<const SymbolWindow>;

/// The identity map (entry j = marker j), the neutral element of compose().
[[nodiscard]] const SymbolWindowPtr& identity_window();

/// Returns the map "apply `earlier`, then `later`": every marker j in `later` is
/// replaced by entry j of `earlier`. Associative; `identity_window()` is neutral.
[[nodiscard]] SymbolWindowPtr compose(const SymbolWindowPtr& earlier, const SymbolWindowPtr& later);

/// Evaluates a map on a known window: output[i] = map[i] if literal, else previous[marker].
void apply(const SymbolWindow& map, const ByteWindow& previous, ByteWindow& output) noexcept;

/// The window that follows a chunk, as a map from the window that precedes it.
/// For a chunk decoded with a known window, the result is concrete.
[[nodiscard]] SymbolWindowPtr final_window_of(const ChunkData& chunk);

/// Second decoding stage: writes `symbols` as bytes to `output`, resolving each
/// marker through `window`.
void replace_markers(std::span<const std::uint16_t> symbols, const ByteWindow& window,
                     std::uint8_t* output) noexcept;

}  // namespace riptide
