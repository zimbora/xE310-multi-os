/// Zephyr application entry point for the modem controller.
/// Desktop entry point is src/main.cpp (not compiled for Zephyr).

#include "modem/network_lte.h"
#include "modem/xe310.h"
#include "modem/modem_controller.h"
#include "modem/uart_factory.h"
#include "modem/message_queue_interface.h"
#include "modem/i_radio_lte.h"
#include "modem/timer_factory.h"
#include "modem/log.h"
#include <memory>
#include <string>
#include <string_view>

#include <zephyr/kernel.h>

MODEM_LOG_MODULE_REGISTER(modem_app);

static constexpr size_t EVENT_THREAD_STACK_SIZE = 2048;
static constexpr int EVENT_THREAD_PRIORITY = 7;

K_THREAD_STACK_DEFINE(event_thread_stack, EVENT_THREAD_STACK_SIZE);
static struct k_thread event_thread_data;

/// Event consumer thread: waits for RadioLteChannels events and logs state
/// changes, responses, and log messages published by the network thread.
static void event_thread_entry(void* p1, void* /*p2*/, void* /*p3*/) {
    auto* channels = static_cast<modem::RadioLteChannels*>(p1);
    constexpr uint32_t ALL_EVENTS = modem::MODEM_EVT_STATE | modem::MODEM_EVT_LOG;

    while (true) {
        uint32_t matched = channels->wait(ALL_EVENTS, true, 1000);
        if (matched == 0) continue;

        if ((matched & modem::MODEM_EVT_STATE) != 0) {
            const auto& st = channels->current_state();
            MODEM_LOG_INF("Event thread: state=%u event=%u", static_cast<unsigned>(st.state),
                          static_cast<unsigned>(st.event));
        }

        if ((matched & modem::MODEM_EVT_LOG) != 0) {
            modem::ModemLogMsg log_msg{};
            while (channels->recv_log(log_msg) == modem::MessageChannelError::ok) {
                MODEM_LOG_INF("Event thread: log: %s", log_msg.text.c_str());
            }
        }
    }
}

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
    modem::RadioLteChannels channels;

    // Start event consumer thread before connecting
    k_thread_create(&event_thread_data, event_thread_stack, K_THREAD_STACK_SIZEOF(event_thread_stack),
                    event_thread_entry, &channels, nullptr, nullptr, EVENT_THREAD_PRIORITY, 0, K_NO_WAIT);

    bool fNetRes = network.network_connect();
    if (!fNetRes) {
        MODEM_LOG_ERR("Failed to connect to network");
        modem_controller.disconnect();
        return 1;
    }

    MODEM_LOG_INF("Network connected");

    while (true) {
        network.loop();

        // Process cross-thread requests via message channels
        modem::process_radio_requests(channels, network);

        // Publish current state via event flags
        channels.publish_state(network.state(), network.event());

        modem::QueueMessage rx_msg;
        while (network.rx_read(lteConfig.conn_id, rx_msg) == modem::QueueError::ok) {
            MODEM_LOG_INF("RX [conn %d]: %.*s (%zu bytes)", lteConfig.conn_id, static_cast<int>(rx_msg.length),
                          reinterpret_cast<const char*>(rx_msg.data.data()), rx_msg.length);
        }
    }

    return 0;
}
