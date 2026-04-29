#include "modem/modem_controller.h"
#include "modem/log.h"

#include <cstring>

namespace modem {

ModemController::ModemController(std::unique_ptr<UartInterface> uart)
    : uart_(std::move(uart)) {}

ModemStatus ModemController::connect(const char* port, const UartConfig& config) {
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
    if (uart_ && uart_->is_open()) {
        uart_->close();
    }
}

bool ModemController::is_connected() const {
    return uart_ && uart_->is_open();
}

ModemStatus ModemController::send_command(const AtCommand& cmd, AtResponse& response) {
    if (!is_connected()) {
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

    // Read response
    uint8_t buffer[512];
    size_t bytes_read = 0;
    while(err == UartError::ok) {
        memset(buffer, 0, sizeof(buffer));
        err = uart_->read(buffer, sizeof(buffer) - 1, bytes_read, cmd.timeout_ms());
        if(err != modem::UartError::ok) {
            switch(err) {
                case UartError::timeout:
                    return ModemStatus::timeout;
                default:
                    return ModemStatus::uart_error;
            }
        }

        if(bytes_read > 0) {
            buffer[bytes_read] = '\0';
            std::string raw(reinterpret_cast<const char*>(buffer), bytes_read);
            response = AtCommand::parse_response(raw);
            
        }
        
        if (response.status == AtStatus::ok) {
            return ModemStatus::ok;
        }
    }

    return ModemStatus::at_error;
}

ModemStatus ModemController::send_raw(const std::string& command, AtResponse& response,
                                      uint32_t timeout_ms) {
    AtCommand cmd(command, timeout_ms);
    return send_command(cmd, response);
}

ModemStatus ModemController::send_binary(const std::vector<uint8_t>& data, AtResponse& response,
                                         uint32_t timeout_ms) {
    if (!is_connected()) {
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
    if (!is_connected()) {
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

} // namespace modem
