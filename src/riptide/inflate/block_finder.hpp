#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <riptide/huffman/huffman_decoder.hpp>
#include <riptide/inflate/chunk_decoder.hpp>
#include <riptide/inflate/dynamic_header.hpp>

namespace riptide {

/// Searches a DEFLATE stream for offsets where a block may start.
///
/// Block boundaries are not indexed and can lie at any bit offset, so a worker that
/// starts in the middle of a file must search for them. The finder looks for
/// non-final dynamic-Huffman blocks (validating the complete code-length header) and
/// non-final stored blocks (validating LEN against NLEN). Fixed-Huffman blocks are
/// not searched for: their header carries no data that could be validated.
///
/// The search is speculative: a candidate is a position that *could* be a block
/// start. A false positive is detected later, because decoding from it either fails
/// or produces a chunk that does not line up with the end of its predecessor.
class BlockFinder {
public:
    explicit BlockFinder(std::span<const std::uint8_t> input) noexcept : input_(input) {}

    /// First candidate whose boundary bit lies in [begin_bit, end_bit).
    [[nodiscard]] std::optional<BlockBoundary> find(std::size_t begin_bit, std::size_t end_bit) const;

    /// First dynamic-block header in [begin_bit, end_bit).
    [[nodiscard]] std::optional<std::size_t> find_dynamic(std::size_t begin_bit,
                                                          std::size_t end_bit) const;

    /// First stored-block LEN field (a byte-aligned bit offset) in [begin_bit, end_bit).
    [[nodiscard]] std::optional<std::size_t> find_stored(std::size_t begin_bit,
                                                         std::size_t end_bit) const;

private:
    /// Decoders and lengths reused across all candidates of one search.
    struct Scratch {
        DynamicCodeLengths lengths;
        HuffmanDecoder code_length;
        HuffmanDecoder literal_length;
        HuffmanDecoder distance;
    };

    [[nodiscard]] bool is_dynamic_header(std::size_t bit, Scratch& scratch) const;

    std::span<const std::uint8_t> input_;
};

}  // namespace riptide
