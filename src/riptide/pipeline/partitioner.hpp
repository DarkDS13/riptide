#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <riptide/parallel/thread_pool.hpp>

namespace riptide {

/// Byte offsets at which chunks start their search for a block boundary.
/// `starts[0]` is the start of the DEFLATE data; chunk i covers
/// [starts[i], starts[i + 1]), and the last chunk runs to `end`.
struct PartitionPlan {
    std::vector<std::size_t> starts;
    std::size_t end = 0;
    std::size_t samples = 0;  ///< Headers sampled (work-balanced plans only).
};

/// Cuts [begin, end) into chunks of `chunk_size` compressed bytes.
[[nodiscard]] PartitionPlan plan_fixed_chunks(std::size_t begin, std::size_t end,
                                              std::size_t chunk_size);

/// Cuts [begin, end) into the same number of chunks as plan_fixed_chunks(), but of
/// equal *estimated decoding cost* (M1).
///
/// Every `sample_interval` bytes, the first dynamic block header is located with the
/// block finder and its expansion is estimated from its code lengths, in parallel.
/// Each interval is assigned the cost density of its sample, and the chunk
/// boundaries are placed at equal quantiles of the cumulative cost.
[[nodiscard]] PartitionPlan plan_work_balanced_chunks(ThreadPool& pool,
                                                      std::span<const std::uint8_t> input,
                                                      std::size_t begin, std::size_t end,
                                                      std::size_t chunk_size,
                                                      std::size_t sample_interval);

/// Estimated decoding cost per compressed byte, in units of one Huffman symbol.
///
/// Decoding a symbol costs one unit and producing an output byte (a literal store
/// or one byte of a match copy) a further kCopyCost units; stored blocks are a
/// plain copy. The constants follow the model used in our analysis of Silesia.
[[nodiscard]] double estimated_cost_density(double symbols_per_byte, double expansion_ratio) noexcept;

}  // namespace riptide
