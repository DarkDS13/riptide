#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <riptide/common/bit_reader.hpp>
#include <riptide/common/deflate_constants.hpp>

namespace riptide {

/// Table-driven decoder for a canonical Huffman code (RFC 1951, section 3.2.2).
///
/// Codes of up to kRootBits bits are resolved with a single lookup in a
/// 2^kRootBits-entry root table. Longer codes (up to 15 bits) go through a second
/// lookup in a 2^(15 - kRootBits)-entry subtable reached from the root entry.
class HuffmanDecoder {
public:
    static constexpr unsigned kRootBits = 10;

    /// How strictly the code lengths must describe a complete prefix code.
    enum class Completeness {
        /// The code must be complete (used for the code-length code).
        kRequired,
        /// A single code of length 1 may leave the code incomplete, as zlib allows.
        kAllowSingleCode,
        /// Like kAllowSingleCode, and an empty code is also accepted (distance codes
        /// of a block that contains only literals).
        kAllowEmptyOrSingle,
    };

    /// Builds the tables from per-symbol code lengths (0 = symbol unused).
    /// Returns false if the lengths do not describe a valid code under `rule`.
    [[nodiscard]] bool build(std::span<const std::uint8_t> code_lengths, Completeness rule);

    /// Decodes and consumes one symbol. Returns -1 for a bit pattern that is not a code.
    [[nodiscard]] int decode(BitReader& reader) const noexcept {
        const std::uint32_t bits = reader.peek(kMaxCodeLength);
        Entry entry = root_[bits & kRootMask];
        if (entry.length == kSubtableLink) {
            entry = subtables_[entry.value + ((bits >> kRootBits) & kSubtableMask)];
        }
        if (entry.length == 0) {
            return -1;
        }
        reader.skip(entry.length);
        return entry.value;
    }

    /// Decoder for the fixed literal/length code of block type 1.
    [[nodiscard]] static const HuffmanDecoder& fixed_literal_length();

    /// Decoder for the fixed distance code of block type 1.
    [[nodiscard]] static const HuffmanDecoder& fixed_distance();

private:
    struct Entry {
        std::uint16_t value = 0;  ///< Symbol, or subtable offset for a link entry.
        std::uint8_t length = 0;  ///< Code length in bits; 0 = invalid code.
    };

    static constexpr std::uint8_t kSubtableLink = 0xFF;
    static constexpr std::uint32_t kRootMask = (1U << kRootBits) - 1;
    static constexpr unsigned kSubtableBits = kMaxCodeLength - kRootBits;
    static constexpr std::uint32_t kSubtableMask = (1U << kSubtableBits) - 1;

    std::array<Entry, 1U << kRootBits> root_{};
    std::vector<Entry> subtables_;
};

}  // namespace riptide
