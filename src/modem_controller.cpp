#include "modem/modem_controller.h"
#include "modem/log.h"
#include "modem/timer_factory.h"

#include <cstring>

namespace modem {

ModemController::ModemController(std::unique_ptr<UartInterface> uart, std::unique_ptr<TimerInterface> timer)
    : uart_(std::move(uart)), cmd_timer_(timer ? std::move(timer) : create_platform_timer()) {}

ModemStatus ModemController::connect(const char* port, const UartConfig& config) {
    IoLockGuard lock(io_mutex_);
    if (!lock) {
        return ModemStatus::busy;
    }
    if (!uart_) {
        return ModemStatus::uart_error;
    }

    auto err = uart_->open(port, config);
    if (err != UartError::ok) {
        return ModemStatus::uart_error;
    }

    return ModemStatus::ok;
}

void ModemController::disconnect() {
    IoLockGuard lock(io_mutex_);
    if (!lock) {
        return;
    }
    if (uart_ && uart_->is_open()) {
        uart_->close();
    }
}

bool ModemController::is_connected() const {
    IoLockGuard lock(io_mutex_);
    if (!lock) {
        return false;
    }
    return uart_ && uart_->is_open();
}

ModemStatus ModemController::send_command(const AtCommand& cmd, AtResponse& response) {
    IoLockGuard lock(io_mutex_);
    if (!lock) {
        return ModemStatus::busy;
    }
    if (!(uart_ && uart_->is_open())) {
        return ModemStatus::not_connected;
    }

    const auto cmd_str = cmd.command_string();
    // Build full command with CRLF in a stack buffer
    char full_cmd[AT_CMD_MAX + 3];
    size_t cmd_len = std::min(cmd_str.size(), sizeof(full_cmd) - 3);
    std::memcpy(full_cmd, cmd_str.data(), cmd_len);
    full_cmd[cmd_len] = '\r';
    full_cmd[cmd_len + 1] = '\n';
    full_cmd[cmd_len + 2] = '\0';
    size_t full_len = cmd_len + 2;

    auto err = uart_->write(reinterpret_cast<const uint8_t*>(full_cmd), full_len);
    if (err != UartError::ok) {
        return ModemStatus::uart_error;
    }
    MODEM_LOG_DBG(">>: %.*s", (int)cmd_str.size(), cmd_str.data());

    // Read response — enforce an overall deadline across all reads.
    const uint32_t total_ms = cmd.timeout_ms();
    cmd_timer_->stop();
    cmd_timer_->start(total_ms, nullptr);

    uint8_t buffer[512];
    size_t bytes_read = 0;
    FixedString<AT_RESPONSE_MAX> accumulated;

    while (true) {
        const uint32_t elapsed = cmd_timer_->elapsed_ms();
        if (elapsed >= total_ms) {
            return ModemStatus::timeout;
        }
        const uint32_t remaining_ms = total_ms - elapsed;

        memset(buffer, 0, sizeof(buffer));
        auto read_err = uart_->read(buffer, sizeof(buffer) - 1, bytes_read, remaining_ms);
        if (read_err == UartError::timeout) {
            return ModemStatus::timeout;
        }
        if (read_err != UartError::ok) {
            return ModemStatus::uart_error;
        }

        if (bytes_read > 0) {
            accumulated.append(reinterpret_cast<const char*>(buffer), bytes_read);
            response = AtCommand::parse_response(accumulated.view());

            if (response.status == AtStatus::ok || response.status == AtStatus::error ||
                response.status == AtStatus::busy) {
                // Extract any URCs that arrived after the status line and buffer them for poll_urc().
                std::string_view acc_view = accumulated.view();
                std::string_view search_term = (response.status == AtStatus::ok)      ? "OK"
                                               : (response.status == AtStatus::error) ? "ERROR"
                                                                                      : "BUSY";
                size_t status_end = acc_view.find(search_term);
                if (status_end != std::string_view::npos) {
                    // Find the end of the status line (OK\r\n or ERROR\r\n)
                    status_end = acc_view.find("\r\n", status_end);
                    if (status_end != std::string_view::npos) {
                        status_end += 2; // skip the \r\n
                        // Anything after the status line goes to the URC buffer
                        if (status_end < acc_view.size()) {
                            urc_rx_buffer_.append(acc_view.substr(status_end));
                        }
                    }
                }

                if (response.status == AtStatus::ok) {
                    return ModemStatus::ok;
                }
                if (response.status == AtStatus::error || response.status == AtStatus::busy) {
                    return ModemStatus::at_error;
                }
            }
        }
    }
}

ModemStatus ModemController::send_raw(std::string_view command, AtResponse& response, uint32_t timeout_ms, bool retry) {
    AtCommand cmd(command, timeout_ms);
    ModemStatus status = send_command(cmd, response);

    if (retry && status == ModemStatus::timeout) {
        for (uint8_t attempt = 1; attempt < MAX_AT_RETRIES && status == ModemStatus::timeout; ++attempt) {
            MODEM_LOG_DBG("Retrying AT command (%u/%u): %.*s", attempt, MAX_AT_RETRIES - 1, (int)command.size(),
                          command.data());
            AtCommand retry_cmd(command, timeout_ms);
            status = send_command(retry_cmd, response);
        }
    }

    return status;
}

ModemStatus ModemController::send_binary(const uint8_t* data, size_t length, AtResponse& response,
                                         uint32_t timeout_ms) {
    IoLockGuard lock(io_mutex_);
    if (!lock) {
        return ModemStatus::busy;
    }
    if (!(uart_ && uart_->is_open())) {
        return ModemStatus::not_connected;
    }

    auto err = uart_->write(data, length);
    if (err != UartError::ok) {
        return ModemStatus::uart_error;
    }

    // Log hex representation using stack buffer
    char hex_buf[512];
    size_t hex_pos = 0;
    for (size_t i = 0; i < length && hex_pos + 3 < sizeof(hex_buf); ++i) {
        int n = snprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos, "%02x ", data[i]);
        if (n > 0) hex_pos += static_cast<size_t>(n);
    }
    hex_buf[hex_pos] = '\0';
    MODEM_LOG_DBG(">>: [binary %zu bytes]: %s", length, hex_buf);

