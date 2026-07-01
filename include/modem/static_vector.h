#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace modem {

namespace detail {
template <typename T>
constexpr T sv_min(T a, T b) noexcept { return (a < b) ? a : b; }
} // namespace detail

/// Fixed-capacity vector — no heap allocation.
/// Backed by std::array<T, Capacity> with a runtime size counter.
template <typename T, size_t Capacity>
class StaticVector {
public:
    StaticVector() noexcept = default;

    StaticVector(std::initializer_list<T> init) noexcept {
        for (auto& v : init) {
            if (size_ >= Capacity) break;
            data_[size_++] = v;
        }
    }

    StaticVector(const T* first, const T* last) noexcept {
        size_t count = detail::sv_min(static_cast<size_t>(last - first), Capacity);
        for (size_t i = 0; i < count; ++i) {
            data_[i] = first[i];
        }
        size_ = count;
    }

    // --- Capacity ---
    size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    static constexpr size_t capacity() noexcept { return Capacity; }
    bool full() const noexcept { return size_ >= Capacity; }

    // --- Element access ---
    T& operator[](size_t i) noexcept { return data_[i]; }
    const T& operator[](size_t i) const noexcept { return data_[i]; }
    T& front() noexcept { return data_[0]; }
    const T& front() const noexcept { return data_[0]; }
    T& back() noexcept { return data_[size_ - 1]; }
    const T& back() const noexcept { return data_[size_ - 1]; }
    T* data() noexcept { return data_.data(); }
    const T* data() const noexcept { return data_.data(); }

    // --- Iterators ---
    T* begin() noexcept { return data_.data(); }
    T* end() noexcept { return data_.data() + size_; }
    const T* begin() const noexcept { return data_.data(); }
    const T* end() const noexcept { return data_.data() + size_; }

    // --- Modifiers ---
    void clear() noexcept { size_ = 0; }

    bool push_back(const T& value) noexcept {
        if (size_ >= Capacity) return false;
        data_[size_++] = value;
        return true;
    }

    bool push_back(T&& value) noexcept {
        if (size_ >= Capacity) return false;
        data_[size_++] = static_cast<T&&>(value);
        return true;
    }

    void pop_back() noexcept {
        if (size_ > 0) --size_;
    }

    void resize(size_t new_size) noexcept {
        if (new_size > Capacity) new_size = Capacity;
        if (new_size > size_) {
            for (size_t i = size_; i < new_size; ++i)
                data_[i] = T{};
        }
        size_ = new_size;
    }

    void resize(size_t new_size, const T& value) noexcept {
        if (new_size > Capacity) new_size = Capacity;
        if (new_size > size_) {
            for (size_t i = size_; i < new_size; ++i)
                data_[i] = value;
        }
        size_ = new_size;
    }

    /// Assign from a range [first, last).
    void assign(const T* first, const T* last) noexcept {
        size_ = 0;
        size_t count = detail::sv_min(static_cast<size_t>(last - first), Capacity);
        for (size_t i = 0; i < count; ++i) {
            data_[i] = first[i];
        }
        size_ = count;
    }

    /// Assign from initializer list.
    void assign(std::initializer_list<T> init) noexcept {
        size_ = 0;
        for (auto& v : init) {
            if (size_ >= Capacity) break;
            data_[size_++] = v;
        }
    }

    // --- Comparison ---
    bool operator==(const StaticVector& other) const noexcept {
        if (size_ != other.size_) return false;
        for (size_t i = 0; i < size_; ++i) {
            if (!(data_[i] == other.data_[i])) return false;
        }
        return true;
    }

    bool operator!=(const StaticVector& other) const noexcept {
        return !(*this == other);
    }

private:
    std::array<T, Capacity> data_{};
    size_t size_ = 0;
};

} // namespace modem
