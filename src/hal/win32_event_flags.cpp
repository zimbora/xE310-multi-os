#include "modem/event_flags_factory.h"

#ifdef MODEM_PLATFORM_WINDOWS

#include <condition_variable>
#include <mutex>

namespace modem {

class Win32EventFlags : public EventFlagsInterface {
public:
    void set(uint32_t flags) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            flags_ |= flags;
        }
        cv_.notify_all();
    }

    uint32_t wait(uint32_t flags, bool fClear, uint32_t timeout_ms) override {
        std::unique_lock<std::mutex> lock(mutex_);
        auto matches = [&]() { return (flags_ & flags) != 0U; };
        if (!matches()) {
            if (timeout_ms == 0U) return 0U;
            bool fReady = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), matches);
            if (!fReady) return 0U;
        }

        uint32_t matched = flags_ & flags;
        if (fClear) {
            flags_ &= ~matched;
        }
        return matched;
    }

    void clear(uint32_t flags) override {
        std::lock_guard<std::mutex> lock(mutex_);
        flags_ &= ~flags;
    }

    uint32_t get() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return flags_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    uint32_t flags_ = 0;
};

EventFlagsHandle create_platform_event_flags() {
    // dynamic-memory-allow: factory boundary returns owning HAL handle
    return EventFlagsHandle(new Win32EventFlags(), EventFlagsHandleDeleter{});
}

} // namespace modem

#endif