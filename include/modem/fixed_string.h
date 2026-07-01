#pragma once

#include <cstddef>
#include <cstring>
#include <string_view>

namespace modem {

namespace detail {
template<typename T> constexpr T fs_min(T a, T b) noexcept {
    return (a < b) ? a : b;
}
} // namespace detail

/// Fixed-capacity string — no heap allocation.
/// Provides a subset of std::string-like API for use on embedded targets.
template<size_t Capacity> class FixedString {
public:
    FixedString() noexcept { buf_[0] = '\0'; }

    explicit FixedString(const char* s) noexcept {
        if (s != nullptr) {
            size_ = detail::fs_min(std::strlen(s), Capacity);
            std::memcpy(buf_, s, size_);
        }
        buf_[size_] = '\0';
    }

    FixedString(const char* s, size_t len) noexcept : size_(detail::fs_min(len, Capacity)) {
        std::memcpy(buf_, s, size_);
        buf_[size_] = '\0';
    }

    explicit FixedString(std::string_view sv) noexcept : size_(detail::fs_min(sv.size(), Capacity)) {
        std::memcpy(buf_, sv.data(), size_);
        buf_[size_] = '\0';
    }

    // --- Capacity ---
    size_t size() const noexcept { return size_; }
    size_t length() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    static constexpr size_t capacity() noexcept { return Capacity; }

    // --- Element access ---
    const char* c_str() const noexcept { return buf_; }
    const char* data() const noexcept { return buf_; }
    char* data() noexcept { return buf_; }
    char& operator[](size_t i) noexcept { return buf_[i]; }
    const char& operator[](size_t i) const noexcept { return buf_[i]; }

    // --- Conversion ---
    operator std::string_view() const noexcept { return {buf_, size_}; }
    std::string_view view() const noexcept { return {buf_, size_}; }

    // --- Assignment ---
    FixedString& operator=(const char* s) noexcept {
        if (s != nullptr) {
            size_ = detail::fs_min(std::strlen(s), Capacity);
            std::memcpy(buf_, s, size_);
        } else {
            size_ = 0;
        }
        buf_[size_] = '\0';
        return *this;
    }

    FixedString& operator=(std::string_view sv) noexcept {
        size_ = detail::fs_min(sv.size(), Capacity);
        std::memcpy(buf_, sv.data(), size_);
        buf_[size_] = '\0';
        return *this;
    }

    // --- Modifiers ---
    void clear() noexcept {
        size_ = 0;
        buf_[0] = '\0';
    }

    FixedString& append(const char* s, size_t len) noexcept {
        size_t to_copy = detail::fs_min(len, Capacity - size_);
        std::memcpy(buf_ + size_, s, to_copy);
        size_ += to_copy;
        buf_[size_] = '\0';
        return *this;
    }

    FixedString& append(std::string_view sv) noexcept { return append(sv.data(), sv.size()); }

    FixedString& operator+=(std::string_view sv) noexcept { return append(sv); }

    FixedString& operator+=(char c) noexcept {
        if (size_ < Capacity) {
            buf_[size_++] = c;
            buf_[size_] = '\0';
        }
        return *this;
    }

    void erase(size_t pos, size_t count = NPOS) noexcept {
        if (pos >= size_) return;
        if (count == NPOS || pos + count >= size_) {
            size_ = pos;
        } else {
            std::memmove(buf_ + pos, buf_ + pos + count, size_ - pos - count);
            size_ -= count;
        }
        buf_[size_] = '\0';
    }

    // --- Search ---
    static constexpr size_t NPOS = std::string_view::npos;

    size_t find(char c, size_t pos = 0) const noexcept { return view().find(c, pos); }

    size_t find(std::string_view sv, size_t pos = 0) const noexcept { return view().find(sv, pos); }

    size_t rfind(char c, size_t pos = NPOS) const noexcept { return view().rfind(c, pos); }

    size_t rfind(std::string_view sv, size_t pos = NPOS) const noexcept { return view().rfind(sv, pos); }

    size_t find_first_not_of(std::string_view chars, size_t pos = 0) const noexcept {
        return view().find_first_not_of(chars, pos);
    }

    size_t find_last_not_of(std::string_view chars, size_t pos = NPOS) const noexcept {
        return view().find_last_not_of(chars, pos);
    }

    // --- Substring (returns a new FixedString) ---
    FixedString substr(size_t pos, size_t count = NPOS) const noexcept {
        if (pos >= size_) return {};
        size_t len = (count == NPOS) ? (size_ - pos) : detail::fs_min(count, size_ - pos);
        return FixedString(buf_ + pos, len);
    }

    // --- Comparison ---
    bool operator==(std::string_view other) const noexcept { return view() == other; }
    bool operator!=(std::string_view other) const noexcept { return view() != other; }
    bool operator==(const char* other) const noexcept { return view() == std::string_view((other != nullptr) ? other : ""); }
    bool operator!=(const char* other) const noexcept { return !(*this == other); }

    template<size_t M> bool operator==(const FixedString<M>& other) const noexcept { return view() == other.view(); }
    template<size_t M> bool operator!=(const FixedString<M>& other) const noexcept { return view() != other.view(); }

    // Allow iteration
    const char* begin() const noexcept { return buf_; }
    const char* end() const noexcept { return buf_ + size_; }

    // Set size explicitly (for external writes into data())
    void set_size(size_t n) noexcept {
        size_ = detail::fs_min(n, Capacity);
        buf_[size_] = '\0';
    }

private:
    char buf_[Capacity + 1] = {};
    size_t size_ = 0;
};

// --- Free-function concatenation (returns a FixedString large enough) ---
template<size_t N> FixedString<N> operator+(const FixedString<N>& lhs, std::string_view rhs) noexcept {
    FixedString<N> result = lhs;
    result.append(rhs);
    return result;
}

template<size_t N> FixedString<N> operator+(std::string_view lhs, const FixedString<N>& rhs) noexcept {
    FixedString<N> result(lhs);
    result.append(rhs.view());
    return result;
}

} // namespace modem
