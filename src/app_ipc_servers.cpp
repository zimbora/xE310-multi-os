#include "app_ipc_servers.h"

#include "modem/log.h"
#include "rpc_helpers.h"

#include <cstdint>
#include <future>
#include <string>
#include <vector>

MODEM_LOG_MODULE_REGISTER(app_ipc_servers);

namespace app {

IpcServers::IpcServers(IpcServersContext context)
    : context_(context),
      ipc_(9000, nullptr, IpcServer::Mode::line),
      coap_ipc_(9001, nullptr, IpcServer::Mode::framed),
      at_ipc_(9002, nullptr, IpcServer::Mode::line) {}

void IpcServers::start() {
    configure_data_ipc();
    configure_coap_ipc();
    configure_at_ipc();

    if (!ipc_.start()) {
        MODEM_LOG_WRN("IPC server failed to start on port 9000 (continuing without it)");
    } else {
        MODEM_LOG_INF("IPC server listening on localhost:9000");
    }

    if (!coap_ipc_.start()) {
        MODEM_LOG_WRN("CoAP IPC server failed to start on port 9001 (continuing without it)");
    } else {
        MODEM_LOG_INF("CoAP IPC server listening on localhost:9001 (framed binary)");
    }

    if (!at_ipc_.start()) {
        MODEM_LOG_WRN("AT IPC server failed to start on port 9002 (continuing without it)");
    } else {
        MODEM_LOG_INF("AT IPC server listening on localhost:9002 (AT command passthrough)");
    }
}

void IpcServers::send_udp_rx(const uint8_t* data, uint16_t len) {
    ipc_.send(data, len);
}

void IpcServers::enqueue_network_command(std::function<void()> command) {
    std::scoped_lock command_queue_lock(command_queue_mutex_);
    command_queue_.emplace_back(std::move(command));
}

void IpcServers::process_pending_commands() {
    std::deque<std::function<void()>> pending_commands;
    {
        std::scoped_lock command_queue_lock(command_queue_mutex_);
        pending_commands.swap(command_queue_);
    }

    for (const auto& command : pending_commands) {
        command();
    }
}

std::string IpcServers::run_network_command_sync(const std::function<std::string()>& command) {
    if (!context_.network_worker_running.load()) {
        std::scoped_lock network_lock(context_.network_mutex);
        return command();
    }

    auto done = std::make_shared<std::promise<std::string>>();
    auto result = done->get_future();
    enqueue_network_command([command, done]() mutable { done->set_value(command()); });
    return result.get();
}

std::pair<bool, std::string> IpcServers::send_at_command_sync(const std::string& cmd, uint32_t timeout_ms) {
    struct AtCommandResult {
        bool ok = false;
        std::string response;
    };

    auto run_command = [&]() {
        modem::FixedString<modem::AT_RESPONSE_MAX> response;
        MODEM_LOG_INF("AT IPC >> %s", cmd.c_str());

        AtCommandResult result{};
        result.ok = context_.network.send_at_command(cmd, response, timeout_ms);
        if (result.ok) {
            if (!response.empty()) response.append("\r\n");
            response.append("OK\r\n");
            MODEM_LOG_INF("AT IPC << %s", response.c_str());
        }
        result.response = response.c_str();
        return result;
    };

    if (!context_.network_worker_running.load()) {
        std::scoped_lock network_lock(context_.network_mutex);
        AtCommandResult result = run_command();
        return {result.ok, result.response};
    }

    auto done = std::make_shared<std::promise<AtCommandResult>>();
    auto result = done->get_future();
    enqueue_network_command([run_command, done]() mutable { done->set_value(run_command()); });

    AtCommandResult command_result = result.get();
    return {command_result.ok, command_result.response};
}

void IpcServers::configure_data_ipc() {
    ipc_.set_callback([this](const uint8_t* data, uint16_t len) {
        std::vector<uint8_t> payload(data, data + len);
        enqueue_network_command([this, payload = std::move(payload), len]() {
            if (context_.network.state() != modem::NetworkLteState::data_ready) {
                const std::string error = std::string("ERROR: network not ready, state=") +
                                          rpc::to_str(context_.network.state());
                MODEM_LOG_WRN("IPC: %s", error.c_str());
                ipc_.send(reinterpret_cast<const uint8_t*>(error.data()), static_cast<uint16_t>(error.size()));
                return;
            }

            modem::QueueError queue_err =
                context_.network.tx_write(context_.lte_config.conn_id, payload.data(), len);
            if (queue_err != modem::QueueError::ok) {
                const std::string error = std::string("ERROR: tx_write failed, code=") +
                                          std::to_string(static_cast<int>(queue_err));
                MODEM_LOG_ERR("IPC: %s", error.c_str());
                ipc_.send(reinterpret_cast<const uint8_t*>(error.data()), static_cast<uint16_t>(error.size()));
                return;
            }

            context_.network.call_action(modem::ModemAction::send_data);
            MODEM_LOG_INF("IPC: queued %u bytes for TX on conn %d", len, context_.lte_config.conn_id);
        });
        return 0;
    });
}

void IpcServers::configure_coap_ipc() {
    coap_ipc_.set_callback([this](const uint8_t* data, uint16_t len) {
        std::vector<uint8_t> payload(data, data + len);
        enqueue_network_command([this, payload = std::move(payload), len]() {
            if (context_.network.state() != modem::NetworkLteState::data_ready) {
                const std::string error = std::string("ERROR: network not ready, state=") +
                                          rpc::to_str(context_.network.state());
                MODEM_LOG_WRN("CoAP IPC: %s", error.c_str());
                coap_ipc_.send(reinterpret_cast<const uint8_t*>(error.data()), static_cast<uint16_t>(error.size()));
                return;
            }

            modem::QueueError queue_err =
                context_.network.tx_write(context_.lte_config.conn_id, payload.data(), len);
            if (queue_err != modem::QueueError::ok) {
                const std::string error = std::string("ERROR: tx_write failed, code=") +
                                          std::to_string(static_cast<int>(queue_err));
                MODEM_LOG_ERR("CoAP IPC: %s", error.c_str());
                coap_ipc_.send(reinterpret_cast<const uint8_t*>(error.data()), static_cast<uint16_t>(error.size()));
                return;
            }

            context_.network.call_action(modem::ModemAction::send_data);
            MODEM_LOG_INF("CoAP IPC: queued %u bytes for TX on conn %d", len, context_.lte_config.conn_id);
        });
    });
}

void IpcServers::configure_at_ipc() {
    at_ipc_.set_connect_callback([this]() {
        enqueue_network_command([this]() {
            MODEM_LOG_INF("AT IPC: client connected, entering transparent mode");
            context_.network.enter_transparent_mode();
        });
    });

    at_ipc_.set_callback([this](const uint8_t* data, uint16_t len) {
        std::string cmd(reinterpret_cast<const char*>(data), len);
        auto [ok, response] = send_at_command_sync(cmd, static_cast<uint32_t>(210000));
        if (!ok) {
            MODEM_LOG_ERR("AT IPC: command failed");
        }

        at_ipc_.send(reinterpret_cast<const uint8_t*>(response.data()), static_cast<uint16_t>(response.size()));
    });

    at_ipc_.set_disconnect_callback([this]() {
        enqueue_network_command([this]() {
            MODEM_LOG_INF("AT IPC: client disconnected, leaving transparent mode");
            context_.network.leave_transparent_mode();
        });
    });
}

} // namespace app
