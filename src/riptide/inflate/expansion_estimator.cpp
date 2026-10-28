#include <riptide/inflate/expansion_estimator.hpp>

#include <cmath>

#include <riptide/common/bit_reader.hpp>
#include <riptide/common/deflate_constants.hpp>

namespace riptide {

ExpansionEstimate estimate_expansion(const DynamicCodeLengths& lengths) noexcept {
    double total_weight = 0.0;
    double literal_weight = 0.0;
    double match_weight = 0.0;
    double match_bytes = 0.0;
    double literal_length_bits = 0.0;

    for (unsigned symbol = 0; symbol < lengths.literal_length_count; ++symbol) {
        const unsigned length = lengths.literal_length[symbol];
        if (length == 0) {
            continue;
        }
        const double weight = std::ldexp(1.0, -static_cast<int>(length));
        total_weight += weight;
        double bits = length;
        if (symbol < 256) {
            literal_weight += weight;
        } else if (symbol > kEndOfBlock && symbol - 257 < kLengthBase.size()) {
            const unsigned index = symbol - 257;
            const unsigned extra = kLengthExtraBits[index];
            // Extra bits are assumed uniform: the mean length is the middle of the range.
            const double mean_length = kLengthBase[index] + (std::ldexp(1.0, static_cast<int>(extra)) - 1.0) / 2.0;
            match_weight += weight;
            match_bytes += weight * mean_length;
            bits += extra;
        }
        literal_length_bits += weight * bits;
    }

    double distance_weight = 0.0;
    double distance_bits = 0.0;
    for (unsigned symbol = 0; symbol < lengths.distance_count && symbol < kDistanceBase.size();
         ++symbol) {
        const unsigned length = lengths.distance[symbol];
        if (length == 0) {
            continue;
        }
        const double weight = std::ldexp(1.0, -static_cast<int>(length));
        distance_weight += weight;
        distance_bits += weight * (length + kDistanceExtraBits[symbol]);
    }

    ExpansionEstimate estimate;
    if (total_weight <= 0.0) {
        return estimate;
    }
    const double match_share = match_weight / total_weight;
    const double mean_match_length = match_weight > 0.0 ? match_bytes / match_weight : 0.0;
    const double mean_distance_bits = distance_weight > 0.0 ? distance_bits / distance_weight : 0.0;

    estimate.bytes_per_symbol = literal_weight / total_weight + match_share * mean_match_length;
    estimate.bits_per_symbol = literal_length_bits / total_weight + match_share * mean_distance_bits;
    return estimate;
}

std::optional<ExpansionEstimate> estimate_block_at(std::span<const std::uint8_t> input,
                                                   std::size_t bit) {
    BitReader reader(input, bit);
    const std::uint32_t header = reader.read(3);
    if ((header >> 1) != static_cast<std::uint32_t>(BlockType::kDynamic)) {
        return std::nullopt;
    }
    DynamicCodeLengths lengths;
    HuffmanDecoder code_length_decoder;
    if (!read_dynamic_code_lengths(reader, lengths, code_length_decoder)) {
        return std::nullopt;
    }
    return estimate_expansion(lengths);
}

}  // namespace riptide
