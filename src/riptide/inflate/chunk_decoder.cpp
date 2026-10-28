#include <riptide/inflate/chunk_decoder.hpp>

#include <algorithm>
#include <cstring>
#include <type_traits>

#include <riptide/common/bit_reader.hpp>
#include <riptide/common/error.hpp>
#include <riptide/huffman/huffman_decoder.hpp>
#include <riptide/inflate/dynamic_header.hpp>

namespace riptide {
namespace {

constexpr std::size_t kInitialCapacity = 256 * 1024;

std::size_t align_up_to_byte(std::size_t bit) noexcept { return (bit + 7) / 8 * 8; }

/// Block-by-block DEFLATE decoder that writes 16-bit symbols while the preceding
/// window is unknown and switches to 8-bit output once it no longer matters.
class Inflater {
public:
    explicit Inflater(std::span<const std::uint8_t> input) : input_(input), reader_(input) {}

    /// Starts with an unknown window represented by identity markers.
    void begin_with_markers() {
        marked_mode_ = true;
        data_.uses_markers = true;
        data_.marked.resize(kWindowSize + kInitialCapacity);
        for (std::size_t j = 0; j < kWindowSize; ++j) {
            data_.marked[j] = static_cast<std::uint16_t>(kMarkerBase + j);
        }
        marked_size_ = kWindowSize;
        last_marker_end_ = kWindowSize;
    }

    /// Starts with a known window (possibly empty).
    void begin_with_window(std::span<const std::uint8_t> window) {
        if (window.size() > kWindowSize) {
            window = window.last(kWindowSize);
        }
        marked_mode_ = false;
        data_.plain.resize(window.size() + kInitialCapacity);
        std::copy(window.begin(), window.end(), data_.plain.begin());
        data_.plain_prefix = window.size();
        plain_size_ = window.size();
    }

    ChunkResult run(BlockBoundary start, const ChunkLimits& limits) {
        ChunkResult result;
        result.start = start;

        bool at_start = true;
        if (start.stored_payload) {
            reader_.seek(start.bit);
            copy_stored_payload();
            finish_block(result);
            at_start = false;
        } else {
            reader_.seek(start.bit);
        }

        for (;;) {
            const std::size_t block_bit = reader_.tell();
            if (block_bit >= reader_.size_in_bits()) {
                throw DecodeError("DEFLATE stream ends without a final block");
            }
            const std::uint32_t header = reader_.peek(3);
            const bool is_final = (header & 1U) != 0;
            const auto type = static_cast<BlockType>(header >> 1);

            if (!at_start && !is_final &&
                (type == BlockType::kDynamic || type == BlockType::kStored)) {
                const BlockBoundary boundary = type == BlockType::kDynamic
                                                   ? BlockBoundary{block_bit, false}
                                                   : BlockBoundary{align_up_to_byte(block_bit + 3), true};
                if (boundary.bit >= limits.stop_bit) {
                    return finish(std::move(result), StopReason::kBoundary, block_bit, boundary);
                }
                if (output_size() >= limits.output_budget) {
                    return finish(std::move(result), StopReason::kOutputBudget, block_bit, boundary);
                }
            }
            at_start = false;
            reader_.skip(3);

            switch (type) {
                case BlockType::kStored:
                    reader_.align_to_byte();
                    copy_stored_payload();
                    break;
                case BlockType::kFixed:
                    decode_block(HuffmanDecoder::fixed_literal_length(),
                                 HuffmanDecoder::fixed_distance());
                    break;
                case BlockType::kDynamic:
                    if (!read_dynamic_code_lengths(reader_, lengths_) ||
                        !build_dynamic_decoders(lengths_, literal_length_, distance_)) {
                        throw DecodeError("invalid dynamic Huffman block header");
                    }
                    decode_block(literal_length_, distance_);
                    break;
                case BlockType::kReserved:
                    throw DecodeError("reserved DEFLATE block type");
            }
            finish_block(result);

            if (is_final) {
                return finish(std::move(result), StopReason::kStreamEnd, reader_.tell(), {});
            }
        }
    }

private:
    [[nodiscard]] std::size_t output_size() const noexcept {
        const std::size_t marked = data_.uses_markers ? marked_size_ - kWindowSize : 0;
        const std::size_t plain = plain_size_ - data_.plain_prefix;
        return marked + plain;
    }

    void decode_block(const HuffmanDecoder& literal_length, const HuffmanDecoder& distance) {
        if (marked_mode_) {
            decode_symbols(literal_length, distance, data_.marked, marked_size_);
        } else {
            decode_symbols(literal_length, distance, data_.plain, plain_size_);
        }
        if (reader_.exhausted()) {
            throw DecodeError("truncated DEFLATE block");
        }
    }

