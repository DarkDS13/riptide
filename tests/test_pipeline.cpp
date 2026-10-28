// End-to-end tests of the parallel decompressor against zlib.

#include <zlib.h>

#include <cstdlib>
#include <random>
#include <string>

#include <riptide/common/error.hpp>
#include <riptide/io/mapped_file.hpp>
#include <riptide/pipeline/parallel_decompressor.hpp>
#include <riptide/pipeline/partitioner.hpp>

#include "test_data.hpp"
#include "test_framework.hpp"

using namespace riptide;
using riptide::test::Bytes;

namespace {

bool decompresses_correctly(const Bytes& gzip, const DecompressorOptions& options,
                            DecompressionStatistics* statistics = nullptr) {
    ParallelDecompressor decompressor(options);
    const DecompressionResult result = decompressor.decompress(gzip);
    if (statistics != nullptr) {
        *statistics = result.statistics;
    }
    const Bytes expected = test::zlib_decompress(gzip);
    const auto actual = result.data();
    return actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin());
}

DecompressorOptions small_chunks(std::size_t chunk_size, std::size_t threads = 4) {
    DecompressorOptions options;
    options.threads = threads;
    options.chunk_size = chunk_size;
    return options;
}

}  // namespace

RIPTIDE_TEST(pipeline, every_window_strategy_matches_zlib) {
    const Bytes gzip = test::gzip_compress(test::mixed(3'000'000, 1));
    for (const auto strategy : {WindowStrategy::kSerial, WindowStrategy::kHillisSteele,
                                WindowStrategy::kBlelloch, WindowStrategy::kBlocked}) {
        auto options = small_chunks(32 * 1024);
        options.window_strategy = strategy;
        DecompressionStatistics statistics;
        CHECK(decompresses_correctly(gzip, options, &statistics));
        CHECK(statistics.chain_length > 10);
    }
}

RIPTIDE_TEST(pipeline, chunk_sizes_and_thread_counts) {
    const Bytes gzip = test::gzip_compress(test::text_like(2'000'000, 2));
    for (const std::size_t chunk : {std::size_t{4096}, std::size_t{20'000}, std::size_t{100'000},
                                    std::size_t{4} << 20}) {
        for (const std::size_t threads : {1U, 2U, 7U}) {
            CHECK(decompresses_correctly(gzip, small_chunks(chunk, threads)));
        }
    }
}

