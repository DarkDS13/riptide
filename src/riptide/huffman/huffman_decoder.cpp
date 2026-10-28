#include <riptide/huffman/huffman_decoder.hpp>

#include <algorithm>

namespace riptide {
namespace {

/// Reverses the lowest `length` bits of `code`. DEFLATE stores Huffman codes
/// starting from their most significant bit, while the bit reader is LSB-first.
std::uint32_t reverse_bits(std::uint32_t code, unsigned length) noexcept {
    std::uint32_t reversed = 0;
    for (unsigned i = 0; i < length; ++i) {
        reversed = (reversed << 1) | ((code >> i) & 1U);
    }
    return reversed;
}

}  // namespace

bool HuffmanDecoder::build(std::span<const std::uint8_t> code_lengths, Completeness rule) {
    std::array<unsigned, kMaxCodeLength + 1> count{};
    unsigned used_symbols = 0;
    unsigned longest = 0;
    for (const std::uint8_t length : code_lengths) {
        if (length > kMaxCodeLength) {
            return false;
        }
        if (length != 0) {
            ++count[length];
            ++used_symbols;
            longest = std::max<unsigned>(longest, length);
        }
    }

    root_.fill(Entry{});
    subtables_.clear();

    if (used_symbols == 0) {
        return rule == Completeness::kAllowEmptyOrSingle;
    }

    // Check the Kraft inequality: an over-subscribed code is never valid, and an
    // incomplete one only in the single-code case that zlib also accepts.
    int unused_codes = 1;
    for (unsigned length = 1; length <= kMaxCodeLength; ++length) {
        unused_codes = unused_codes * 2 - static_cast<int>(count[length]);
        if (unused_codes < 0) {
            return false;
        }
    }
    if (unused_codes > 0 && (rule == Completeness::kRequired || longest != 1)) {
        return false;
    }

    // First canonical code of each length.
    std::array<std::uint32_t, kMaxCodeLength + 2> next_code{};
    std::uint32_t code = 0;
    for (unsigned length = 1; length <= kMaxCodeLength; ++length) {
        code = (code + count[length - 1]) << 1;
        next_code[length] = code;
    }

    for (std::size_t symbol = 0; symbol < code_lengths.size(); ++symbol) {
        const unsigned length = code_lengths[symbol];
        if (length == 0) {
            continue;
        }
        const std::uint32_t reversed = reverse_bits(next_code[length]++, length);
        const Entry entry{static_cast<std::uint16_t>(symbol), static_cast<std::uint8_t>(length)};

        if (length <= kRootBits) {
            for (std::uint32_t index = reversed; index < root_.size(); index += 1U << length) {
                root_[index] = entry;
            }
            continue;
        }

        Entry& link = root_[reversed & kRootMask];
        if (link.length != kSubtableLink) {
            link.value = static_cast<std::uint16_t>(subtables_.size());
            link.length = kSubtableLink;
            subtables_.resize(subtables_.size() + (1U << kSubtableBits));
        }
        const unsigned sub_length = length - kRootBits;
        for (std::uint32_t index = reversed >> kRootBits; index <= kSubtableMask;
             index += 1U << sub_length) {
            subtables_[link.value + index] = entry;
        }
    }
    return true;
}

const HuffmanDecoder& HuffmanDecoder::fixed_literal_length() {
    static const HuffmanDecoder decoder = [] {
        std::array<std::uint8_t, 288> lengths{};
        std::fill(lengths.begin(), lengths.begin() + 144, std::uint8_t{8});
        std::fill(lengths.begin() + 144, lengths.begin() + 256, std::uint8_t{9});
        std::fill(lengths.begin() + 256, lengths.begin() + 280, std::uint8_t{7});
        std::fill(lengths.begin() + 280, lengths.end(), std::uint8_t{8});
        HuffmanDecoder result;
        [[maybe_unused]] const bool ok = result.build(lengths, Completeness::kRequired);
        return result;
    }();
    return decoder;
}

const HuffmanDecoder& HuffmanDecoder::fixed_distance() {
    static const HuffmanDecoder decoder = [] {
        // RFC 1951 assigns 5-bit codes to all 32 distance symbols; 30 and 31 never
        // occur in valid data and are rejected by the inflater.
        std::array<std::uint8_t, 32> lengths{};
        lengths.fill(5);
        HuffmanDecoder result;
        [[maybe_unused]] const bool ok = result.build(lengths, Completeness::kRequired);
        return result;
    }();
    return decoder;
}

}  // namespace riptide
