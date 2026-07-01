#include "modem/at_command.h"
#include "modem/log.h"

#include <algorithm>
#include <string_view>

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

    MODEM_LOG_DBG("<<: %s", raw.c_str());

    // Split raw response into lines on AT_TERMINATOR ("\r\n")
    constexpr std::string_view TERMINATOR = AT_TERMINATOR;
    std::string::size_type start = 0;
    while (start < raw.size()) {
        auto end = raw.find(AT_TERMINATOR, start);
        std::string line = (end == std::string::npos)
                           ? raw.substr(start)
                           : raw.substr(start, end - start);
        start = (end == std::string::npos) ? raw.size() : end + TERMINATOR.size();

        if (line.empty()) {
            continue;
        }

        if (line == "OK") {
            response.status = AtStatus::ok;
        } else if (line == "ERROR" || line.rfind("+CME ERROR:", 0) == 0 || line.rfind("+CMS ERROR:", 0) == 0) {
            response.status = AtStatus::error;
        } else if (line == "BUSY") {
            response.status = AtStatus::busy;
        } else {
            if (!response.body.empty()) {
                response.body += AT_TERMINATOR;
            }
            response.body += line;
        }
    }

    return response;
}

} // namespace modem
