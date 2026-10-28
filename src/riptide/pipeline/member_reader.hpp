#pragma once

// Helpers shared by the sequential and parallel decompressors for walking the
// members of a (possibly multi-member) gzip file.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <riptide/common/error.hpp>
#include <riptide/gzip/gzip_format.hpp>

namespace riptide {

/// True if another gzip member starts at `offset`. Trailing zero bytes, which some
/// tools append for padding, end the file; any other trailing data is an error.
inline bool has_another_member(std::span<const std::uint8_t> gzip, std::size_t offset) {
    if (offset >= gzip.size()) {
        if (offset == 0) {
            throw GzipFormatError("empty input");
        }
        return false;
    }
    if (looks_like_gzip(gzip, offset)) {
        return true;
    }
    const auto rest = gzip.subspan(offset);
    if (std::all_of(rest.begin(), rest.end(), [](std::uint8_t byte) { return byte == 0; })) {
        return false;
    }
    throw GzipFormatError("trailing data after gzip member at byte " + std::to_string(offset));
}

/// Checks decoded member data against its gzip footer.
inline void check_member_footer(const GzipFooter& footer, std::uint32_t crc, std::size_t size) {
    if (footer.isize != static_cast<std::uint32_t>(size)) {
        throw ChecksumError("gzip size mismatch: footer says " + std::to_string(footer.isize) +
                            ", decoded " + std::to_string(size) + " bytes");
    }
    if (footer.crc32 != crc) {
        throw ChecksumError("gzip CRC-32 mismatch");
    }
}

}  // namespace riptide
