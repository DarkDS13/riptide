#include <riptide/window/window.hpp>

#include <algorithm>

namespace riptide {

const SymbolWindowPtr& identity_window() {
    static const SymbolWindowPtr identity = [] {
        auto window = std::make_shared<SymbolWindow>();
        for (std::size_t j = 0; j < kWindowSize; ++j) {
            window->symbols[j] = static_cast<std::uint16_t>(kMarkerBase + j);
        }
        return SymbolWindowPtr(std::move(window));
    }();
    return identity;
}

SymbolWindowPtr compose(const SymbolWindowPtr& earlier, const SymbolWindowPtr& later) {
    // A concrete map ignores its input, and the identity changes nothing; both
    // cases are common and need no new 64 KiB map.
    if (later->concrete || earlier == identity_window()) {
        return later;
    }
    if (later == identity_window()) {
        return earlier;
    }

    auto result = std::make_shared<SymbolWindow>();
    const auto& outer = later->symbols;
    const auto& inner = earlier->symbols;
    std::uint16_t seen = 0;
    for (std::size_t i = 0; i < kWindowSize; ++i) {
        const std::uint16_t symbol = outer[i];
        const std::uint16_t resolved = is_marker(symbol) ? inner[symbol - kMarkerBase] : symbol;
        result->symbols[i] = resolved;
        seen = static_cast<std::uint16_t>(seen | resolved);
    }
    result->concrete = !is_marker(seen);
    return result;
}

void apply(const SymbolWindow& map, const ByteWindow& previous, ByteWindow& output) noexcept {
    for (std::size_t i = 0; i < kWindowSize; ++i) {
        const std::uint16_t symbol = map.symbols[i];
        output[i] = is_marker(symbol) ? previous[symbol - kMarkerBase]
                                      : static_cast<std::uint8_t>(symbol);
    }
}

SymbolWindowPtr final_window_of(const ChunkData& chunk) {
    auto window = std::make_shared<SymbolWindow>();
    auto& symbols = window->symbols;

    if (chunk.plain.size() >= kWindowSize) {
        // Output switched to 8-bit form (or started with a full known window):
        // its last 32 KiB are concrete bytes.
        const auto tail = std::span(chunk.plain).last(kWindowSize);
        std::copy(tail.begin(), tail.end(), symbols.begin());
        window->concrete = true;
    } else if (chunk.uses_markers) {
        // Still in 16-bit form; `marked` starts with 32 KiB of identity markers, so
        // it always holds at least a full window.
        const auto tail = std::span(chunk.marked).last(kWindowSize);
        std::copy(tail.begin(), tail.end(), symbols.begin());
        window->concrete =
            std::none_of(tail.begin(), tail.end(), [](std::uint16_t s) { return is_marker(s); });
    } else {
        // Known-window chunk shorter than 32 KiB (only at the start of a stream);
        // positions before the stream start are never referenced and stay zero.
        std::copy(chunk.plain.begin(), chunk.plain.end(),
                  symbols.end() - static_cast<std::ptrdiff_t>(chunk.plain.size()));
        window->concrete = true;
    }
    return window;
}

void replace_markers(std::span<const std::uint16_t> symbols, const ByteWindow& window,
                     std::uint8_t* output) noexcept {
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        const std::uint16_t symbol = symbols[i];
        output[i] = is_marker(symbol) ? window[symbol - kMarkerBase]
                                      : static_cast<std::uint8_t>(symbol);
    }
}

}  // namespace riptide
