#pragma once

#if defined(PLATFORM_ZEPHYR) || defined(__ZEPHYR__)
#include <zephyr/kernel.h>
#else
#include <chrono>
#include <condition_variable>
#include <mutex>
#endif

#include <cstddef>
#include <cstdint>

namespace modem {

/// Platform-independent mutex + condition-variable for the trace ring buffer.
class TraceMutex {
public:
#if defined(PLATFORM_ZEPHYR) || defined(__ZEPHYR__)
    TraceMutex() {
        k_mutex_init(&m_);
        k_condvar_init(&cv_);
    }

    void lock() { k_mutex_lock(&m_, K_FOREVER); }
    void unlock() { k_mutex_unlock(&m_); }
    void notify() { k_condvar_signal(&cv_); }

    /// Block until count > 0 or timeout_ms elapses; mutex must be held on entry and is still held on return.
    bool wait_for_data(const size_t& count, uint32_t timeout_ms) {
        if (count > 0U) return true;
        if (timeout_ms == 0U) return false;
        const int64_t deadline = k_uptime_get() + static_cast<int64_t>(timeout_ms);
        while (count == 0U) {
            int64_t remaining = deadline - k_uptime_get();
            if (remaining <= 0) return false;
            k_condvar_wait(&cv_, &m_, K_MSEC(remaining));
        }
        return true;
    }

private:
    struct k_mutex m_{};
    struct k_condvar cv_{};
#else
    void lock() { m_.lock(); }
    void unlock() { m_.unlock(); }
    void notify() { cv_.notify_one(); }

    /// Block until count > 0 or timeout_ms elapses; mutex must be held on entry and is still held on return.
    bool wait_for_data(size_t& count, uint32_t timeout_ms) {
        // adopt_lock: take ownership of already-acquired mutex without re-locking.
        std::unique_lock<std::mutex> lk(m_, std::adopt_lock);
        auto has_data = [&count]() { return count > 0U; };
        bool result =
            (timeout_ms == 0U) ? has_data() : cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), has_data);
        lk.release(); // transfer lock ownership back to caller; do not unlock on destructor
        return result;
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
#endif
};

} // namespace modem
