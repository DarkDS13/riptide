#include <riptide/pipeline/sequential_decompressor.hpp>

#include <algorithm>

#include <riptide/common/error.hpp>
#include <riptide/gzip/crc32.hpp>
#include <riptide/gzip/gzip_format.hpp>
#include <riptide/inflate/chunk_decoder.hpp>
#include <riptide/pipeline/member_reader.hpp>

namespace riptide {

std::vector<std::uint8_t> decompress_sequential(std::span<const std::uint8_t> gzip, bool verify) {
    std::vector<std::uint8_t> output;
    std::size_t offset = 0;

    while (has_another_member(gzip, offset)) {
        const GzipHeader header = parse_gzip_header(gzip, offset);
        const BlockBoundary start{(offset + header.size) * 8, false};
        ChunkResult member = decode_chunk_with_window(gzip, start, {}, ChunkLimits{});

        const std::size_t footer_offset = (member.end_bit + 7) / 8;
        const GzipFooter footer = read_gzip_footer(gzip, footer_offset);
        const auto& bytes = member.data.plain;
        if (verify) {
            check_member_footer(footer, crc32_update(0, bytes), bytes.size());
        }

        if (output.empty()) {
            output = std::move(member.data.plain);
        } else {
            output.insert(output.end(), bytes.begin(), bytes.end());
        }
        offset = footer_offset + kGzipFooterSize;
    }
    return output;
}

}  // namespace riptide
