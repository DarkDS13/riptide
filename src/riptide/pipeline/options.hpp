#pragma once

#include <cstddef>
#include <string_view>

#include <riptide/parallel/thread_pool.hpp>
#include <riptide/window/window_resolver.hpp>

namespace riptide {

/// How the compressed stream is cut into chunks before decoding.
enum class ChunkingPolicy {
    /// Equal compressed size, as in the base paper.
    kFixed,
    /// Equal estimated decoding cost, from sampled Huffman headers (M1).
    kWorkBalanced,
};

/// What happens when a chunk produces more output than its budget.
enum class OverflowPolicy {
    /// The rest of the chunk is decoded by one task, which serialises highly
    /// compressible regions (the behaviour of the base paper's released software).
    kSerial,
    /// The rest of the chunk is split speculatively across all workers (M1).
    kSplit,
};

[[nodiscard]] std::string_view to_string(ChunkingPolicy policy) noexcept;
[[nodiscard]] std::string_view to_string(OverflowPolicy policy) noexcept;

struct DecompressorOptions {
    std::size_t threads = ThreadPool::hardware_threads();
    /// Compressed bytes per chunk. The base paper's default is 4 MiB.
    std::size_t chunk_size = 4 * 1024 * 1024;
    WindowStrategy window_strategy = WindowStrategy::kBlocked;
    ChunkingPolicy chunking = ChunkingPolicy::kFixed;
    OverflowPolicy overflow = OverflowPolicy::kSplit;
    /// A chunk may produce at most this many times its compressed size before its
    /// remainder is handed on according to `overflow`. The base paper caps at 20.
    double max_expansion = 20.0;
    /// Distance between sampled headers for work-balanced chunking.
    std::size_t sample_interval = 512 * 1024;
    /// Check CRC-32 and size of every gzip member.
    bool verify = true;
};

}  // namespace riptide
