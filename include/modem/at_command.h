#pragma once

#include <cstdint>
#include <string>

#define   AT_TERMINATOR     		"\r\n"

namespace modem {

enum class AtStatus {
    ok = 0,
    error,
    timeout,
    busy,
};

struct AtResponse {
    AtStatus status = AtStatus::error;
    std::string body;
};

/// Builds and parses AT command strings.
class AtCommand {
public:
    /// Create an AT command from a raw command string (e.g. "AT+CSQ").
    explicit AtCommand(const std::string& command, uint32_t timeout_ms = 1000);

    const std::string& command_string() const;
    uint32_t timeout_ms() const;

    /// Parse a raw response buffer into an AtResponse.
    static AtResponse parse_response(const std::string& raw);

private:
    std::string command_;
    uint32_t timeout_ms_;
};

} // namespace modem
