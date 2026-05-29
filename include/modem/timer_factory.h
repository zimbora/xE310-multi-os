#pragma once

#include "modem/timer_interface.h"
#include <memory>

namespace modem {

/// Creates the platform-appropriate timer implementation.
std::unique_ptr<TimerInterface> create_platform_timer();

/// Blocks the calling thread for at least the given number of milliseconds.
/// Uses the native platform delay primitive (k_msleep on Zephyr, sleep_for on desktop).
void delay_ms(uint32_t ms);

} // namespace modem
