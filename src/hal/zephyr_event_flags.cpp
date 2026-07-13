#include "modem/event_flags_factory.h"

#if defined(PLATFORM_ZEPHYR) || defined(__ZEPHYR__)

#include <zephyr/kernel.h>

namespace modem {

class ZephyrEventFlags : public EventFlagsInterface {
public:
    ZephyrEventFlags() { k_event_init(&event_); }

    void set(uint32_t flags) override { k_event_post(&event_, flags); }

    uint32_t wait(uint32_t flags, bool fClear, uint32_t timeout_ms) override {
        return k_event_wait(&event_, flags, fClear, timeout_ms == 0U ? K_NO_WAIT : K_MSEC(timeout_ms));
    }

    void clear(uint32_t flags) override { k_event_clear(&event_, flags); }

    uint32_t get() const override { return k_event_test(const_cast<struct k_event*>(&event_), ~0U); }

private:
    struct k_event event_{};
};

EventFlagsHandle create_platform_event_flags() {
    auto* eventFlags = new ZephyrEventFlags(); // dynamic-memory-allow: factory boundary returns owning HAL handle
    return EventFlagsHandle(eventFlags, EventFlagsHandleDeleter{});
}

} // namespace modem

#endif