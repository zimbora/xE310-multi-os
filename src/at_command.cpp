#include "modem/at_command.h"
#include "modem/log.h"

#include <algorithm>
#include <string_view>

namespace modem {

AtCommand::AtCommand(std::string_view command, uint32_t timeout_ms)
    : command_(command), timeout_ms_(timeout_ms) {}

std::string_view AtCommand::command_string() const {
    return command_.view();
}

uint32_t AtCommand::timeout_ms() const {
    return timeout_ms_;
}

AtResponse AtCommand::parse_response(std::string_view raw) {
    AtResponse response;

    if (raw.empty()) {
        response.status = AtStatus::timeout;
        return response;
    }

    MODEM_LOG_DBG("<<: %.*s", static_cast<int>(raw.size()), raw.data());

    // Split raw response into lines on AT_TERMINATOR ("\r\n")
    constexpr std::string_view TERMINATOR = AT_TERMINATOR;
    std::string_view::size_type start = 0;
    while (start < raw.size()) {
        auto end = raw.find(AT_TERMINATOR, start);
        std::string_view line = (end == std::string_view::npos)
                           ? raw.substr(start)
                           : raw.substr(start, end - start);
        start = (end == std::string_view::npos) ? raw.size() : end + TERMINATOR.size();

        if (line.empty()) {
            continue;
        }

        if (line == "OK") {
            response.status = AtStatus::ok;
        } else if (line == "ERROR" || line.substr(0, 11) == "+CME ERROR:" || line.substr(0, 11) == "+CMS ERROR:") {
            response.status = AtStatus::error;
        } else if (line == "BUSY") {
            response.status = AtStatus::busy;
        } else {
            if (!response.body.empty()) {
                response.body += AT_TERMINATOR;
            }
            response.body.append(line);
        }
    }

    return response;
}

} // namespace modem
