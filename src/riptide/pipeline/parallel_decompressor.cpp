#include <riptide/pipeline/parallel_decompressor.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <riptide/common/error.hpp>
#include <riptide/common/timer.hpp>
#include <riptide/gzip/crc32.hpp>
#include <riptide/gzip/gzip_format.hpp>
#include <riptide/inflate/block_finder.hpp>
#include <riptide/inflate/chunk_decoder.hpp>
#include <riptide/parallel/task_group.hpp>
#include <riptide/pipeline/member_reader.hpp>
#include <riptide/pipeline/partitioner.hpp>
#include <riptide/window/window.hpp>
#include <riptide/window/window_resolver.hpp>

namespace riptide {
namespace {

constexpr std::size_t kNoLimit = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kMinimumSplitPiece = 64 * 1024;
constexpr std::size_t kReplaceGrain = 1024 * 1024;
constexpr std::size_t kCopyGrain = 4 * 1024 * 1024;
constexpr std::size_t kChecksumGrain = 4 * 1024 * 1024;

/// Speculative decodes stop with an error once they produce this many times the
/// chunk budget: decoding from a false positive can otherwise expand garbage.
constexpr std::size_t kSpeculativeLimitFactor = 4;

/// Busiest worker's busy time divided by the mean, between two pool snapshots.
double imbalance(const std::vector<double>& before, const std::vector<double>& after) {
    double total = 0.0;
    double busiest = 0.0;
    for (std::size_t i = 0; i < after.size(); ++i) {
        const double busy = after[i] - before[i];
        total += busy;
        busiest = std::max(busiest, busy);
    }
    const double mean = total / static_cast<double>(after.size());
    return mean > 0.0 ? busiest / mean : 1.0;
}

/// A piece of compressed data to be decoded by one task.
struct DecodeRange {
    /// Known exact start; otherwise the task searches [search_begin_bit, search_end_bit).
    std::optional<BlockBoundary> exact_start;
    std::size_t search_begin_bit = 0;
    std::size_t search_end_bit = 0;
    std::size_t stop_bit = kNoLimit;
    bool empty_window = false;  ///< Start of the stream: the preceding window is empty.
};

/// Thread-safe store of decoded chunks, keyed by the block they start at.
class ChunkStore {
public:
    void insert(std::unique_ptr<ChunkResult> chunk) {
        const std::lock_guard lock(mutex_);
        chunks_.try_emplace(key(chunk->start), std::move(chunk));
    }

    std::unique_ptr<ChunkResult> take(const BlockBoundary& start) {
        const std::lock_guard lock(mutex_);
        const auto found = chunks_.find(key(start));
        if (found == chunks_.end()) {
            return nullptr;
        }
        auto chunk = std::move(found->second);
        chunks_.erase(found);
        return chunk;
    }

    [[nodiscard]] std::size_t size() {
        const std::lock_guard lock(mutex_);
        return chunks_.size();
    }

private:
    static std::pair<std::size_t, bool> key(const BlockBoundary& b) { return {b.bit, b.stored_payload}; }

    std::mutex mutex_;
    std::map<std::pair<std::size_t, bool>, std::unique_ptr<ChunkResult>> chunks_;
};

/// State of one decompress() call.
class DecompressionRun {
public:
    DecompressionRun(ThreadPool& pool, const DecompressorOptions& options,
                     std::span<const std::uint8_t> input)
        : pool_(pool), options_(options), input_(input) {}

    DecompressionResult execute() {
        const Stopwatch total;
        stats_.compressed_bytes = input_.size();

        if (!has_another_member(input_, 0)) {
            return {};
        }
        const GzipHeader header = parse_gzip_header(input_, 0);
        const std::size_t deflate_begin = header.size;

        Stopwatch stage;
        plan(deflate_begin);
        stats_.partition_seconds = stage.seconds();

        stage.restart();
        const std::vector<double> busy_before = pool_.busy_seconds();
        decode_chunks(deflate_begin);
        stats_.decode_seconds = stage.seconds();
        stats_.decode_imbalance = imbalance(busy_before, pool_.busy_seconds());

        stage.restart();
        link_chain(deflate_begin);
        stats_.chain_seconds = stage.seconds();

        stage.restart();
        const WindowResolution resolution = resolve();
        stats_.window_seconds = resolution_seconds_;
        stats_.window_map_seconds = stage.seconds() - resolution_seconds_;

        stage.restart();
        DecompressionResult result = assemble(resolution);
        stats_.replace_seconds = stage.seconds();

        stage.restart();
        verify_members(result);
        stats_.checksum_seconds = stage.seconds();

        stats_.total_seconds = total.seconds();
        stats_.pool = pool_.statistics();
        result.statistics = stats_;
        return result;
    }

private:
    // ---- 1. Partition ------------------------------------------------------------

