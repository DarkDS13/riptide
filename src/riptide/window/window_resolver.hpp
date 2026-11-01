#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <riptide/parallel/prefix_scan.hpp>
#include <riptide/parallel/thread_pool.hpp>
#include <riptide/window/window.hpp>

namespace riptide {

/// How the chain of chunk windows is resolved.
enum class WindowStrategy {
    /// One window after another, as in the base paper: n dependent steps.
    kSerial,
    /// Hillis-Steele scan over window maps: ceil(log2 n) rounds, O(n log n) work.
    kHillisSteele,
    /// Blelloch scan over window maps: 2 log2 n + 1 rounds, O(n) work.
    kBlelloch,
    /// Blocked resolution: one block of consecutive chunks per worker, about 2n work
    /// and 2n / p + p dependent steps. The default.
    kBlocked,
    /// The serial chain on the GPU: each step resolves one whole window in parallel
    /// (32 768 threads), but the n steps still run one after another.
    kGpuSerial,
    /// Blocked resolution on the GPU with about sqrt(2n) blocks: about 2 sqrt(2n)
    /// dependent steps, each resolving one window of every block at once.
    kGpuBlocked,
};

[[nodiscard]] std::string_view to_string(WindowStrategy strategy) noexcept;

/// True for the strategies that run on the GPU (see gpu/gpu_window_resolver.hpp).
[[nodiscard]] bool is_gpu(WindowStrategy strategy) noexcept;

struct WindowResolution {
    /// windows[k] is the concrete window that follows chunk k of the chain.
    std::span<const ByteWindow> windows;
    /// Owns the memory of `windows` (host memory, or a buffer shared with the GPU).
    std::shared_ptr<const void> storage;
    ScanCost cost;
    /// Time the GPU spent executing, for the GPU strategies; zero otherwise.
    double device_seconds = 0.0;
};

/// Resolves the windows after every chunk of a chain.
///
/// `first` is the concrete window after chunk 0; `maps[k - 1]` is the window map of
/// chunk k (k >= 1), as returned by final_window_of(). Every strategy produces
/// identical windows; they differ only in how much of the work is on the critical
/// path. The GPU strategies are not handled here but by GpuWindowResolver, and
/// raise std::invalid_argument.
[[nodiscard]] WindowResolution resolve_windows(ThreadPool& pool, const ByteWindow& first,
                                               std::span<const SymbolWindowPtr> maps,
                                               WindowStrategy strategy);

}  // namespace riptide
