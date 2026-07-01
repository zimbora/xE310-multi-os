#pragma once

#include "modem/fixed_string.h"

#include <cstdint>
#include <string_view>

#define   AT_TERMINATOR     		"\r\n"

namespace modem {

enum class AtStatus {
    ok = 0,
    error,
    timeout,
    busy,
};

/// Maximum capacity for an AT command string.
static constexpr size_t AT_CMD_MAX = 256;
/// Maximum capacity for an AT response body.
static constexpr size_t AT_RESPONSE_MAX = 2048;

struct AtResponse {
    AtStatus status = AtStatus::error;
    FixedString<AT_RESPONSE_MAX> body;
};

/// Builds and parses AT command strings.
class AtCommand {
public:
    /// Create an AT command from a raw command string (e.g. "AT+CSQ").
    explicit AtCommand(std::string_view command, uint32_t timeout_ms = 1000);

    std::string_view command_string() const;
    uint32_t timeout_ms() const;

    /// Parse a raw response buffer into an AtResponse.
    static AtResponse parse_response(std::string_view raw);

private:
    FixedString<AT_CMD_MAX> command_;
    uint32_t timeout_ms_;
};

} // namespace modem