    void plan(std::size_t deflate_begin) {
        plan_ = options_.chunking == ChunkingPolicy::kWorkBalanced
                    ? plan_work_balanced_chunks(pool_, input_, deflate_begin, input_.size(),
                                                options_.chunk_size, options_.sample_interval)
                    : plan_fixed_chunks(deflate_begin, input_.size(), options_.chunk_size);
        stats_.planned_chunks = plan_.starts.size();

        const double budget = options_.max_expansion * static_cast<double>(options_.chunk_size);
        budget_ = budget >= static_cast<double>(kNoLimit) ? kNoLimit : static_cast<std::size_t>(budget);
    }

    /// Stop offset for a chunk that starts at `bit`: the next planned chunk start.
    [[nodiscard]] std::size_t next_planned_stop(std::size_t bit) const {
        const auto next = std::upper_bound(plan_.starts.begin(), plan_.starts.end(), bit / 8);
        return next == plan_.starts.end() ? kNoLimit : *next * 8;
    }

    // ---- 2. First-stage decoding ---------------------------------------------------

    void decode_chunks(std::size_t deflate_begin) {
        TaskGroup group(pool_);
        const auto& starts = plan_.starts;
        for (std::size_t i = 0; i < starts.size(); ++i) {
            const bool last = i + 1 == starts.size();
            DecodeRange range;
            range.search_begin_bit = starts[i] * 8;
            range.search_end_bit = (last ? plan_.end : starts[i + 1]) * 8;
            range.stop_bit = last ? kNoLimit : starts[i + 1] * 8;
            if (i == 0) {
                range.exact_start = BlockBoundary{deflate_begin * 8, false};
                range.empty_window = true;
            }
            schedule(group, range);
        }
        group.wait();
    }

    void schedule(TaskGroup& group, DecodeRange range) {
        group.run([this, &group, range] {
            std::unique_ptr<ChunkResult> chunk = decode(range);
            if (!chunk) {
                return;
            }
            if (chunk->reason == StopReason::kOutputBudget) {
                overflow_splits_.fetch_add(1, std::memory_order_relaxed);
                schedule_remainder(group, *chunk, range.stop_bit);
            }
            store_.insert(std::move(chunk));
        });
    }

    std::unique_ptr<ChunkResult> decode(const DecodeRange& range) {
        ChunkLimits limits;
        limits.stop_bit = range.stop_bit;
        limits.output_budget = budget_;

        if (range.exact_start) {
            if (range.empty_window) {
                return std::make_unique<ChunkResult>(
                    decode_chunk_with_window(input_, *range.exact_start, {}, limits));
            }
            return std::make_unique<ChunkResult>(
                decode_chunk_with_markers(input_, *range.exact_start, limits));
        }

        limits.output_limit = budget_ == kNoLimit ? kNoLimit : budget_ * kSpeculativeLimitFactor;
        const BlockFinder finder(input_);
        std::size_t from = range.search_begin_bit;
        while (const auto candidate = finder.find(from, range.search_end_bit)) {
            try {
                return std::make_unique<ChunkResult>(
                    decode_chunk_with_markers(input_, *candidate, limits));
            } catch (const DecodeError&) {
                // A false positive of the block finder; try the next candidate.
                rejected_candidates_.fetch_add(1, std::memory_order_relaxed);
                from = candidate->bit + 1;
            }
        }
        return nullptr;
    }

