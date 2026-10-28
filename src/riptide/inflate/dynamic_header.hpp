#pragma once

#include <array>
#include <cstdint>

#include <riptide/common/bit_reader.hpp>
#include <riptide/huffman/huffman_decoder.hpp>

namespace riptide {

/// Code lengths declared by a dynamic-Huffman block header.
struct DynamicCodeLengths {
    unsigned literal_length_count = 0;  ///< HLIT + 257
    unsigned distance_count = 0;        ///< HDIST + 1
    std::array<std::uint8_t, 288> literal_length{};
    std::array<std::uint8_t, 32> distance{};
};

/// Reads the code-length section of a dynamic block (everything after the 3-bit
/// block header) and validates it.
///
/// Returns false if the header cannot belong to a valid block. This is used both
/// by the decoder, which turns false into a DecodeError, and by the block finder,
/// for which a false result simply rejects a candidate offset.
///
/// `code_length_decoder` is scratch space for the code-length code. It is passed in
/// so that callers checking many candidate offsets reuse one decoder instead of
/// constructing (and zero-filling) a new table each time.
[[nodiscard]] bool read_dynamic_code_lengths(BitReader& reader, DynamicCodeLengths& lengths,
                                             HuffmanDecoder& code_length_decoder);

/// Builds the literal/length and distance decoders for previously read lengths.
[[nodiscard]] bool build_dynamic_decoders(const DynamicCodeLengths& lengths,
                                          HuffmanDecoder& literal_length,
                                          HuffmanDecoder& distance);

}  // namespace riptide