RIPTIDE_TEST(pipeline, every_block_type_and_compression_setting) {
    const Bytes text = test::text_like(800'000, 3);
    for (const test::GzipSettings& settings :
         {test::settings(1), test::settings(9), test::settings(0), test::settings(6, Z_FIXED),
          test::settings(6, Z_HUFFMAN_ONLY), test::settings(6, Z_RLE), test::settings(6, 0, 30'000)}) {
        CHECK(decompresses_correctly(test::gzip_compress(text, settings), small_chunks(16 * 1024)));
    }
    CHECK(decompresses_correctly(test::gzip_compress(test::random_bytes(500'000, 4)),
                                 small_chunks(16 * 1024)));
}

RIPTIDE_TEST(pipeline, multi_member_files) {
    std::vector<Bytes> members;
    std::mt19937 engine(5);
    for (int i = 0; i < 40; ++i) {
        members.push_back(test::gzip_compress(test::mixed(20'000 + engine() % 200'000, engine())));
    }
    members.push_back(test::gzip_compress(Bytes{}));
    const Bytes gzip = test::concatenate(members);
    DecompressionStatistics statistics;
    CHECK(decompresses_correctly(gzip, small_chunks(64 * 1024), &statistics));
    CHECK_EQ(statistics.members, members.size());
}

RIPTIDE_TEST(pipeline, overflow_policies) {
    // Text expands about 3x, so a budget of 1.5x the compressed chunk size makes
    // nearly every chunk overflow at one of its many block boundaries.
    const Bytes gzip = test::gzip_compress(test::text_like(4'000'000, 6));
    for (const auto policy : {OverflowPolicy::kSerial, OverflowPolicy::kSplit}) {
        auto options = small_chunks(64 * 1024);
        options.overflow = policy;
        options.max_expansion = 1.5;
        DecompressionStatistics statistics;
        CHECK(decompresses_correctly(gzip, options, &statistics));
        CHECK(statistics.overflow_splits > 0);
    }
}

RIPTIDE_TEST(pipeline, work_balanced_chunking) {
    const Bytes gzip = test::gzip_compress(test::mixed(6'000'000, 7));
    auto options = small_chunks(256 * 1024);
    options.chunking = ChunkingPolicy::kWorkBalanced;
    options.sample_interval = 32 * 1024;
    DecompressionStatistics statistics;
    CHECK(decompresses_correctly(gzip, options, &statistics));
    CHECK(statistics.planned_chunks > 1);
}

RIPTIDE_TEST(pipeline, work_balanced_plan_is_ordered_and_covers_the_input) {
    const Bytes gzip = test::gzip_compress(test::mixed(4'000'000, 8));
    ThreadPool pool(4);
    const PartitionPlan plan = plan_work_balanced_chunks(pool, gzip, 20, gzip.size(), 100'000, 16'384);
    CHECK_EQ(plan.starts.front(), 20U);
    CHECK(std::is_sorted(plan.starts.begin(), plan.starts.end()));
    CHECK(plan.starts.back() < gzip.size());
    CHECK(plan.samples > 0);
}

RIPTIDE_TEST(pipeline, detects_checksum_and_size_corruption) {
    Bytes gzip = test::gzip_compress(test::text_like(500'000, 9));
    Bytes bad_crc = gzip;
    bad_crc[bad_crc.size() - 8] ^= 0x01;
    CHECK_THROWS(ParallelDecompressor(small_chunks(32 * 1024)).decompress(bad_crc), ChecksumError);
    Bytes bad_size = gzip;
    bad_size[bad_size.size() - 1] ^= 0x01;
    CHECK_THROWS(ParallelDecompressor(small_chunks(32 * 1024)).decompress(bad_size), ChecksumError);
}

RIPTIDE_TEST(pipeline, rejects_corrupted_and_truncated_input) {
    const Bytes gzip = test::gzip_compress(test::mixed(1'000'000, 10));
    std::mt19937 engine(12);
    ParallelDecompressor decompressor(small_chunks(32 * 1024));
    for (int trial = 0; trial < 25; ++trial) {
        Bytes corrupted = gzip;
        corrupted[20 + engine() % (gzip.size() - 40)] ^= static_cast<std::uint8_t>(1U << (engine() % 8));
        CHECK_THROWS(decompressor.decompress(corrupted), DecodeError);
    }
    const Bytes truncated(gzip.begin(), gzip.begin() + static_cast<std::ptrdiff_t>(gzip.size() / 2));
    CHECK_THROWS(decompressor.decompress(truncated), DecodeError);
}

RIPTIDE_TEST(pipeline, trailing_zeros_are_accepted_and_garbage_rejected) {
    Bytes gzip = test::gzip_compress(test::text_like(100'000, 13));
    Bytes padded = gzip;
    padded.resize(padded.size() + 512, 0);
    CHECK(decompresses_correctly(padded, small_chunks(16 * 1024)));
    Bytes garbage = gzip;
    garbage.push_back('!');
    CHECK_THROWS(ParallelDecompressor(small_chunks(16 * 1024)).decompress(garbage), GzipFormatError);
}

RIPTIDE_TEST(pipeline, silesia_if_available) {
    // Set RIPTIDE_SILESIA to the gzipped Silesia tarball to run this test.
    const char* path = std::getenv("RIPTIDE_SILESIA");
    if (path == nullptr) {
        return;
    }
    const MappedFile file(path);
    const Bytes gzip(file.bytes().begin(), file.bytes().end());
    for (const std::size_t chunk : {std::size_t{64} << 10, std::size_t{4} << 20}) {
        CHECK(decompresses_correctly(gzip, small_chunks(chunk, ThreadPool::hardware_threads())));
    }
}