    // Read response (expect OK or ERROR after binary payload)
    uint8_t buffer[512];
    size_t bytes_read = 0;
    err = uart_->read(buffer, sizeof(buffer) - 1, bytes_read, timeout_ms);
    if (err == UartError::timeout) {
        return ModemStatus::timeout;
    }
    if (err != UartError::ok) {
        return ModemStatus::uart_error;
    }

    buffer[bytes_read] = '\0';
    std::string_view raw(reinterpret_cast<const char*>(buffer), bytes_read);
    response = AtCommand::parse_response(raw);

    if (response.status != AtStatus::ok) {
        return ModemStatus::at_error;
    }

    return ModemStatus::ok;
}

ModemStatus ModemController::send_with_prompt(std::string_view command, const uint8_t* data, size_t length,
                                              AtResponse& response, uint32_t timeout_ms) {
    IoLockGuard lock(io_mutex_);
    if (!lock) {
        return ModemStatus::busy;
    }
    if (!(uart_ && uart_->is_open())) {
        return ModemStatus::not_connected;
    }

    // Step 1: Send the AT command
    char full_cmd[AT_CMD_MAX + 3];
    size_t cmd_len = std::min(command.size(), sizeof(full_cmd) - 3);
    std::memcpy(full_cmd, command.data(), cmd_len);
    full_cmd[cmd_len] = '\r';
    full_cmd[cmd_len + 1] = '\n';
    full_cmd[cmd_len + 2] = '\0';
    size_t full_len = cmd_len + 2;

    auto err = uart_->write(reinterpret_cast<const uint8_t*>(full_cmd), full_len);
    if (err != UartError::ok) {
        return ModemStatus::uart_error;
    }

    MODEM_LOG_DBG(">>: %.*s", (int)command.size(), command.data());

    // Step 2: Wait for the prompt "\r\n> "
    uint8_t buffer[512];
    size_t bytes_read = 0;
    err = uart_->read(buffer, sizeof(buffer) - 1, bytes_read, timeout_ms);
    if (err == UartError::timeout) {
        return ModemStatus::timeout;
    }
    if (err != UartError::ok) {
        return ModemStatus::uart_error;
    }

    // Verify we got the '>' prompt
    buffer[bytes_read] = '\0';
    std::string_view prompt(reinterpret_cast<const char*>(buffer), bytes_read);
    if (prompt.find('>') == std::string_view::npos) {
        return ModemStatus::at_error;
    }

    // Log the data payload
    char log_buf[512];
    size_t log_len = std::min(length, sizeof(log_buf) - 1);
    std::memcpy(log_buf, data, log_len);
    log_buf[log_len] = '\0';
    MODEM_LOG_DBG(">>: %s", log_buf);

    // Step 3: Send the binary payload
    err = uart_->write(data, length);
    if (err != UartError::ok) {
        return ModemStatus::uart_error;
    }

    // Step 4: Read the final response (OK or ERROR)
    bytes_read = 0;
    err = uart_->read(buffer, sizeof(buffer) - 1, bytes_read, timeout_ms);
    if (err == UartError::timeout) {
        return ModemStatus::timeout;
    }
    if (err != UartError::ok) {
        return ModemStatus::uart_error;
    }

    buffer[bytes_read] = '\0';
    std::string_view raw(reinterpret_cast<const char*>(buffer), bytes_read);
    response = AtCommand::parse_response(raw);

    if (response.status != AtStatus::ok) {
        return ModemStatus::at_error;
    }

    return ModemStatus::ok;
}

