#include <riptide/io/mapped_file.hpp>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace riptide {
namespace {

[[noreturn]] void throw_system_error(const std::string& what, const std::string& path) {
    throw std::runtime_error(what + " '" + path + "': " + std::strerror(errno));
}

}  // namespace

MappedFile::MappedFile(const std::string& path) {
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0) {
        throw_system_error("cannot open", path);
    }
    struct stat status {};
    if (::fstat(descriptor, &status) != 0) {
        ::close(descriptor);
        throw_system_error("cannot stat", path);
    }
    size_ = static_cast<std::size_t>(status.st_size);
    if (size_ > 0) {
        address_ = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, descriptor, 0);
        if (address_ == MAP_FAILED) {
            address_ = nullptr;
            ::close(descriptor);
            throw_system_error("cannot map", path);
        }
        // The file is read front to back by many threads at once.
        ::madvise(address_, size_, MADV_WILLNEED);
    }
    ::close(descriptor);
}

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
        ::munmap(address_, size_);
        address_ = nullptr;
        size_ = 0;
    }
}

}  // namespace riptide
