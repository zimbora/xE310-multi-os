#pragma once

#include "modem/at_command.h"
#include "modem/uart_interface.h"

#include <memory>
#include <string>
#include <vector>

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
    std::unique_ptr<UartInterface> uart_;
};

} // namespace modem
