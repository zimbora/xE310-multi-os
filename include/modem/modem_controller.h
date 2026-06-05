#pragma once

#include "modem/at_command.h"
#include "modem/timer_interface.h"
#include "modem/uart_interface.h"

#include <memory>
#include <string>
#include <vector>

#ifdef PLATFORM_ZEPHYR
#include <zephyr/kernel.h>
#else
#include <mutex>
#endif

namespace modem {

enum class ModemStatus {
    ok = 0,
    uart_error,
    not_connected,
    at_error,
    timeout,
    invalid_param,
};

/// High-level modem controller — platform-independent.
class ModemController {
public:
    /// Takes ownership of a platform-specific UART implementation.
    /// Optionally accepts a timer; if nullptr, a platform timer is created internally.
    explicit ModemController(std::unique_ptr<UartInterface> uart,
                             std::unique_ptr<TimerInterface> timer = nullptr);

    ModemStatus connect(const char* port, const UartConfig& config = {});
    void disconnect();
    bool is_connected() const;

    /// Send an AT command and wait for a response.
    ModemStatus send_command(const AtCommand& cmd, AtResponse& response);

    /// Convenience: send raw AT command string.
    ModemStatus send_raw(const std::string& command, AtResponse& response,
                         uint32_t timeout_ms = 5000);

    /// Send binary data.
    ModemStatus send_binary(const std::vector<uint8_t>& data, AtResponse& response,
                            uint32_t timeout_ms = 5000);

    /// Send a command that expects a '>' prompt, then send binary data, then read final response.
    ModemStatus send_with_prompt(const std::string& command, const std::vector<uint8_t>& data,
                                 AtResponse& response, uint32_t timeout_ms = 5000);

    /// Non-blocking read of any unsolicited data from the modem.
    /// Returns lines that begin with a known URC prefix (e.g. "+CREG:", "+CGEV:").
    /// Reads for at most timeout_ms; pass 0 for a best-effort non-blocking poll.
    std::vector<std::string> poll_urc(uint32_t timeout_ms = 50);

private:
    class IoMutex {
    public:
#ifdef PLATFORM_ZEPHYR
        IoMutex() { k_mutex_init(&m_); }
        void lock()   { k_mutex_lock(&m_, K_FOREVER); }
        void unlock() { k_mutex_unlock(&m_); }
    private:
        struct k_mutex m_;
#else
        void lock()   { m_.lock(); }
        void unlock() { m_.unlock(); }
    private:
        std::mutex m_;
#endif
    };

    class IoLockGuard {
    public:
        explicit IoLockGuard(IoMutex& m) : m_(m) { m_.lock(); }
        ~IoLockGuard() { m_.unlock(); }
        IoLockGuard(const IoLockGuard&) = delete;
        IoLockGuard& operator=(const IoLockGuard&) = delete;
    private:
        IoMutex& m_;
    };

    // Serialize UART read/write access across command and URC paths.
    mutable IoMutex io_mutex_;
    std::unique_ptr<UartInterface> uart_;
    std::unique_ptr<TimerInterface> cmd_timer_;
};

} // namespace modem
