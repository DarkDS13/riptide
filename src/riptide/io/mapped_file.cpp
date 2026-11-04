#include <riptide/io/mapped_file.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <limits>
#include <system_error>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

#include <stdexcept>
#include <utility>

namespace riptide {
namespace {

#if defined(_WIN32)
[[noreturn]] void throw_system_error(const std::string& what, const std::string& path, DWORD error) {
    throw std::runtime_error(what + " '" + path +
                             "': " + std::system_category().message(static_cast<int>(error)));
}
#else
[[noreturn]] void throw_system_error(const std::string& what, const std::string& path, int error) {
    throw std::runtime_error(what + " '" + path + "': " + std::strerror(error));
}
#endif

}  // namespace

#if defined(_WIN32)

MappedFile::MappedFile(const std::string& path) {
    const HANDLE file = ::CreateFileA(path.c_str(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw_system_error("cannot open", path, ::GetLastError());
    }
    LARGE_INTEGER file_size{};
    if (::GetFileSizeEx(file, &file_size) == 0) {
        const DWORD error = ::GetLastError();
        ::CloseHandle(file);
        throw_system_error("cannot stat", path, error);
    }
    if (static_cast<unsigned long long>(file_size.QuadPart) > std::numeric_limits<std::size_t>::max()) {
        ::CloseHandle(file);
        throw std::runtime_error("cannot map '" + path + "': file too large for the address space");
    }
    size_ = static_cast<std::size_t>(file_size.QuadPart);
    if (size_ > 0) {
        // Windows cannot map an empty file, hence the size check. The view keeps the
        // mapping and the file open, so both handles can be closed straight away.
        const HANDLE mapping = ::CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping == nullptr) {
            const DWORD error = ::GetLastError();
            ::CloseHandle(file);
            throw_system_error("cannot map", path, error);
        }
        address_ = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        const DWORD error = ::GetLastError();
        ::CloseHandle(mapping);
        if (address_ == nullptr) {
            ::CloseHandle(file);
            throw_system_error("cannot map", path, error);
        }
        // The file is read front to back by many threads at once.
        WIN32_MEMORY_RANGE_ENTRY range{address_, size_};
        ::PrefetchVirtualMemory(::GetCurrentProcess(), 1, &range, 0);
    }
    ::CloseHandle(file);
}

#else

MappedFile::MappedFile(const std::string& path) {
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0) {
        throw_system_error("cannot open", path, errno);
    }
    struct stat status {};
    if (::fstat(descriptor, &status) != 0) {
        const int error = errno;
        ::close(descriptor);
        throw_system_error("cannot stat", path, error);
    }
    size_ = static_cast<std::size_t>(status.st_size);
    if (size_ > 0) {
        address_ = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, descriptor, 0);
        if (address_ == MAP_FAILED) {
            const int error = errno;
            address_ = nullptr;
            ::close(descriptor);
            throw_system_error("cannot map", path, error);
        }
        // The file is read front to back by many threads at once.
        ::madvise(address_, size_, MADV_WILLNEED);
    }
    ::close(descriptor);
}

#endif

MappedFile::~MappedFile() { release(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
    : address_(std::exchange(other.address_, nullptr)), size_(std::exchange(other.size_, 0)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        release();
        address_ = std::exchange(other.address_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

void MappedFile::release() noexcept {
    if (address_ != nullptr) {
#if defined(_WIN32)
        ::UnmapViewOfFile(address_);
#else
        ::munmap(address_, size_);
#endif
        address_ = nullptr;
        size_ = 0;
    }
}

}  // namespace riptide
