#include "modem/uart_interface.h"

#ifdef PLATFORM_ZEPHYR

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <cstring>

namespace modem {

class ZephyrUart : public UartInterface {
public:
    ZephyrUart() = default;
    ~ZephyrUart() override { dev_ = nullptr; }

    ZephyrUart(const ZephyrUart&) = delete;
    ZephyrUart& operator=(const ZephyrUart&) = delete;

    UartError open(const char* port, const UartConfig& config) override {
        if (dev_ != nullptr) {
            close();
        }

        dev_ = device_get_binding(port);
        if (dev_ == nullptr) {
            return UartError::invalid_config;
        }

        struct uart_config uart_cfg{};
        uart_cfg.baudrate  = config.baud_rate;
        uart_cfg.data_bits = UART_CFG_DATA_BITS_8;
        uart_cfg.stop_bits = (config.stop_bits == 2) ? UART_CFG_STOP_BITS_2 : UART_CFG_STOP_BITS_1;
        uart_cfg.parity    = UART_CFG_PARITY_NONE;
        uart_cfg.flow_ctrl = UART_CFG_FLOW_CTRL_NONE;

        switch (config.data_bits) {
            case 5: uart_cfg.data_bits = UART_CFG_DATA_BITS_5; break;
            case 6: uart_cfg.data_bits = UART_CFG_DATA_BITS_6; break;
            case 7: uart_cfg.data_bits = UART_CFG_DATA_BITS_7; break;
            default: uart_cfg.data_bits = UART_CFG_DATA_BITS_8; break;
        }

        if (uart_configure(dev_, &uart_cfg) != 0) {
            dev_ = nullptr;
            return UartError::invalid_config;
        }

        return UartError::ok;
    }

    void close() override {
        dev_ = nullptr;
    }

    bool is_open() const override {
        return dev_ != nullptr;
    }

    UartError write(const uint8_t* data, size_t length) override {
        if (dev_ == nullptr) {
            return UartError::port_not_open;
        }

        for (size_t i = 0; i < length; ++i) {
            uart_poll_out(dev_, data[i]);
        }

        return UartError::ok;
    }

    UartError read(uint8_t* buffer, size_t buffer_size, size_t& bytes_read,
                   uint32_t timeout_ms) override {
        if (dev_ == nullptr) {
            return UartError::port_not_open;
        }

        bytes_read = 0;
        int64_t deadline = k_uptime_get() + static_cast<int64_t>(timeout_ms);

        while (bytes_read < buffer_size) {
            unsigned char c;
            int ret = uart_poll_in(dev_, &c);
            if (ret == 0) {
                buffer[bytes_read++] = static_cast<uint8_t>(c);
            } else {
                // No data available — check timeout
                if (k_uptime_get() >= deadline) {
                    break;
                }
                k_sleep(K_MSEC(1));
            }
        }

        if (bytes_read == 0) {
            return UartError::timeout;
        }

        return UartError::ok;
    }

private:
    const struct device* dev_ = nullptr;
};

} // namespace modem

#include "modem/uart_factory.h"

namespace modem {

std::unique_ptr<UartInterface> create_platform_uart() {
    return std::make_unique<ZephyrUart>();
}

} // namespace modem

#endif