    /// Hands on the rest of an overflowing chunk: as one task (serial policy), or
    /// split speculatively into one piece per worker (M1).
    void schedule_remainder(TaskGroup& group, const ChunkResult& chunk, std::size_t stop_bit) {
        const std::size_t begin_byte = chunk.end.bit / 8;
        const std::size_t end_byte =
            stop_bit == kNoLimit ? input_.size() : std::min(stop_bit / 8, input_.size());
        std::size_t pieces = 1;
        if (options_.overflow == OverflowPolicy::kSplit && end_byte > begin_byte) {
            pieces = std::clamp<std::size_t>((end_byte - begin_byte) / kMinimumSplitPiece, 1,
                                             pool_.size());
        }

        for (std::size_t i = 0; i < pieces; ++i) {
            const std::size_t lo = begin_byte + (end_byte - begin_byte) * i / pieces;
            const std::size_t hi = begin_byte + (end_byte - begin_byte) * (i + 1) / pieces;
            DecodeRange piece;
            piece.stop_bit = i + 1 == pieces ? stop_bit : hi * 8;
            if (i == 0) {
                piece.exact_start = chunk.end;
            } else {
                piece.search_begin_bit = lo * 8;
                piece.search_end_bit = hi * 8;
            }
            schedule(group, piece);
        }
    }

    // ---- 3. Chain linking ------------------------------------------------------------

    /// Follows chunks from the stream start: each next link is the chunk that starts
    /// at the block where the previous one stopped. A missing link (the block finder
    /// returned a false positive or skipped the boundary) is decoded serially.
    void link_chain(std::size_t deflate_begin) {
        std::unique_ptr<ChunkResult> current = store_.take(BlockBoundary{deflate_begin * 8, false});
        if (!current) {
            throw DecodeError("first chunk was not decoded");
        }
        while (current->reason != StopReason::kStreamEnd) {
            const BlockBoundary next = current->end;
            chain_.push_back(std::move(current));
            current = store_.take(next);
            if (!current) {
                ++stats_.fallback_decodes;
                ChunkLimits limits;
                limits.stop_bit = next_planned_stop(next.bit);
                limits.output_budget = budget_;
                current = std::make_unique<ChunkResult>(decode_chunk_with_markers(input_, next, limits));
            }
        }
        chain_.push_back(std::move(current));

        stats_.chain_length = chain_.size();
        stats_.unused_results = store_.size();
        stats_.overflow_splits = overflow_splits_.load();
        stats_.rejected_candidates = rejected_candidates_.load();
    }

    // ---- 4. Window resolution (M3) -----------------------------------------------------

    WindowResolution resolve() {
        const SymbolWindowPtr first_map = final_window_of(chain_.front()->data);
        ByteWindow first{};
        for (std::size_t i = 0; i < kWindowSize; ++i) {
            first[i] = static_cast<std::uint8_t>(first_map->symbols[i]);
        }

        std::vector<SymbolWindowPtr> maps(chain_.size() - 1);
        parallel_for(pool_, 0, maps.size(), 1, [&](std::size_t lo, std::size_t hi) {
            for (std::size_t k = lo; k < hi; ++k) {
                maps[k] = final_window_of(chain_[k + 1]->data);
            }
        });

        // Only the resolution itself is the step under study; building the maps
        // above is independent per chunk and fully parallel.
        const Stopwatch resolution_time;
        WindowResolution resolution = resolve_windows(pool_, first, maps, options_.window_strategy);
        resolution_seconds_ = resolution_time.seconds();
        stats_.window_rounds = resolution.cost.rounds;
        stats_.window_combinations = resolution.cost.combinations;
        return resolution;
    }

    // ---- 5. Marker replacement and assembly --------------------------------------------

