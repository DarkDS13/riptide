#pragma once

// Window resolution on the GPU (M3 with Metal, on Apple silicon).
//
// Applying a window map to a window is 32 768 independent lookups, and composing two
// maps is 32 768 independent gathers. Every step of window resolution is therefore
// data-parallel across the window, in addition to the parallelism across chunks
// that the CPU scans exploit. On the GPU one step is one dispatch with 32 768
// threads per window, and what remains on the critical path is the number of
// dependent dispatches:
//
//   gpu-serial    the base paper's chain, one dispatch per chunk: n dispatches,
//                 each resolving a single window.
//   gpu-blocked   the blocked scheme of resolve_windows() (compose each block,
//                 carry between blocks, resolve each block), with B blocks chosen
//                 to minimise dispatches rather than one per CPU thread:
//                 2 ceil(n / B) + B - 1 dispatches for B = sqrt(2n), each covering
//                 one window of every block.
//
// Both perform the same operations as their CPU counterparts and produce identical
// windows. On Apple silicon the GPU shares memory with the CPU: the pipeline writes
// the window maps straight into a GPU buffer, and the resolved windows are read by
// marker replacement from another one, with no copies.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <riptide/common/deflate_constants.hpp>
#include <riptide/window/window.hpp>
#include <riptide/window/window_resolver.hpp>

namespace riptide {

class GpuWindowResolver {
public:
    /// Window maps in memory shared with the GPU. Map k (the map of chunk k + 1 of
    /// the chain) is the kWindowSize symbols starting at map(k).
    class Maps {
    public:
        [[nodiscard]] std::uint16_t* map(std::size_t k) const noexcept { return symbols_ + k * kWindowSize; }
        [[nodiscard]] std::size_t size() const noexcept { return count_; }

    private:
        friend class GpuWindowResolver;
        std::shared_ptr<void> buffer_;
        std::uint16_t* symbols_ = nullptr;
        std::size_t count_ = 0;
    };

    /// Selects the system GPU and compiles the kernels (once, at construction).
    /// Throws std::runtime_error if there is no usable GPU.
    GpuWindowResolver();
    ~GpuWindowResolver();

    GpuWindowResolver(const GpuWindowResolver&) = delete;
    GpuWindowResolver& operator=(const GpuWindowResolver&) = delete;

    /// True if this build has GPU support and a GPU is present.
    [[nodiscard]] static bool available() noexcept;

    /// Allocates GPU-visible storage for `count` maps; the caller fills it.
    [[nodiscard]] Maps allocate_maps(std::size_t count) const;

    /// Resolves the windows after every chunk. `first` is the window after chunk 0;
    /// `strategy` must be kGpuSerial or kGpuBlocked. Not safe to call concurrently.
    [[nodiscard]] WindowResolution resolve(const ByteWindow& first, const Maps& maps,
                                           WindowStrategy strategy) const;

    [[nodiscard]] std::string device_name() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Number of blocks a GPU strategy uses for `maps` window maps: 1 for gpu-serial,
/// round(sqrt(2 maps)) for gpu-blocked, which minimises 2 ceil(n / B) + B - 1.
[[nodiscard]] inline std::size_t gpu_block_count(WindowStrategy strategy, std::size_t maps) noexcept {
    if (strategy != WindowStrategy::kGpuBlocked || maps <= 1) {
        return 1;
    }
    const auto blocks = static_cast<std::size_t>(std::lround(std::sqrt(2.0 * static_cast<double>(maps))));
    return std::clamp<std::size_t>(blocks, 1, maps);
}

}  // namespace riptide
