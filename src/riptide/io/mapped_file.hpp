#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace riptide {

/// Read-only memory mapping of a file (RAII). The decoder works directly on the
/// mapped bytes, so worker threads can read any part of the input without I/O calls.
class MappedFile {
public:
    explicit MappedFile(const std::string& path);
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept {
        return {static_cast<const std::uint8_t*>(address_), size_};
    }

private:
    void release() noexcept;

    void* address_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace riptide
