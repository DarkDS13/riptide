#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

namespace riptide {

/// Growable array of trivially copyable elements whose new elements are left
/// uninitialised.
///
/// Decoded chunks are tens to hundreds of megabytes. std::vector::resize writes
/// every new element, which costs a pass over memory and makes all of it resident
/// at once, and shrinking a vector never returns its capacity. Buffer resizes with
/// realloc instead: growing does not touch the new elements, large blocks are
/// usually moved by remapping pages rather than copying, and shrinking returns the
/// tail to the allocator.
template <typename T>
class Buffer {
    static_assert(std::is_trivially_copyable_v<T>, "Buffer elements are copied with realloc");

public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    Buffer() noexcept = default;
    explicit Buffer(std::size_t size) { resize(size); }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept
        : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)) {}

    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            std::free(data_);
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    ~Buffer() { std::free(data_); }

    /// Sets the size to `size` elements. Existing elements up to the new size are
    /// kept; new elements are uninitialised.
    void resize(std::size_t size) {
        if (size == size_) {
            return;
        }
        if (size == 0) {
            clear();
            return;
        }
        void* grown = std::realloc(data_, size * sizeof(T));
        if (grown == nullptr) {
            throw std::bad_alloc();
        }
        data_ = static_cast<T*>(grown);
        size_ = size;
    }

    /// Releases the memory.
    void clear() noexcept {
        std::free(data_);
        data_ = nullptr;
        size_ = 0;
    }

    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    T& operator[](std::size_t i) noexcept { return data_[i]; }
    const T& operator[](std::size_t i) const noexcept { return data_[i]; }

    [[nodiscard]] T* begin() noexcept { return data_; }
    [[nodiscard]] T* end() noexcept { return data_ + size_; }
    [[nodiscard]] const T* begin() const noexcept { return data_; }
    [[nodiscard]] const T* end() const noexcept { return data_ + size_; }

private:
    T* data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace riptide
