// Tests of the building blocks: bit reader, Huffman decoder, CRC-32, gzip parsing.

#include <zlib.h>

#include <array>
#include <random>

#include <riptide/common/bit_reader.hpp>
#include <riptide/common/error.hpp>
#include <riptide/gzip/crc32.hpp>
#include <riptide/gzip/gzip_format.hpp>
#include <riptide/huffman/huffman_decoder.hpp>

#include "test_data.hpp"
#include "test_framework.hpp"

using namespace riptide;
using riptide::test::Bytes;

RIPTIDE_TEST(bit_reader, reads_lsb_first) {
    const std::array<std::uint8_t, 3> data = {0b1011'0101, 0b0000'1111, 0xFF};
    BitReader reader(data);
    CHECK_EQ(reader.read(1), 1U);
    CHECK_EQ(reader.read(3), 0b010U);
    CHECK_EQ(reader.read(8), 0b1111'1011U);  // crosses the byte boundary
    CHECK_EQ(reader.tell(), 12U);
}

RIPTIDE_TEST(bit_reader, seek_and_align) {
    const Bytes data = test::random_bytes(64, 1);
    BitReader reader(data, 13);
    CHECK_EQ(reader.tell(), 13U);
    CHECK_EQ(reader.read(3), static_cast<std::uint32_t>((data[1] >> 5) & 7));
    reader.read(1);
    reader.align_to_byte();
    CHECK_EQ(reader.tell(), 24U);
}

RIPTIDE_TEST(bit_reader, reading_past_the_end_is_detected) {
    const std::array<std::uint8_t, 2> data = {0xAA, 0x55};
    BitReader reader(data);
    reader.read(16);
    CHECK(!reader.exhausted());
    CHECK_EQ(reader.read(8), 0U);
    CHECK(reader.exhausted());
}

RIPTIDE_TEST(huffman, decodes_canonical_code) {
    // RFC 1951 example: A=2 bits, B=1 bit, C=3 bits, D=3 bits -> B=0, A=10, C=110, D=111.
    const std::array<std::uint8_t, 4> lengths = {2, 1, 3, 3};
    HuffmanDecoder decoder;
    CHECK(decoder.build(lengths, HuffmanDecoder::Completeness::kRequired));
    // Codes are sent MSB first, so the stream B, A, D, C is the bit sequence
    // 0, 1 0, 1 1 1, 1 1 0, packed into bytes starting at the least significant bit.
    const std::array<std::uint8_t, 2> data = {0b1111'1010, 0b0000'0000};
    BitReader reader(data);
    CHECK_EQ(decoder.decode(reader), 1);
    CHECK_EQ(decoder.decode(reader), 0);
    CHECK_EQ(decoder.decode(reader), 3);
    CHECK_EQ(decoder.decode(reader), 2);
}

RIPTIDE_TEST(huffman, rejects_oversubscribed_and_incomplete_codes) {
    HuffmanDecoder decoder;
    const std::array<std::uint8_t, 3> oversubscribed = {1, 1, 1};
    CHECK(!decoder.build(oversubscribed, HuffmanDecoder::Completeness::kAllowSingleCode));
    const std::array<std::uint8_t, 3> incomplete = {2, 2, 2};
    CHECK(!decoder.build(incomplete, HuffmanDecoder::Completeness::kAllowSingleCode));
    const std::array<std::uint8_t, 3> single = {0, 1, 0};
    CHECK(decoder.build(single, HuffmanDecoder::Completeness::kAllowSingleCode));
    CHECK(!decoder.build(single, HuffmanDecoder::Completeness::kRequired));
    const std::array<std::uint8_t, 3> empty = {0, 0, 0};
    CHECK(decoder.build(empty, HuffmanDecoder::Completeness::kAllowEmptyOrSingle));
    CHECK(!decoder.build(empty, HuffmanDecoder::Completeness::kAllowSingleCode));
}

RIPTIDE_TEST(huffman, long_codes_use_subtables) {
    // A complete code with lengths 1..15 plus one extra 15-bit code.
    std::array<std::uint8_t, 16> lengths{};
    for (unsigned i = 0; i < 15; ++i) {
        lengths[i] = static_cast<std::uint8_t>(i + 1);
    }
    lengths[15] = 15;
    HuffmanDecoder decoder;
    CHECK(decoder.build(lengths, HuffmanDecoder::Completeness::kRequired));
    // Symbol 14 has code 111111111111110 (15 bits, MSB first).
    std::uint32_t reversed = 0;
    const std::uint32_t code = 0b111111111111110;
    for (int i = 0; i < 15; ++i) {
        reversed = (reversed << 1) | ((code >> i) & 1U);
    }
    const std::array<std::uint8_t, 4> data = {static_cast<std::uint8_t>(reversed),
                                              static_cast<std::uint8_t>(reversed >> 8), 0, 0};
    BitReader reader(data);
    CHECK_EQ(decoder.decode(reader), 14);
    CHECK_EQ(reader.tell(), 15U);
}

RIPTIDE_TEST(crc32, matches_zlib) {
    for (const std::size_t size : {0U, 1U, 7U, 8U, 9U, 1000U, 65537U}) {
        const Bytes data = test::random_bytes(size, static_cast<std::uint32_t>(size));
        const auto expected = static_cast<std::uint32_t>(::crc32(0, data.data(), static_cast<uInt>(size)));
        CHECK_EQ(crc32_update(0, data), expected);
    }
}

RIPTIDE_TEST(crc32, hardware_and_portable_paths_agree) {
    for (const std::size_t size : {0U, 3U, 8U, 13U, 4096U, 100'003U}) {
        const Bytes data = test::random_bytes(size, static_cast<std::uint32_t>(size) + 1);
        CHECK_EQ(crc32_update(0x1234U, data), crc32_update_portable(0x1234U, data));
    }
}

RIPTIDE_TEST(crc32, combine_equals_crc_of_concatenation) {
    const Bytes data = test::random_bytes(200'000, 42);
    const std::uint32_t whole = crc32_update(0, data);
    std::mt19937 engine(7);
    for (int trial = 0; trial < 50; ++trial) {
        const std::size_t split = engine() % (data.size() + 1);
        const std::uint32_t left = crc32_update(0, std::span(data).first(split));
        const std::uint32_t right = crc32_update(0, std::span(data).subspan(split));
        CHECK_EQ(crc32_combine(left, right, data.size() - split), whole);
    }
    CHECK_EQ(crc32_combine(whole, 0, 0), whole);
}

RIPTIDE_TEST(gzip_format, parses_header_fields) {
    test::GzipSettings settings;
    settings.name = "silesia.tar";
    const Bytes gzip = test::gzip_compress(test::text_like(1000, 3), settings);
    const GzipHeader header = parse_gzip_header(gzip, 0);
    CHECK_EQ(header.original_name, std::string("silesia.tar"));
    CHECK_EQ(header.size, 10U + settings.name.size() + 1);
    const GzipFooter footer = read_gzip_footer(gzip, gzip.size() - kGzipFooterSize);
    CHECK_EQ(footer.isize, 1000U);
}

RIPTIDE_TEST(gzip_format, rejects_bad_headers) {
    const Bytes not_gzip = {'P', 'K', 3, 4, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK_THROWS(parse_gzip_header(not_gzip, 0), GzipFormatError);
    Bytes truncated = test::gzip_compress(test::text_like(100, 1));
    truncated.resize(6);
    CHECK_THROWS(parse_gzip_header(truncated, 0), GzipFormatError);
}
