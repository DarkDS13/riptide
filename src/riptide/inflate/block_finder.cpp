#include <riptide/inflate/block_finder.hpp>

#include <algorithm>
#include <cstring>

#include <riptide/common/bit_reader.hpp>

namespace riptide {
namespace {

/// Loads up to eight bytes starting at `byte_offset`, zero-padded past the end.
std::uint64_t load_word(std::span<const std::uint8_t> data, std::size_t byte_offset) noexcept {
    std::uint64_t word = 0;
    if (byte_offset < data.size()) {
        std::memcpy(&word, data.data() + byte_offset,
                    std::min<std::size_t>(sizeof(word), data.size() - byte_offset));
    }
    return word;
}

/// Cheap pre-filter on the first 13 bits of a candidate header: BFINAL = 0,
/// BTYPE = 2 (dynamic), and HLIT and HDIST within their valid ranges. It rejects
/// about 89% of bit offsets before any table is read; the code-length code check in
/// is_dynamic_header() then rejects nearly all of the rest.
constexpr bool passes_header_prefilter(std::uint64_t bits) noexcept {
    const bool non_final_dynamic = (bits & 0x7U) == 0x4U;
    const auto hlit = (bits >> 3) & 0x1FU;
    const auto hdist = (bits >> 8) & 0x1FU;
    return non_final_dynamic && hlit < 30 && hdist < 30;
}

/// Second filter: the code-length code, up to 19 lengths of 3 bits starting at
/// bit 17 of the header, must be complete (its Kraft sum must be exactly one). One
/// unaligned load holds all of it, so most of the candidates that pass the
/// pre-filter are rejected without constructing a bit reader or a decoder.
bool passes_code_length_filter(std::span<const std::uint8_t> data, std::size_t bit) noexcept {
    const std::uint64_t header = load_word(data, bit / 8) >> (bit % 8);
    const auto count = static_cast<unsigned>((header >> 13) & 0xFU) + 4;
    const std::size_t lengths_bit = bit + 17;
    const std::uint64_t lengths = load_word(data, lengths_bit / 8) >> (lengths_bit % 8);
    unsigned kraft = 0;  // in units of 2^-7
    for (unsigned i = 0; i < count; ++i) {
        const auto length = static_cast<unsigned>((lengths >> (3 * i)) & 0x7U);
        if (length != 0) {
            kraft += 128U >> length;
        }
    }
    return kraft == 128;
}

/// Length of one search segment of find(): 64 KiB of input.
constexpr std::size_t kSegmentBits = 64 * 1024 * 8;

}  // namespace

std::optional<BlockBoundary> BlockFinder::find(std::size_t begin_bit, std::size_t end_bit) const {
    // Search in segments, so that the cost is proportional to the distance to the
    // first candidate of either kind. Searching the whole range for dynamic headers
    // first scans every bit of incompressible regions, which hold only stored
    // blocks; searching it for stored blocks first scans every byte of compressible
    // regions, which hold none.
    for (std::size_t from = begin_bit; from < end_bit; from += kSegmentBits) {
        const std::size_t to = std::min(from + kSegmentBits, end_bit);
        const auto stored = find_stored(from, to);
        if (const auto dynamic = find_dynamic(from, stored ? *stored : to)) {
            return BlockBoundary{*dynamic, false};
        }
        if (stored) {
            return BlockBoundary{*stored, true};
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> BlockFinder::find_dynamic(std::size_t begin_bit,
                                                     std::size_t end_bit) const {
    end_bit = std::min(end_bit, input_.size() * 8);
    Scratch scratch;
    std::size_t bit = begin_bit;
    while (bit < end_bit) {
        const std::size_t byte = bit / 8;
        const std::uint64_t word = load_word(input_, byte);
        for (unsigned shift = static_cast<unsigned>(bit % 8); shift < 8 && bit < end_bit;
             ++shift, ++bit) {
            if (passes_header_prefilter(word >> shift) && passes_code_length_filter(input_, bit) &&
                is_dynamic_header(bit, scratch)) {
                return bit;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> BlockFinder::find_stored(std::size_t begin_bit,
                                                    std::size_t end_bit) const {
    // The LEN field is byte-aligned, and the three bits before it are either the
    // block header (BFINAL = 0, BTYPE = 00) or zero padding.
    std::size_t byte = std::max<std::size_t>((begin_bit + 7) / 8, 1);
    const std::size_t end_byte = std::min((end_bit + 7) / 8, input_.size());
    for (; byte + 4 <= input_.size() && byte < end_byte; ++byte) {
        if ((input_[byte - 1] >> 5) != 0) {
            continue;
        }
        const unsigned length = input_[byte] | (unsigned{input_[byte + 1]} << 8);
        const unsigned complement = input_[byte + 2] | (unsigned{input_[byte + 3]} << 8);
        if ((length ^ 0xFFFFU) == complement && byte + 4 + length <= input_.size()) {
            return byte * 8;
        }
    }
    return std::nullopt;
}

bool BlockFinder::is_dynamic_header(std::size_t bit, Scratch& scratch) const {
    BitReader reader(input_, bit + 3);
    return read_dynamic_code_lengths(reader, scratch.lengths, scratch.code_length) &&
           build_dynamic_decoders(scratch.lengths, scratch.literal_length, scratch.distance);
}

}  // namespace riptide
