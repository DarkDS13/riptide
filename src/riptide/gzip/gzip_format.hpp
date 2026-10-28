#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace riptide {

/// Fields of a gzip member header (RFC 1952) that the decompressor needs.
struct GzipHeader {
    std::size_t size = 0;        ///< Header length in bytes; the DEFLATE stream starts after it.
    std::uint8_t flags = 0;      ///< FLG byte.
    std::uint32_t mtime = 0;     ///< Modification time as stored in the header.
    std::string original_name;   ///< FNAME field, if present.
};

/// Trailer of a gzip member.
struct GzipFooter {
    std::uint32_t crc32 = 0;  ///< CRC-32 of the uncompressed member data.
    std::uint32_t isize = 0;  ///< Uncompressed size modulo 2^32.
};

inline constexpr std::size_t kGzipFooterSize = 8;

/// True if `data` starts with the gzip magic bytes at `offset`.
[[nodiscard]] bool looks_like_gzip(std::span<const std::uint8_t> data, std::size_t offset = 0) noexcept;

/// Parses the member header starting at byte `offset`. Throws GzipFormatError.
[[nodiscard]] GzipHeader parse_gzip_header(std::span<const std::uint8_t> data, std::size_t offset);

/// Reads the member footer starting at byte `offset`. Throws GzipFormatError.
[[nodiscard]] GzipFooter read_gzip_footer(std::span<const std::uint8_t> data, std::size_t offset);

}  // namespace riptide
