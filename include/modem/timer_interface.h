#pragma once

#include <cstdint>
#include <functional>

namespace modem {

enum class TimerError {
    ok = 0,
    already_running,
    not_running,
};

/// Abstract one-shot timer interface — implemented per platform.
/// The callback is invoked once after the timeout expires.
/// All methods are safe to call from the same thread that called start().
/// Calling stop() from within the callback is allowed.
class TimerInterface {
public:
    using Callback = std::function<void()>;

    virtual ~TimerInterface() = default;

    /// Start the timer. Fires cb once after timeout_ms milliseconds.
    /// Returns already_running if the timer is already active.
    virtual TimerError start(uint32_t timeout_ms, Callback cb) = 0;

    /// Stop the timer. The callback will not be invoked after this returns.
    /// Returns not_running if the timer was not active.
    virtual TimerError stop() = 0;

    /// Restart the timer with a new timeout, preserving the current callback.
    /// May be called while the timer is running (resets the countdown) or when stopped.
    virtual TimerError reset(uint32_t timeout_ms) = 0;

    /// Returns true if the timer is currently counting down.
    virtual bool is_running() const = 0;

    /// Returns milliseconds elapsed since the timer was last started or reset.
    /// Returns 0 if the timer has never been started.
    virtual uint32_t elapsed_ms() const = 0;
};

} // namespace modem
