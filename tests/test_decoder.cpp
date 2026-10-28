// Tests of the DEFLATE decoder: sequential decoding, two-stage decoding with
// markers, stop conditions, the block finder and the expansion estimator.

#include <zlib.h>

#include <algorithm>
#include <random>
#include <tuple>

#include <riptide/common/error.hpp>
#include <riptide/gzip/gzip_format.hpp>
#include <riptide/inflate/block_finder.hpp>
#include <riptide/inflate/chunk_decoder.hpp>
#include <riptide/inflate/expansion_estimator.hpp>
#include <riptide/pipeline/sequential_decompressor.hpp>
#include <riptide/window/window.hpp>

#include "test_data.hpp"
#include "test_framework.hpp"

using namespace riptide;
using riptide::test::Bytes;

namespace {

/// Inputs that together exercise every block type and the corner cases of DEFLATE.
std::vector<std::pair<std::string, Bytes>> corpus() {
    std::vector<std::pair<std::string, Bytes>> files;
    const Bytes text = test::text_like(300'000, 1);
    files.emplace_back("text-level6", test::gzip_compress(text));
    files.emplace_back("text-level1", test::gzip_compress(text, test::settings(1)));
    files.emplace_back("text-level9", test::gzip_compress(text, test::settings(9)));
    files.emplace_back("random-stored", test::gzip_compress(test::random_bytes(200'000, 2)));
    files.emplace_back("level0-stored", test::gzip_compress(text, test::settings(0)));
    files.emplace_back("fixed-blocks", test::gzip_compress(text, test::settings(6, Z_FIXED)));
    files.emplace_back("huffman-only", test::gzip_compress(text, test::settings(6, Z_HUFFMAN_ONLY)));
    files.emplace_back("rle", test::gzip_compress(test::repetitive(300'000, 3), test::settings(6, Z_RLE)));
    files.emplace_back("overlapping-copies", test::gzip_compress(test::repetitive(400'000, 4)));
    files.emplace_back("full-flush", test::gzip_compress(text, test::settings(6, 0, 20'000)));
    files.emplace_back("mixed", test::gzip_compress(test::mixed(1'500'000, 5)));
    files.emplace_back("empty", test::gzip_compress(Bytes{}));
    files.emplace_back("one-byte", test::gzip_compress(Bytes{'x'}));
    files.emplace_back("multi-member",
                       test::concatenate({test::gzip_compress(test::text_like(50'000, 6)),
                                          test::gzip_compress(test::random_bytes(10'000, 7)),
                                          test::gzip_compress(Bytes{})}));
    return files;
}

/// Bit offsets of every non-final dynamic/stored block boundary, found by decoding
/// sequentially and stopping at each one in turn.
std::vector<BlockBoundary> boundaries_of(const Bytes& gzip) {
    std::vector<BlockBoundary> boundaries;
    const GzipHeader header = parse_gzip_header(gzip, 0);
    BlockBoundary position{header.size * 8, false};
    for (;;) {
        ChunkLimits limits;
        limits.stop_bit = position.bit + 1;
        const ChunkResult chunk = decode_chunk_with_markers(gzip, position, limits);
        if (chunk.reason != StopReason::kBoundary) {
            return boundaries;
        }
        boundaries.push_back(chunk.end);
        position = chunk.end;
    }
}

}  // namespace

RIPTIDE_TEST(decoder, sequential_matches_zlib) {
    for (const auto& [name, gzip] : corpus()) {
        const Bytes expected = test::zlib_decompress(gzip);
        const Bytes actual = decompress_sequential(gzip);
        if (actual != expected) {
            test::report_failure(__FILE__, __LINE__, "sequential output differs for " + name);
        }
    }
}

RIPTIDE_TEST(decoder, two_stage_decoding_matches_zlib) {
    // Decode each file as two chunks split at a real block boundary: the second
    // with markers, then resolve them with the first chunk's final window.
    for (const auto& [name, gzip] : corpus()) {
        const auto boundaries = boundaries_of(gzip);
        if (boundaries.empty()) {
            continue;
        }
        const BlockBoundary middle = boundaries[boundaries.size() / 2];
        const GzipHeader header = parse_gzip_header(gzip, 0);

        ChunkLimits first_limits;
        first_limits.stop_bit = middle.bit;
        const ChunkResult first =
            decode_chunk_with_window(gzip, BlockBoundary{header.size * 8, false}, {}, first_limits);
        const ChunkResult second = decode_chunk_with_markers(gzip, middle, ChunkLimits{});
        CHECK(first.end == middle);

        const ByteWindow before_stream{};
        ByteWindow window{};
        apply(*final_window_of(first.data), before_stream, window);

        Bytes output(first.data.plain.begin() + static_cast<std::ptrdiff_t>(first.data.plain_prefix),
                     first.data.plain.end());
        Bytes resolved(second.data.marked_size());
        replace_markers(std::span(second.data.marked).subspan(kWindowSize), window, resolved.data());
        output.insert(output.end(), resolved.begin(), resolved.end());
        output.insert(output.end(),
                      second.data.plain.begin() + static_cast<std::ptrdiff_t>(second.data.plain_prefix),
                      second.data.plain.end());

        if (output != test::zlib_decompress(gzip)) {
            test::report_failure(__FILE__, __LINE__, "two-stage output differs for " + name);
        }
    }
}

RIPTIDE_TEST(decoder, switches_to_8_bit_output_when_markers_end) {
    // Random data is stored; nothing refers back, so markers end immediately.
    const Bytes gzip = test::gzip_compress(test::concatenate(
        {test::text_like(100'000, 1), test::random_bytes(300'000, 2)}));
    const auto boundaries = boundaries_of(gzip);
    CHECK(!boundaries.empty());
    const ChunkResult chunk = decode_chunk_with_markers(gzip, boundaries.front(), ChunkLimits{});
    CHECK(chunk.data.plain_size() > 0);
    CHECK_EQ(chunk.data.plain_prefix, kWindowSize);
}

RIPTIDE_TEST(decoder, output_budget_stops_at_a_block_boundary) {
    const Bytes gzip = test::gzip_compress(test::text_like(2'000'000, 9));
    const GzipHeader header = parse_gzip_header(gzip, 0);
    ChunkLimits limits;
    limits.output_budget = 100'000;
    const ChunkResult chunk =
        decode_chunk_with_window(gzip, BlockBoundary{header.size * 8, false}, {}, limits);
    CHECK(chunk.reason == StopReason::kOutputBudget);
    CHECK(chunk.data.size() >= limits.output_budget);
    const auto boundaries = boundaries_of(gzip);
    CHECK(std::find(boundaries.begin(), boundaries.end(), chunk.end) != boundaries.end());
}

RIPTIDE_TEST(decoder, rejects_corrupted_input) {
    const Bytes gzip = test::gzip_compress(test::text_like(200'000, 3));
    std::mt19937 engine(11);
    int detected = 0;
    for (int trial = 0; trial < 40; ++trial) {
        Bytes corrupted = gzip;
        const std::size_t position = 20 + engine() % (gzip.size() - 40);
        corrupted[position] ^= static_cast<std::uint8_t>(1U << (engine() % 8));
        try {
            static_cast<void>(decompress_sequential(corrupted));
        } catch (const DecodeError&) {
            ++detected;  // invalid DEFLATE data or a checksum mismatch
        }
    }
    CHECK_EQ(detected, 40);
}

RIPTIDE_TEST(decoder, rejects_truncated_input) {
    const Bytes gzip = test::gzip_compress(test::text_like(100'000, 4));
    for (const std::size_t keep : {gzip.size() / 4, gzip.size() / 2, gzip.size() - 9}) {
        const Bytes truncated(gzip.begin(), gzip.begin() + static_cast<std::ptrdiff_t>(keep));
        CHECK_THROWS(decompress_sequential(truncated), DecodeError);
    }
}

RIPTIDE_TEST(block_finder, recognises_every_real_boundary) {
    for (const auto& [name, gzip] : corpus()) {
        const BlockFinder finder(gzip);
        for (const BlockBoundary& boundary : boundaries_of(gzip)) {
            const auto found = finder.find(boundary.bit, boundary.bit + 1);
            if (!found || !(*found == boundary)) {
                test::report_failure(__FILE__, __LINE__, "missed a block boundary in " + name);
            }
        }
    }
}

RIPTIDE_TEST(block_finder, rarely_reports_false_positives_in_random_data) {
    const Bytes noise = test::random_bytes(1'000'000, 99);
    const BlockFinder finder(noise);
    std::size_t candidates = 0;
    for (auto found = finder.find_dynamic(0, noise.size() * 8); found;
         found = finder.find_dynamic(*found + 1, noise.size() * 8)) {
        ++candidates;
    }
    // The base paper measured about one valid-looking dynamic header per 5 * 10^9
    // bit offsets in random data; 8 * 10^6 offsets should yield none or a handful.
    CHECK(candidates < 5);
}

RIPTIDE_TEST(expansion_estimator, predicts_block_expansion) {
    for (const auto& [data, low, high] :
         {std::tuple{test::text_like(1'000'000, 5), 2.0, 5.0},
          std::tuple{test::repetitive(1'000'000, 6), 20.0, 1000.0}}) {
        const Bytes gzip = test::gzip_compress(data);
        const GzipHeader header = parse_gzip_header(gzip, 0);
        const auto estimate = estimate_block_at(gzip, header.size * 8);
        CHECK(estimate.has_value());
        if (estimate) {
            CHECK(estimate->expansion_ratio() > low);
            CHECK(estimate->expansion_ratio() < high);
        }
    }
}