    DecompressionResult assemble(const WindowResolution& resolution) {
        std::vector<std::size_t> offsets(chain_.size() + 1, 0);
        for (std::size_t k = 0; k < chain_.size(); ++k) {
            offsets[k + 1] = offsets[k] + chain_[k]->data.size();
            stats_.marked_symbols += chain_[k]->data.marked_size();
        }

        DecompressionResult result;
        result.size = offsets.back();
        result.buffer = std::make_unique_for_overwrite<std::uint8_t[]>(result.size);
        stats_.decompressed_bytes = result.size;

        struct Piece {
            std::size_t chunk;
            bool marked;
            std::size_t from;
            std::size_t to;
        };
        std::vector<Piece> pieces;
        for (std::size_t k = 0; k < chain_.size(); ++k) {
            const ChunkData& data = chain_[k]->data;
            for (std::size_t from = 0; from < data.marked_size(); from += kReplaceGrain) {
                pieces.push_back({k, true, from, std::min(from + kReplaceGrain, data.marked_size())});
            }
            for (std::size_t from = 0; from < data.plain_size(); from += kCopyGrain) {
                pieces.push_back({k, false, from, std::min(from + kCopyGrain, data.plain_size())});
            }
        }

        std::uint8_t* const output = result.buffer.get();
        parallel_for(pool_, 0, pieces.size(), 1, [&](std::size_t lo, std::size_t hi) {
            for (std::size_t p = lo; p < hi; ++p) {
                const Piece& piece = pieces[p];
                const ChunkData& data = chain_[piece.chunk]->data;
                const std::size_t length = piece.to - piece.from;
                if (piece.marked) {
                    // Chunk 0 is decoded with a known window and never holds markers.
                    replace_markers(std::span(data.marked).subspan(kWindowSize + piece.from, length),
                                    resolution.windows[piece.chunk - 1],
                                    output + offsets[piece.chunk] + piece.from);
                } else {
                    std::memcpy(output + offsets[piece.chunk] + data.marked_size() + piece.from,
                                data.plain.data() + data.plain_prefix + piece.from, length);
                }
            }
        });

        // Remember where every gzip member ends before the chunks are released.
        for (std::size_t k = 0; k < chain_.size(); ++k) {
            for (const MemberEnd& end : chain_[k]->member_ends) {
                member_ends_.push_back({offsets[k] + end.output_offset, end.footer_offset});
            }
        }
        chain_.clear();
        return result;
    }

    // ---- 6. Verification ------------------------------------------------------------------

    void verify_members(const DecompressionResult& result) {
        stats_.members = member_ends_.size();
        if (member_ends_.empty()) {
            throw DecodeError("stream has no final block");
        }
        static_cast<void>(has_another_member(
            input_, member_ends_.back().footer_offset + kGzipFooterSize));
        if (!options_.verify) {
            return;
        }

        // Checksum blocks never straddle a member boundary.
        struct Block {
            std::size_t member;
            std::size_t from;
            std::size_t to;
            std::uint32_t crc = 0;
        };
        std::vector<Block> blocks;
        std::size_t member_begin = 0;
        for (std::size_t m = 0; m < member_ends_.size(); ++m) {
            const std::size_t member_end = member_ends_[m].output_offset;
            for (std::size_t from = member_begin; from < member_end; from += kChecksumGrain) {
                blocks.push_back({m, from, std::min(from + kChecksumGrain, member_end)});
            }
            member_begin = member_end;
        }

        const std::uint8_t* data = result.buffer.get();
        parallel_for(pool_, 0, blocks.size(), 1, [&](std::size_t lo, std::size_t hi) {
            for (std::size_t b = lo; b < hi; ++b) {
                blocks[b].crc = crc32_update(0, std::span(data + blocks[b].from, blocks[b].to - blocks[b].from));
            }
        });

        std::vector<std::uint32_t> member_crc(member_ends_.size(), 0);
        for (const Block& block : blocks) {
            member_crc[block.member] =
                crc32_combine(member_crc[block.member], block.crc, block.to - block.from);
        }
        member_begin = 0;
        for (std::size_t m = 0; m < member_ends_.size(); ++m) {
            const MemberEnd& end = member_ends_[m];
            check_member_footer(read_gzip_footer(input_, end.footer_offset), member_crc[m],
                                end.output_offset - member_begin);
            member_begin = end.output_offset;
        }
    }

    ThreadPool& pool_;
    const DecompressorOptions& options_;
    std::span<const std::uint8_t> input_;

    PartitionPlan plan_;
    std::size_t budget_ = kNoLimit;
    ChunkStore store_;
    std::vector<std::unique_ptr<ChunkResult>> chain_;
    std::vector<MemberEnd> member_ends_;  ///< Output offsets are global here.
    double resolution_seconds_ = 0.0;

    std::atomic<std::size_t> overflow_splits_{0};
    std::atomic<std::size_t> rejected_candidates_{0};
    DecompressionStatistics stats_;
};

}  // namespace

ParallelDecompressor::ParallelDecompressor(DecompressorOptions options)
    : options_(options), pool_(options.threads) {}

DecompressionResult ParallelDecompressor::decompress(std::span<const std::uint8_t> gzip) {
    DecompressionRun run(pool_, options_, gzip);
    return run.execute();
}

}  // namespace riptide
