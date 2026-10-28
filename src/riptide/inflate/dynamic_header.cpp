#include <riptide/inflate/dynamic_header.hpp>

#include <algorithm>
#include <span>

#include <riptide/common/deflate_constants.hpp>

namespace riptide {

bool read_dynamic_code_lengths(BitReader& reader, DynamicCodeLengths& lengths,
                               HuffmanDecoder& code_length_decoder) {
    lengths.literal_length_count = reader.read(5) + 257;
    lengths.distance_count = reader.read(5) + 1;
    const unsigned code_length_count = reader.read(4) + 4;
    if (lengths.literal_length_count > kMaxLiteralLengthCodes ||
        lengths.distance_count > kMaxDistanceCodes) {
        return false;
    }

    std::array<std::uint8_t, kCodeLengthCodes> code_length_lengths{};
    for (unsigned i = 0; i < code_length_count; ++i) {
        code_length_lengths[kCodeLengthOrder[i]] = static_cast<std::uint8_t>(reader.read(3));
    }
    if (!code_length_decoder.build(code_length_lengths, HuffmanDecoder::Completeness::kRequired)) {
        return false;
    }

    // Literal/length and distance code lengths form one sequence, so a repeat
    // code may run from the end of the first table into the second.
    const unsigned total = lengths.literal_length_count + lengths.distance_count;
    std::array<std::uint8_t, 288 + 32> sequence{};
    unsigned filled = 0;
    while (filled < total) {
        const int symbol = code_length_decoder.decode(reader);
        if (symbol < 0) {
            return false;
        }
        if (symbol < 16) {
            sequence[filled++] = static_cast<std::uint8_t>(symbol);
            continue;
        }

        std::uint8_t value = 0;
        unsigned repeat = 0;
        if (symbol == 16) {
            if (filled == 0) {
                return false;
            }
            value = sequence[filled - 1];
            repeat = 3 + reader.read(2);
        } else if (symbol == 17) {
            repeat = 3 + reader.read(3);
        } else {
            repeat = 11 + reader.read(7);
        }
        if (filled + repeat > total) {
            return false;
        }
        std::fill_n(sequence.begin() + filled, repeat, value);
        filled += repeat;
    }

    lengths.literal_length.fill(0);
    lengths.distance.fill(0);
    std::copy_n(sequence.begin(), lengths.literal_length_count, lengths.literal_length.begin());
    std::copy_n(sequence.begin() + lengths.literal_length_count, lengths.distance_count,
                lengths.distance.begin());

    // A block without an end-of-block code could never terminate.
    return lengths.literal_length[kEndOfBlock] != 0 && !reader.exhausted();
}

bool build_dynamic_decoders(const DynamicCodeLengths& lengths, HuffmanDecoder& literal_length,
                            HuffmanDecoder& distance) {
    using Completeness = HuffmanDecoder::Completeness;
    return literal_length.build(
               std::span(lengths.literal_length.data(), lengths.literal_length_count),
               Completeness::kAllowSingleCode) &&
           distance.build(std::span(lengths.distance.data(), lengths.distance_count),
                          Completeness::kAllowEmptyOrSingle);
}

}  // namespace riptide