    template <typename Symbol>
    void decode_symbols(const HuffmanDecoder& literal_length, const HuffmanDecoder& distance,
                        std::vector<Symbol>& buffer, std::size_t& size) {
        constexpr bool kTracksMarkers = std::is_same_v<Symbol, std::uint16_t>;
        Symbol* out = buffer.data();

        for (;;) {
            if (buffer.size() - size <= kMaxMatchLength) {
                buffer.resize(std::max(buffer.size() * 2, size + kInitialCapacity));
                out = buffer.data();
            }

            const int symbol = literal_length.decode(reader_);
            if (symbol < 256) {
                if (symbol < 0) {
                    throw DecodeError("invalid literal/length code");
                }
                out[size++] = static_cast<Symbol>(symbol);
                continue;
            }
            if (symbol == static_cast<int>(kEndOfBlock)) {
                return;
            }

            const unsigned length_index = static_cast<unsigned>(symbol) - 257;
            if (length_index >= kLengthBase.size()) {
                throw DecodeError("invalid length symbol");
            }
            const std::size_t length =
                kLengthBase[length_index] + reader_.read(kLengthExtraBits[length_index]);

            const int distance_symbol = distance.decode(reader_);
            if (distance_symbol < 0 ||
                static_cast<unsigned>(distance_symbol) >= kDistanceBase.size()) {
                throw DecodeError("invalid distance code");
            }
            const std::size_t match_distance =
                kDistanceBase[distance_symbol] + reader_.read(kDistanceExtraBits[distance_symbol]);
            if (match_distance > size) {
                throw DecodeError("back-reference distance too far back");
            }

            Symbol* destination = out + size;
            const Symbol* source = destination - match_distance;
            if constexpr (kTracksMarkers) {
                // Element-wise forward copy also handles overlapping matches
                // (distance < length). OR-ing the copied symbols tells whether any
                // marker was copied, since only markers have the top bit set.
                std::uint16_t seen = 0;
                for (std::size_t i = 0; i < length; ++i) {
                    const std::uint16_t value = source[i];
                    destination[i] = value;
                    seen = static_cast<std::uint16_t>(seen | value);
                }
                if (is_marker(seen)) {
                    last_marker_end_ = size + length;
                }
            } else if (match_distance >= length) {
                std::memcpy(destination, source, length);
            } else {
                for (std::size_t i = 0; i < length; ++i) {
                    destination[i] = source[i];
                }
            }
            size += length;
        }
    }

    /// Copies the payload of a stored block; the reader is at its LEN field.
    void copy_stored_payload() {
        const std::uint32_t length = reader_.read(16);
        const std::uint32_t complement = reader_.read(16);
        if ((length ^ 0xFFFFU) != complement) {
            throw DecodeError("stored block length check failed");
        }
        const std::size_t byte_offset = reader_.tell() / 8;
        if (byte_offset + length > input_.size()) {
            throw DecodeError("truncated stored block");
        }
        const auto payload = input_.subspan(byte_offset, length);
        if (marked_mode_) {
            append(payload, data_.marked, marked_size_);
        } else {
            append(payload, data_.plain, plain_size_);
        }
        reader_.seek((byte_offset + length) * 8);
    }

    template <typename Symbol>
    static void append(std::span<const std::uint8_t> bytes, std::vector<Symbol>& buffer,
                       std::size_t& size) {
        if (buffer.size() - size < bytes.size()) {
            buffer.resize(std::max(buffer.size() * 2, size + bytes.size()));
        }
        std::copy(bytes.begin(), bytes.end(), buffer.begin() + static_cast<std::ptrdiff_t>(size));
        size += bytes.size();
    }

    void finish_block(ChunkResult& result) {
        ++result.block_count;
        if (marked_mode_ && marked_size_ - last_marker_end_ >= kWindowSize) {
            switch_to_plain_output();
        }
    }

    /// Continues in 8-bit form once the last 32 KiB of output contain no markers.
    void switch_to_plain_output() {
        data_.plain.resize(kWindowSize + kInitialCapacity);
        const std::uint16_t* tail = data_.marked.data() + marked_size_ - kWindowSize;
        for (std::size_t i = 0; i < kWindowSize; ++i) {
            data_.plain[i] = static_cast<std::uint8_t>(tail[i]);
        }
        data_.plain_prefix = kWindowSize;
        plain_size_ = kWindowSize;
        marked_mode_ = false;
    }

    ChunkResult finish(ChunkResult result, StopReason reason, std::size_t end_bit,
                       BlockBoundary end) {
        data_.marked.resize(data_.uses_markers ? marked_size_ : 0);
        data_.plain.resize(plain_size_);
        result.reason = reason;
        result.end_bit = end_bit;
        result.end = end;
        result.data = std::move(data_);
        return result;
    }

    std::span<const std::uint8_t> input_;
    BitReader reader_;
    ChunkData data_;
    bool marked_mode_ = false;
    std::size_t marked_size_ = 0;
    std::size_t plain_size_ = 0;
    std::size_t last_marker_end_ = 0;  ///< One past the last position that may hold a marker.

    DynamicCodeLengths lengths_;
    HuffmanDecoder literal_length_;
    HuffmanDecoder distance_;
};

}  // namespace

ChunkResult decode_chunk_with_markers(std::span<const std::uint8_t> input, BlockBoundary start,
                                      const ChunkLimits& limits) {
    Inflater inflater(input);
    inflater.begin_with_markers();
    return inflater.run(start, limits);
}

ChunkResult decode_chunk_with_window(std::span<const std::uint8_t> input, BlockBoundary start,
                                     std::span<const std::uint8_t> window,
                                     const ChunkLimits& limits) {
    Inflater inflater(input);
    inflater.begin_with_window(window);
    return inflater.run(start, limits);
}

}  // namespace riptide
