#pragma once

#include "hal/timer_interface.h"
#include <memory>

namespace modem {

struct TimerHandleDeleter {
    bool owns = true;
    void operator()(TimerInterface* ptr) const {
        if (owns) {
            std::default_delete<TimerInterface>{}(ptr);
        }
    }
};
using TimerHandle = std::unique_ptr<TimerInterface, TimerHandleDeleter>;

/// Creates the platform-appropriate timer implementation.
TimerHandle create_platform_timer();

/// Blocks the calling thread for at least the given number of milliseconds.
/// Uses the native platform delay primitive (k_msleep on Zephyr, sleep_for on desktop).
void delay_ms(uint32_t ms);

} // namespace modem
