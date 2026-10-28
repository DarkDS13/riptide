#include <riptide/pipeline/partitioner.hpp>

#include <algorithm>
#include <cmath>

#include <riptide/inflate/block_finder.hpp>
#include <riptide/inflate/expansion_estimator.hpp>
#include <riptide/parallel/task_group.hpp>

namespace riptide {
namespace {

constexpr double kCopyCost = 0.1;
/// Cost density assumed for an interval without a dynamic block (stored data).
constexpr double kStoredDensity = kCopyCost;
/// Chunks are never made smaller than this, so block-finder overhead stays bounded.
constexpr std::size_t kMinimumChunk = 64 * 1024;

std::size_t chunk_count(std::size_t begin, std::size_t end, std::size_t chunk_size) {
    chunk_size = std::max<std::size_t>(chunk_size, 1);
    return std::max<std::size_t>((end - begin + chunk_size - 1) / chunk_size, 1);
}

}  // namespace

double estimated_cost_density(double symbols_per_byte, double expansion_ratio) noexcept {
    return symbols_per_byte + kCopyCost * expansion_ratio;
}

PartitionPlan plan_fixed_chunks(std::size_t begin, std::size_t end, std::size_t chunk_size) {
    PartitionPlan plan;
    plan.end = end;
    const std::size_t count = chunk_count(begin, end, chunk_size);
    for (std::size_t i = 0; i < count; ++i) {
        plan.starts.push_back(begin + i * std::max<std::size_t>(chunk_size, 1));
    }
    return plan;
}

PartitionPlan plan_work_balanced_chunks(ThreadPool& pool, std::span<const std::uint8_t> input,
                                        std::size_t begin, std::size_t end,
                                        std::size_t chunk_size, std::size_t sample_interval) {
    const std::size_t count = chunk_count(begin, end, chunk_size);
    if (count <= 1 || end - begin < 2 * kMinimumChunk) {
        return plan_fixed_chunks(begin, end, chunk_size);
    }

    sample_interval = std::max<std::size_t>(sample_interval, 4096);
    const std::size_t intervals = (end - begin + sample_interval - 1) / sample_interval;
    std::vector<double> density(intervals, kStoredDensity);

    const BlockFinder finder(input);
    parallel_for(pool, 0, intervals, 1, [&](std::size_t lo, std::size_t hi) {
        for (std::size_t i = lo; i < hi; ++i) {
            const std::size_t from = begin + i * sample_interval;
            const std::size_t to = std::min(from + sample_interval, end);
            if (const auto header = finder.find_dynamic(from * 8, to * 8)) {
                if (const auto estimate = estimate_block_at(input, *header)) {
                    density[i] = estimated_cost_density(estimate->symbols_per_byte(),
                                                        estimate->expansion_ratio());
                }
            }
        }
    });

    // Cumulative cost at the end of each interval.
    std::vector<double> cumulative(intervals + 1, 0.0);
    for (std::size_t i = 0; i < intervals; ++i) {
        const std::size_t from = begin + i * sample_interval;
        const std::size_t to = std::min(from + sample_interval, end);
        cumulative[i + 1] = cumulative[i] + density[i] * static_cast<double>(to - from);
    }

    PartitionPlan plan;
    plan.end = end;
    plan.samples = intervals;
    plan.starts.push_back(begin);
    const double total = cumulative.back();
    std::size_t interval = 0;
    for (std::size_t j = 1; j < count; ++j) {
        const double target = total * static_cast<double>(j) / static_cast<double>(count);
        while (interval + 1 < intervals && cumulative[interval + 1] < target) {
            ++interval;
        }
        // Interpolate linearly inside the interval that contains the target.
        const double inside = (target - cumulative[interval]) /
                              std::max(cumulative[interval + 1] - cumulative[interval], 1e-12);
        const std::size_t from = begin + interval * sample_interval;
        auto cut = from + static_cast<std::size_t>(std::llround(
                              inside * static_cast<double>(std::min(sample_interval, end - from))));
        cut = std::max(cut, plan.starts.back() + kMinimumChunk);
        if (cut + kMinimumChunk > end) {
            break;
        }
        plan.starts.push_back(cut);
    }
    return plan;
}

}  // namespace riptide
