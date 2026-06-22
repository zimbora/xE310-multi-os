#include "modem/timer_interface.h"

#ifdef MODEM_PLATFORM_WINDOWS

#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace modem {

class Win32Timer : public TimerInterface {
public:
    Win32Timer() = default;

    ~Win32Timer() override {
        std::thread t;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!running_) return;
            running_   = false;
            cancelled_ = true;
            cv_.notify_all();
            t = std::move(thread_);
        }
        if (t.joinable()) t.join();
    }

    Win32Timer(const Win32Timer&) = delete;
    Win32Timer& operator=(const Win32Timer&) = delete;

    TimerError start(uint32_t timeout_ms, Callback cb) override {
        std::unique_lock<std::mutex> lock(mutex_);
        if (running_) {
            return TimerError::already_running;
        }
        cb_        = std::move(cb);
        running_   = true;
        cancelled_ = false;
        start_     = std::chrono::steady_clock::now();
        deadline_  = start_ + std::chrono::milliseconds(timeout_ms);
        thread_    = std::thread(&Win32Timer::run, this);
        return TimerError::ok;
    }

    TimerError stop() override {
        std::thread t;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!running_) {
                return TimerError::not_running;
            }
            running_   = false;
            cancelled_ = true;
            cv_.notify_all();
            t = std::move(thread_);
        }
        // Allow stop() from within the callback (timer thread) without deadlocking
        if (t.joinable() && t.get_id() != std::this_thread::get_id()) {
            t.join();
        } else if (t.joinable()) {
            t.detach();
        }
        return TimerError::ok;
    }

    TimerError reset(uint32_t timeout_ms) override {
        std::unique_lock<std::mutex> lock(mutex_);
        if (running_) {
            // Timer is active — shift the deadline and wake the thread to re-wait
            start_    = std::chrono::steady_clock::now();
            deadline_ = start_ + std::chrono::milliseconds(timeout_ms);
            cv_.notify_all();
            return TimerError::ok;
        }
        // Timer is not running — start fresh with the saved callback
        if (!cb_) {
            return TimerError::not_running;
        }
        running_   = true;
        cancelled_ = false;
        start_     = std::chrono::steady_clock::now();
        deadline_  = start_ + std::chrono::milliseconds(timeout_ms);
        thread_    = std::thread(&Win32Timer::run, this);
        return TimerError::ok;
    }

    bool is_running() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return running_;
    }

    uint32_t elapsed_ms() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (start_ == std::chrono::steady_clock::time_point{}) {
            return 0;
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_).count();
        return static_cast<uint32_t>(ms < 0 ? 0 : ms);
    }

private:
    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!cancelled_) {
            auto snap   = deadline_;
            auto status = cv_.wait_until(lock, snap);
            // cppcheck-suppress knownConditionTrueFalse
            if (cancelled_) {
                break;
            }
            if (status == std::cv_status::timeout && deadline_ == snap) {
                // Natural expiry: clear running_ and release thread_ before invoking
                // the callback. This prevents std::terminate() if the callback calls
                // start() again (which would otherwise destruct a still-joinable thread_).
                running_ = false;
                Callback cb = cb_;
                std::thread self = std::move(thread_); // thread_ is now empty
                lock.unlock();
                self.detach();                          // safe: run() is about to return
                if (cb) {
                    cb();
                }
                return;
            }
            // Deadline was updated by reset() — loop and re-wait
        }
    }

    mutable std::mutex              mutex_;
    std::condition_variable         cv_;
    std::thread                     thread_;
    Callback                        cb_;
    bool                            running_   = false;
    bool                            cancelled_ = false;
    std::chrono::steady_clock::time_point start_{};
    std::chrono::steady_clock::time_point deadline_;
};

} // namespace modem

#include "modem/timer_factory.h"

namespace modem {

std::unique_ptr<TimerInterface> create_platform_timer() {
    return std::make_unique<Win32Timer>();
}

void delay_ms(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace modem

#endif // MODEM_PLATFORM_WINDOWS
