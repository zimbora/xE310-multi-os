#pragma once

#include "modem/at_command.h"
#include "modem/uart_interface.h"

#include <memory>
#include <string>

namespace modem {

enum class ModemStatus {
    ok = 0,
    uart_error,
    not_connected,
    at_error,
    timeout,
};

/// High-level modem controller — platform-independent.
class ModemController {
public:
    /// Takes ownership of a platform-specific UART implementation.
    explicit ModemController(std::unique_ptr<UartInterface> uart);

    ModemStatus connect(const char* port, const UartConfig& config = {});
    void disconnect();
    bool is_connected() const;

    /// Send an AT command and wait for a response.
    ModemStatus send_command(const AtCommand& cmd, AtResponse& response);

    /// Convenience: send raw AT command string.
    ModemStatus send_raw(const std::string& command, AtResponse& response,
                         uint32_t timeout_ms = 1000);

private:
    std::unique_ptr<UartInterface> uart_;
};

} // namespace modem
