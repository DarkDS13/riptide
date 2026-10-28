#include <riptide/pipeline/sequential_decompressor.hpp>

#include <riptide/gzip/crc32.hpp>
#include <riptide/gzip/gzip_format.hpp>
#include <riptide/inflate/chunk_decoder.hpp>
#include <riptide/pipeline/member_reader.hpp>

namespace riptide {

Buffer<std::uint8_t> decompress_sequential(std::span<const std::uint8_t> gzip, bool verify) {
    if (!has_another_member(gzip, 0)) {
        return {};
    }
    const GzipHeader header = parse_gzip_header(gzip, 0);
    ChunkResult stream =
        decode_chunk_with_window(gzip, BlockBoundary{header.size * 8, false}, {}, ChunkLimits{});
    Buffer<std::uint8_t> output = std::move(stream.data.plain);

    std::size_t member_begin = 0;
    for (const MemberEnd& end : stream.member_ends) {
        if (verify) {
            const std::span<const std::uint8_t> member(output.data() + member_begin,
                                                       end.output_offset - member_begin);
            check_member_footer(read_gzip_footer(gzip, end.footer_offset),
                                crc32_update(0, member), member.size());
        }
        member_begin = end.output_offset;
    }
    // The decoder followed every member that starts with the gzip magic; this only
    // rejects trailing data that is neither a member nor zero padding.
    static_cast<void>(
        has_another_member(gzip, stream.member_ends.back().footer_offset + kGzipFooterSize));
    return output;
}

}  // namespace riptide
