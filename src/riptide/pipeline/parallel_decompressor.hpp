#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include <riptide/parallel/thread_pool.hpp>
#include <riptide/pipeline/options.hpp>
#include <riptide/pipeline/statistics.hpp>

namespace riptide {

struct DecompressionResult {
    std::unique_ptr<std::uint8_t[]> buffer;
    std::size_t size = 0;
    DecompressionStatistics statistics;

    [[nodiscard]] std::span<const std::uint8_t> data() const noexcept { return {buffer.get(), size}; }
};

/// Parallel gzip decompressor.
///
/// Pipeline (each stage is timed separately in DecompressionStatistics):
///
///   1. Partition   cut the compressed stream into chunks (fixed or work-balanced);
///   2. Decode      every chunk in parallel: find a block boundary, then decode
///                  with markers for the unknown preceding window. A chunk that
///                  overflows its output budget is split across idle workers;
///   3. Chain       link chunks whose end and start denote the same block, decoding
///                  any gap serially (false positives of the block finder);
///   4. Windows     resolve the window before every chunk, serially or by a
///                  parallel prefix scan over window maps (M3);
///   5. Replace     resolve markers and assemble the output, in parallel;
///   6. Verify      CRC-32 of every gzip member, in parallel via crc32_combine.
///
/// The output is identical to that of zlib; any mismatch with a member's footer
/// raises ChecksumError, and invalid input raises DecodeError.
class ParallelDecompressor {
public:
    explicit ParallelDecompressor(DecompressorOptions options = {});

    [[nodiscard]] DecompressionResult decompress(std::span<const std::uint8_t> gzip);

    [[nodiscard]] const DecompressorOptions& options() const noexcept { return options_; }

private:
    DecompressorOptions options_;
    ThreadPool pool_;
};

}  // namespace riptide
