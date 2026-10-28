#pragma once

#include <cstddef>
#include <string>

#include <riptide/parallel/thread_pool.hpp>

namespace riptide {

/// Where time went during one decompression, and how the work was divided.
struct DecompressionStatistics {
    // Wall-clock time per pipeline stage, in seconds.
    double partition_seconds = 0.0;  ///< Chunk planning, including header sampling.
    double decode_seconds = 0.0;     ///< First stage: parallel decoding with markers.
    double chain_seconds = 0.0;      ///< Linking chunk results; includes fallback decodes.
    double window_map_seconds = 0.0; ///< Building every chunk's window map (independent, parallel).
    double window_seconds = 0.0;     ///< Window resolution itself (the step M3 parallelises).
    double replace_seconds = 0.0;    ///< Second stage: marker replacement and assembly.
    double checksum_seconds = 0.0;   ///< Parallel CRC-32 and footer checks.
    double total_seconds = 0.0;

    std::size_t compressed_bytes = 0;
    std::size_t decompressed_bytes = 0;
    std::size_t members = 0;

    std::size_t planned_chunks = 0;       ///< Chunks produced by the partitioner.
    std::size_t chain_length = 0;         ///< Chunks in the final, linked chain.
    std::size_t overflow_splits = 0;      ///< Chunks that exceeded their output budget.
    std::size_t fallback_decodes = 0;     ///< Chain links decoded serially after a mismatch.
    std::size_t rejected_candidates = 0;  ///< Block-finder candidates that failed to decode.
    std::size_t unused_results = 0;       ///< Decoded chunks that did not join the chain.
    std::size_t marked_symbols = 0;       ///< Output held in 16-bit form after the first stage.

    std::size_t window_rounds = 0;        ///< Dependent steps in window resolution.
    std::size_t window_combinations = 0;  ///< Window compositions/applications performed.

    ThreadPoolStatistics pool;

    /// Output throughput in MB/s (10^6 bytes per second).
    [[nodiscard]] double throughput_mb_per_s() const noexcept {
        return total_seconds > 0.0 ? static_cast<double>(decompressed_bytes) / total_seconds / 1e6
                                   : 0.0;
    }

    /// Single-line JSON object with every field.
    [[nodiscard]] std::string to_json() const;
};

}  // namespace riptide