StaticVector<FixedString<URC_LINE_MAX>, ModemController::MAX_URC_LINES> ModemController::poll_urc(uint32_t timeout_ms) {
    StaticVector<FixedString<URC_LINE_MAX>, MAX_URC_LINES> urcs;
    IoLockGuard lock(io_mutex_);
    if (!lock) {
        return urcs;
    }
    if (!(uart_ && uart_->is_open())) {
        return urcs;
    }

    // Known URC prefixes to recognise
    static const char* const kPrefixes[] = {"+CREG:",      "+CGREG:",     "+CEREG:", // registration
                                            "+CGEV:",                                // PDP context events
                                            "#PSMURC:",                              // PSM entry
                                            "+CME ERROR:", "+CMS ERROR:",            // async errors
                                            "#CSURV:",                               // survey URC
                                            "SRING:",                                // socket data available
                                            "RING",        // incoming call (can be "RING" or "RING: N")
                                            "NO CARRIER",  // connection terminated
                                            "BUSY",        // line busy
                                            "NO DIALTONE", // no dial tone
                                            nullptr};

    uint8_t buffer[512];
    size_t bytes_read = 0;
    auto err = uart_->read(buffer, sizeof(buffer) - 1, bytes_read, timeout_ms);
    if (err == UartError::ok && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        std::string_view raw_chunk(reinterpret_cast<const char*>(buffer), bytes_read);

        MODEM_LOG_DBG("<< (URC poll raw): %.*s [%zu bytes]", (int)bytes_read, reinterpret_cast<const char*>(buffer),
                      bytes_read);

        // Append and parse only complete CRLF-terminated lines.
        urc_rx_buffer_.append(raw_chunk);
    } else if (err != UartError::timeout && err != UartError::ok) {
        return urcs;
    }

    size_t end = 0;
    while ((end = urc_rx_buffer_.find("\r\n")) != FixedString<URC_RX_BUFFER_MAX>::NPOS) {
        FixedString<URC_LINE_MAX> line(std::string_view(urc_rx_buffer_.data(), end));
        urc_rx_buffer_.erase(0, end + 2);

        if (line.empty()) {
            continue;
        }
        for (const char* const* p = kPrefixes; *p; ++p) {
            if (line.rfind(*p, 0) == 0) {
                urcs.push_back(line);
                break;
            }
        }
    }

    // Prevent unbounded growth if we keep receiving non-terminated garbage.
    if (urc_rx_buffer_.size() > 1024) {
        urc_rx_buffer_.erase(0, urc_rx_buffer_.size() - 512);
    }
    return urcs;
}

} // namespace modem
