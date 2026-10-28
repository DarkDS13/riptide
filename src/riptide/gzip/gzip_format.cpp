#include <riptide/gzip/gzip_format.hpp>

#include <riptide/common/error.hpp>

namespace riptide {
namespace {

constexpr std::uint8_t kMagic1 = 0x1F;
constexpr std::uint8_t kMagic2 = 0x8B;
constexpr std::uint8_t kMethodDeflate = 8;

constexpr std::uint8_t kFlagHeaderCrc = 0x02;
constexpr std::uint8_t kFlagExtra = 0x04;
constexpr std::uint8_t kFlagName = 0x08;
constexpr std::uint8_t kFlagComment = 0x10;
constexpr std::uint8_t kReservedFlags = 0xE0;

constexpr std::size_t kFixedHeaderSize = 10;

std::uint32_t read_le32(std::span<const std::uint8_t> data, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(data[offset]) |
           static_cast<std::uint32_t>(data[offset + 1]) << 8 |
           static_cast<std::uint32_t>(data[offset + 2]) << 16 |
           static_cast<std::uint32_t>(data[offset + 3]) << 24;
}

/// Returns the offset just past the zero terminator of a string field.
std::size_t skip_zero_terminated(std::span<const std::uint8_t> data, std::size_t offset,
                                 std::string* value) {
    while (offset < data.size() && data[offset] != 0) {
        if (value != nullptr) {
            value->push_back(static_cast<char>(data[offset]));
        }
        ++offset;
    }
    if (offset >= data.size()) {
        throw GzipFormatError("truncated gzip header string field");
    }
    return offset + 1;
}

}  // namespace

bool looks_like_gzip(std::span<const std::uint8_t> data, std::size_t offset) noexcept {
    return offset + 3 <= data.size() && data[offset] == kMagic1 && data[offset + 1] == kMagic2 &&
           data[offset + 2] == kMethodDeflate;
}

GzipHeader parse_gzip_header(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + kFixedHeaderSize > data.size()) {
        throw GzipFormatError("truncated gzip header");
    }
    if (!looks_like_gzip(data, offset)) {
        throw GzipFormatError("not a gzip member (bad magic bytes or compression method)");
    }

    GzipHeader header;
    header.flags = data[offset + 3];
    if ((header.flags & kReservedFlags) != 0) {
        throw GzipFormatError("reserved gzip header flags are set");
    }
    header.mtime = read_le32(data, offset + 4);

    std::size_t position = offset + kFixedHeaderSize;
    if ((header.flags & kFlagExtra) != 0) {
        if (position + 2 > data.size()) {
            throw GzipFormatError("truncated gzip extra field");
        }
        const std::size_t extra_length = data[position] | (std::size_t{data[position + 1]} << 8);
        position += 2 + extra_length;
    }
    if ((header.flags & kFlagName) != 0) {
        position = skip_zero_terminated(data, position, &header.original_name);
    }
    if ((header.flags & kFlagComment) != 0) {
        position = skip_zero_terminated(data, position, nullptr);
    }
    if ((header.flags & kFlagHeaderCrc) != 0) {
        position += 2;
    }
    if (position > data.size()) {
        throw GzipFormatError("truncated gzip header");
    }

    header.size = position - offset;
    return header;
}

GzipFooter read_gzip_footer(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + kGzipFooterSize > data.size()) {
        throw GzipFormatError("truncated gzip footer");
    }
    return GzipFooter{read_le32(data, offset), read_le32(data, offset + 4)};
}

}  // namespace riptide
