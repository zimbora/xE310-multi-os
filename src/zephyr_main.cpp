/// Zephyr application entry point for the modem controller.
/// Desktop entry point is src/main.cpp (not compiled for Zephyr).

#include "modem/network_lte.h"
#include "modem/xe310.h"
#include "modem/modem_controller.h"
#include "modem/uart_factory.h"
#include "modem/message_queue_interface.h"
#include "modem/timer_factory.h"
#include "modem/log.h"
#include <memory>
#include <string>
#include <string_view>

MODEM_LOG_MODULE_REGISTER(modem_app);

int main() { // NOLINT(bugprone-exception-escape)
    auto uart = modem::create_platform_uart();
    modem::ModemController modem_controller(std::move(uart));

    // "UART_1" is passed to device_get_binding() in ZephyrUart::open().
    // Update to match the devicetree label for the modem UART on the target board.
    modem_controller.connect("UART_1", modem::UartConfig{});

    modem::xE310 modem(modem_controller);
    auto status = modem.at_ok();
    if (status != modem::ModemStatus::ok) {
        MODEM_LOG_ERR("Modem not responsive");
        modem_controller.disconnect();
        return 1;
    }

    MODEM_LOG_INF("Modem initialized successfully");

    modem::NetworkLteConfig lteConfig;

    auto on_data_received = [&](uint8_t cid, std::string_view data, uint16_t n_bytes) {
        MODEM_LOG_INF("Data received on CID %d (%u bytes): %.*s", cid, n_bytes, static_cast<int>(data.size()),
                      data.data());
    };

    modem::NetworkLte network(modem, lteConfig, on_data_received);

    bool fNetRes = network.network_connect();
    if (!fNetRes) {
        MODEM_LOG_ERR("Failed to connect to network");
        modem_controller.disconnect();
        return 1;
    }

    MODEM_LOG_INF("Network connected");

    while (true) {
        network.loop();

        modem::QueueMessage rx_msg;
        while (network.rx_read(lteConfig.conn_id, rx_msg) == modem::QueueError::ok) {
            MODEM_LOG_INF("RX [conn %d]: %.*s (%zu bytes)", lteConfig.conn_id, static_cast<int>(rx_msg.length),
                          reinterpret_cast<const char*>(rx_msg.data.data()), rx_msg.length);
        }
    }

    return 0;
}
