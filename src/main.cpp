#include "modem/network_lte.h"
#include "modem/xe310.h"
#include "modem/modem_controller.h"
#include "modem/uart_factory.h"
#include "modem/message_queue_interface.h"
#include "modem/i_radio_lte.h"
#include "modem/log.h"
#include "rpc_helpers.h"
#include "modem/timer_interface.h"
#include "modem/timer_factory.h"
#include "app_ipc_servers.h"
#include "app_rpc_server.h"
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

#include <thread>
#include <chrono>

MODEM_LOG_MODULE_REGISTER(modem_app);

int main(int argc, char* argv[]) { // NOLINT(bugprone-exception-escape)

    std::string port = "COM17";
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "-p") {
            port = argv[i + 1];
            break;
        }
    }

    auto uart = modem::create_platform_uart();
    modem::ModemController modem_controller(std::move(uart));

    modem_controller.connect(port.c_str(), modem::UartConfig{});

    modem::xE310 modem(modem_controller);

    modem::NetworkLteConfig lteConfig;
    std::mutex network_mutex;

    // on_data_received is a lambda so it can capture ipc and forward replies.
    auto on_data_received = [&](uint8_t cid, std::string_view data, uint16_t n_bytes) {
        MODEM_LOG_INF("Data received on CID %d (%u bytes): %.*s", cid, n_bytes, static_cast<int>(data.size()),
                      data.data());
    };

    modem::NetworkLte network(modem, lteConfig, on_data_received);
    modem::RadioLteChannels channels;
    std::atomic_bool network_worker_running{false};
    app::IpcServers ipc_servers({network, lteConfig, network_worker_running, network_mutex});
    ipc_servers.start();

    app::RpcServer rpc_server({
        network,
        lteConfig,
        channels,
        network_worker_running,
        network_mutex,
        [&]() {
            return ipc_servers.run_network_command_sync(
                [&]() { return std::string("\"") + std::string(rpc::to_str(network.state())) + "\""; });
        },
    });
    rpc_server.start();

    bool fNetRes = false;
    {
        std::scoped_lock network_lock(network_mutex);
        fNetRes = network.network_connect();
    }
    if (!fNetRes) {
        MODEM_LOG_ERR("Failed to connect to network");
        // modem_controller.disconnect();
        // return 1;
    }

    bool fRes = false;
    {
        std::scoped_lock network_lock(network_mutex);
        fRes = network.server_connect(lteConfig.conn_id, "UDP", "185.205.209.91", 10000);
    }
    if (fRes) {
        MODEM_LOG_INF("Connected to server successfully");
        // Queue initial message for TX
        std::string hello = "Hello, World!";
        std::scoped_lock network_lock(network_mutex);
        network.tx_write(lteConfig.conn_id, reinterpret_cast<const uint8_t*>(hello.data()), hello.size());
        network.call_action(modem::ModemAction::send_data);
        MODEM_LOG_INF("Initial message queued for TX");
    } else {
        MODEM_LOG_ERR("Failed to connect to server");
        // return 1;
    }

    // Event consumer thread: waits for RadioLteChannels events and logs state
    // changes, responses, and log messages published by the network thread.
    std::thread event_thread([&]() {
        constexpr uint32_t ALL_EVENTS = modem::MODEM_EVT_STATE | modem::MODEM_EVT_LOG;
        while (true) {
            uint32_t matched = channels.wait(ALL_EVENTS, true, 1000);
            if (matched == 0) continue;

            if ((matched & modem::MODEM_EVT_STATE) != 0) {
                const auto& st = channels.current_state();
                MODEM_LOG_INF("Event thread: state=%u event=%u", static_cast<unsigned>(st.state),
                              static_cast<unsigned>(st.event));
            }

            if ((matched & modem::MODEM_EVT_LOG) != 0) {
                modem::ModemLogMsg log_msg{};
                while (channels.recv_log(log_msg) == modem::MessageChannelError::ok) {
                    MODEM_LOG_INF("Event thread: log: %s", log_msg.text.c_str());
                }
            }
        }
    });

    // thread to process network events and handle IPC requests
    network_worker_running.store(true); // Set to true to indicate that the network worker thread is running
    std::thread network_thread([&]() {
        while (true) {
            {
                std::scoped_lock network_lock(network_mutex);

                // Process any pending commands from IPC servers (e.g., AT commands, network requests)
                ipc_servers.process_pending_commands();

                // Process the network state machine and handle events (e.g., attach, PDP context, data transfer)
                network.loop();

                // Process cross-thread requests via message channels
                modem::process_radio_requests(channels, network);

                // Publish current state via event flags
                //channels.publish_state(network.state(), network.event());

                // Drain RX queue and forward to IPC client
                modem::QueueMessage rx_msg;
                while (network.rx_read(lteConfig.conn_id, rx_msg) == modem::QueueError::ok) {
                    std::string payload(rx_msg.data.begin(), rx_msg.data.end());
                    MODEM_LOG_INF("RX queue [conn %d]: %s (%zu bytes)", lteConfig.conn_id, payload.c_str(),
                                  rx_msg.data.size());
                    ipc_servers.send_udp_rx(rx_msg.data.data(), static_cast<uint16_t>(rx_msg.data.size()));
                }
            }
            modem::delay_ms(10);
        }
    });

    event_thread.join();
    network_thread.join();
    return 0;
}
