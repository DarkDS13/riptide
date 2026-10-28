#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace riptide {

static_assert(std::endian::native == std::endian::little,
              "BitReader loads 64-bit words and assumes a little-endian host");

/// LSB-first bit reader over an immutable byte buffer, as required by RFC 1951.
///
/// The reader keeps up to 64 bits buffered and refills them eight bytes at a time.
/// Reading past the end of the buffer yields zero bits instead of failing, so that
/// hot decoding loops need no bounds checks; callers detect truncated input by
/// calling exhausted() at block boundaries.
class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> data, std::size_t bit_offset = 0) noexcept
        : data_(data) {
        seek(bit_offset);
    }

    /// Moves to an absolute bit offset.
    void seek(std::size_t bit_offset) noexcept {
        byte_position_ = bit_offset / 8;
        buffer_ = 0;
        bit_count_ = 0;
        refill();
        skip(static_cast<unsigned>(bit_offset % 8));
    }

    /// Absolute offset, in bits, of the next unread bit.
    [[nodiscard]] std::size_t tell() const noexcept { return byte_position_ * 8 - bit_count_; }

    [[nodiscard]] std::size_t size_in_bits() const noexcept { return data_.size() * 8; }

    /// True if more bits were consumed than the buffer contains.
    [[nodiscard]] bool exhausted() const noexcept { return tell() > size_in_bits(); }

    /// Returns the next `count` bits (count <= 32) without consuming them.
    [[nodiscard]] std::uint32_t peek(unsigned count) noexcept {
        if (bit_count_ < count) {
            refill();
        }
        return static_cast<std::uint32_t>(buffer_ & ((std::uint64_t{1} << count) - 1));
    }

    /// Consumes `count` bits that were made available by a preceding peek().
    void skip(unsigned count) noexcept {
        buffer_ >>= count;
        bit_count_ -= count;
    }

    /// Reads and consumes `count` bits (count <= 32).
    std::uint32_t read(unsigned count) noexcept {
        const std::uint32_t value = peek(count);
        skip(count);
        return value;
    }

    /// Discards bits up to the next byte boundary.
    void align_to_byte() noexcept { skip(bit_count_ % 8); }

    /// Underlying buffer, for byte-aligned copies of stored blocks.
    [[nodiscard]] std::span<const std::uint8_t> data() const noexcept { return data_; }

private:
    void refill() noexcept {
        if (byte_position_ + 8 <= data_.size()) {
            std::uint64_t word = 0;
            std::memcpy(&word, data_.data() + byte_position_, sizeof(word));
            buffer_ |= word << bit_count_;
            byte_position_ += (63 - bit_count_) / 8;
            bit_count_ |= 56;
            return;
        }
        while (bit_count_ <= 56) {
            const std::uint64_t byte = byte_position_ < data_.size() ? data_[byte_position_] : 0;
            buffer_ |= byte << bit_count_;
            ++byte_position_;
            bit_count_ += 8;
        }
    }

    std::span<const std::uint8_t> data_;
    std::size_t byte_position_ = 0;
    std::uint64_t buffer_ = 0;
    unsigned bit_count_ = 0;
};

}  // namespace riptide
