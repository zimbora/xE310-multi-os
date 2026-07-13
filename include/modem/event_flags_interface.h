#pragma once

#include <cstdint>

namespace modem {

enum class EventFlagsError : uint8_t {
    ok = 0,
    timeout,
};

/// Abstract event-flags interface.
/// Implementations expose a bit-mask synchronization primitive similar to Zephyr k_event.
class EventFlagsInterface {
public:
    EventFlagsInterface() = default;
    EventFlagsInterface(const EventFlagsInterface&) = delete;
    EventFlagsInterface& operator=(const EventFlagsInterface&) = delete;
    EventFlagsInterface(EventFlagsInterface&&) = delete;
    EventFlagsInterface& operator=(EventFlagsInterface&&) = delete;
    virtual ~EventFlagsInterface() = default;

    /// Set one or more event bits.
    virtual void set(uint32_t flags) = 0;

    /// Wait until any requested event bit is set.
    /// If fClear is true, matching bits are cleared before returning.
    /// Returns the matched bits, or 0 on timeout.
    virtual uint32_t wait(uint32_t flags, bool fClear, uint32_t timeout_ms = 0) = 0;

    /// Clear one or more event bits.
    virtual void clear(uint32_t flags) = 0;

    /// Returns the current event bit mask.
    virtual uint32_t get() const = 0;
};

} // namespace modem