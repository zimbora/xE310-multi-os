#include "modem/modem_controller.h"

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

    // Read response
    uint8_t buffer[512];
    size_t bytes_read = 0;
    err = uart_->read(buffer, sizeof(buffer) - 1, bytes_read, cmd.timeout_ms());
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

ModemStatus ModemController::send_raw(const std::string& command, AtResponse& response,
                                      uint32_t timeout_ms) {
    AtCommand cmd(command, timeout_ms);
    return send_command(cmd, response);
}

} // namespace modem
