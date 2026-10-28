#include <riptide/pipeline/options.hpp>
#include <riptide/pipeline/statistics.hpp>

#include <sstream>

namespace riptide {

std::string_view to_string(ChunkingPolicy policy) noexcept {
    return policy == ChunkingPolicy::kWorkBalanced ? "work-balanced" : "fixed";
}

std::string_view to_string(OverflowPolicy policy) noexcept {
    return policy == OverflowPolicy::kSplit ? "split" : "serial";
}

std::string DecompressionStatistics::to_json() const {
    std::ostringstream out;
    out.precision(9);
    out << '{' << "\"partition_seconds\":" << partition_seconds
        << ",\"decode_seconds\":" << decode_seconds << ",\"chain_seconds\":" << chain_seconds
        << ",\"window_map_seconds\":" << window_map_seconds
        << ",\"window_seconds\":" << window_seconds << ",\"replace_seconds\":" << replace_seconds
        << ",\"checksum_seconds\":" << checksum_seconds << ",\"total_seconds\":" << total_seconds
        << ",\"compressed_bytes\":" << compressed_bytes
        << ",\"decompressed_bytes\":" << decompressed_bytes << ",\"members\":" << members
        << ",\"planned_chunks\":" << planned_chunks << ",\"chain_length\":" << chain_length
        << ",\"overflow_splits\":" << overflow_splits << ",\"fallback_decodes\":" << fallback_decodes
        << ",\"rejected_candidates\":" << rejected_candidates
        << ",\"unused_results\":" << unused_results << ",\"marked_symbols\":" << marked_symbols
        << ",\"window_rounds\":" << window_rounds
        << ",\"window_combinations\":" << window_combinations
        << ",\"tasks_executed\":" << pool.tasks_executed << ",\"tasks_stolen\":" << pool.tasks_stolen
        << ",\"throughput_mb_per_s\":" << throughput_mb_per_s() << '}';
    return out.str();
}

}  // namespace riptide
