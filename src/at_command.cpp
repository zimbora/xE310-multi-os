#include "modem/at_command.h"

#include <algorithm>

namespace modem {

AtCommand::AtCommand(const std::string& command, uint32_t timeout_ms)
    : command_(command), timeout_ms_(timeout_ms) {}

const std::string& AtCommand::command_string() const {
    return command_;
}

uint32_t AtCommand::timeout_ms() const {
    return timeout_ms_;
}

AtResponse AtCommand::parse_response(const std::string& raw) {
    AtResponse response;

    if (raw.empty()) {
        response.status = AtStatus::timeout;
        return response;
    }

    // Check for standard AT result codes
    if (raw.find("OK") != std::string::npos) {
        response.status = AtStatus::ok;
    } else if (raw.find("ERROR") != std::string::npos) {
        response.status = AtStatus::error;
    } else if (raw.find("BUSY") != std::string::npos) {
        response.status = AtStatus::busy;
    } else {
        response.status = AtStatus::error;
    }

    response.body = raw;
    return response;
}

} // namespace modem
