#include "modem/modem_controller.h"
#include "modem/log.h"
#include "modem/timer_factory.h"

#include <cstring>

namespace modem {

ModemController::ModemController(std::unique_ptr<UartInterface> uart,
                                 std::unique_ptr<TimerInterface> timer)
    : uart_(std::move(uart)),
      cmd_timer_(timer ? std::move(timer) : create_platform_timer()) {}

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

    const auto& cmd_str = cmd.command_string();
    std::string full_cmd = cmd_str + "\r\n";

    auto err = uart_->write(reinterpret_cast<const uint8_t*>(full_cmd.c_str()),
                            full_cmd.size());
    if (err != UartError::ok) {
        return ModemStatus::uart_error;
    }
    MODEM_LOG_DBG(">>: %s", cmd_str.c_str());

    // Read response — enforce an overall deadline across all reads.
    // elapsed_ms() is queried each iteration; remaining time is passed to read()
    // so the UART layer also honours the shrinking budget.
    const uint32_t total_ms = cmd.timeout_ms();
    cmd_timer_->stop();
    cmd_timer_->start(total_ms, nullptr);

    uint8_t buffer[512];
    size_t bytes_read = 0;
    std::string accumulated;

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
            response = AtCommand::parse_response(accumulated);

            if (response.status == AtStatus::ok || response.status == AtStatus::error || response.status == AtStatus::busy) {
                // Extract any URCs that arrived after the status line and buffer them for poll_urc().
                // This prevents URCs in the response window from corrupting the payload.
                size_t status_end = accumulated.find(response.status == AtStatus::ok ? "OK" : 
                                                     response.status == AtStatus::error ? "ERROR" : "BUSY");
                if (status_end != std::string::npos) {
                    // Find the end of the status line (OK\r\n or ERROR\r\n)
                    status_end = accumulated.find("\r\n", status_end);
                    if (status_end != std::string::npos) {
                        status_end += 2; // skip the \r\n
                        // Anything after the status line goes to the URC buffer
                        if (status_end < accumulated.size()) {
                            urc_rx_buffer_ += accumulated.substr(status_end);
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

ModemStatus ModemController::send_raw(const std::string& command, AtResponse& response,
                                      uint32_t timeout_ms, bool retry) {
    AtCommand cmd(command, timeout_ms);
    ModemStatus status = send_command(cmd, response);

    if (retry && status == ModemStatus::timeout) {
        for (uint8_t attempt = 1; attempt < MAX_AT_RETRIES && status == ModemStatus::timeout; ++attempt) {
            MODEM_LOG_DBG("Retrying AT command (%u/%u): %s", attempt, MAX_AT_RETRIES - 1, command.c_str());
            AtCommand retry_cmd(command, timeout_ms);
            status = send_command(retry_cmd, response);
        }
    }

    return status;
}

ModemStatus ModemController::send_binary(const std::vector<uint8_t>& data, AtResponse& response,
                                         uint32_t timeout_ms) {
    IoLockGuard lock(io_mutex_);
    if (!lock) {
        return ModemStatus::busy;
    }
    if (!(uart_ && uart_->is_open())) {
        return ModemStatus::not_connected;
    }

    auto err = uart_->write(data.data(), data.size());
    if (err != UartError::ok) {
        return ModemStatus::uart_error;
    }

    std::string hex;
    hex.reserve(data.size() * 3);
    char byte_buf[4];
    for (uint8_t b : data) {
        snprintf(byte_buf, sizeof(byte_buf), "%02x ", b);
        hex += byte_buf;
    }
    MODEM_LOG_DBG(">>: [binary %zu bytes]: %s", data.size(), hex.c_str());
    (void)hex;

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
    std::string raw(reinterpret_cast<const char*>(buffer), bytes_read);
    response = AtCommand::parse_response(raw);

    if (response.status != AtStatus::ok) {
        return ModemStatus::at_error;
    }

    return ModemStatus::ok;
}

ModemStatus ModemController::send_with_prompt(const std::string& command,
                                               const std::vector<uint8_t>& data,
                                               AtResponse& response,
                                               uint32_t timeout_ms) {
    IoLockGuard lock(io_mutex_);
    if (!lock) {
        return ModemStatus::busy;
    }
    if (!(uart_ && uart_->is_open())) {
        return ModemStatus::not_connected;
    }

    // Step 1: Send the AT command
    std::string full_cmd = command + "\r\n";
    auto err = uart_->write(reinterpret_cast<const uint8_t*>(full_cmd.c_str()),
                            full_cmd.size());
    if (err != UartError::ok) {
        return ModemStatus::uart_error;
    }

    MODEM_LOG_DBG(">>: %s", command.c_str());

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
    std::string prompt(reinterpret_cast<const char*>(buffer), bytes_read);
    if (prompt.find('>') == std::string::npos) {
        return ModemStatus::at_error;
    }

    std::vector<char> buf(data.begin(), data.end());
    buf.push_back('\0');
    MODEM_LOG_DBG(">>: %s", buf.data());

    // Step 3: Send the binary payload
    err = uart_->write(data.data(), data.size());
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
    std::string raw(reinterpret_cast<const char*>(buffer), bytes_read);
    response = AtCommand::parse_response(raw);

    if (response.status != AtStatus::ok) {
        return ModemStatus::at_error;
    }

    return ModemStatus::ok;
}

std::vector<std::string> ModemController::poll_urc(uint32_t timeout_ms) {
    std::vector<std::string> urcs;
    IoLockGuard lock(io_mutex_);
    if (!lock) {
        return urcs;
    }
    if (!(uart_ && uart_->is_open())) {
        return urcs;
    }

    // Known URC prefixes to recognise
    static const char* const kPrefixes[] = {
        "+CREG:",  "+CGREG:", "+CEREG:",  // registration
        "+CGEV:",                           // PDP context events
        "#PSMURC:",                         // PSM entry
        "+CME ERROR:", "+CMS ERROR:",       // async errors
        "#CSURV:",                          // survey URC
        "SRING:",                           // socket data available
        "RING",                             // incoming call (can be "RING" or "RING: N")
        "NO CARRIER",                       // connection terminated
        "BUSY",                             // line busy
        "NO DIALTONE",                      // no dial tone
        nullptr
    };

    uint8_t buffer[512];
    size_t bytes_read = 0;
    auto err = uart_->read(buffer, sizeof(buffer) - 1, bytes_read, timeout_ms);
    if (err == UartError::ok && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        std::string raw_chunk(reinterpret_cast<const char*>(buffer), bytes_read);

        MODEM_LOG_DBG("<< (URC poll raw): %s [%zu bytes]", raw_chunk.c_str(), bytes_read);

        // Append and parse only complete CRLF-terminated lines. This avoids losing
        // URCs split across reads, e.g. "S" then "RING: 1\r\n".
        urc_rx_buffer_ += raw_chunk;
    } else if (err != UartError::timeout && err != UartError::ok) {
        // On UART errors, return what we have without modifying buffers.
        return urcs;
    }

    size_t end = 0;
    while ((end = urc_rx_buffer_.find("\r\n")) != std::string::npos) {
        std::string line = urc_rx_buffer_.substr(0, end);
        urc_rx_buffer_.erase(0, end + 2);

        if (line.empty()) {
            continue;
        }
        for (const char* const* p = kPrefixes; *p; ++p) {
            if (line.rfind(*p, 0) == 0) {
                //MODEM_LOG_DBG("URC (extracted): %s", line.c_str());
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
