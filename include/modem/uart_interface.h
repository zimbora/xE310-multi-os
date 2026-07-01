#pragma once

#include <cstddef>
#include <cstdint>

namespace modem {

enum class UartError {
    ok = 0,
    timeout,
    write_failed,
    read_failed,
    port_not_open,
    invalid_config,
};

struct UartConfig {
    uint32_t baud_rate = 115200;
    uint8_t data_bits = 8;
    uint8_t stop_bits = 1;
    uint32_t timeout_ms = 1000;
};

/// Abstract UART interface — implemented per platform.
class UartInterface {
public:
    virtual ~UartInterface() = default;

    virtual UartError open(const char* port, const UartConfig& config) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;

    virtual UartError write(const uint8_t* data, size_t length) = 0;
    virtual UartError read(uint8_t* buffer, size_t buffer_size, size_t& bytes_read, uint32_t timeout_ms) = 0;
};

} // namespace modem
