#pragma once

#include "ipc_server.h"
#include "modem/network_lte.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace app {

struct IpcServersContext {
    modem::NetworkLte& network;
    modem::NetworkLteConfig& lte_config;
    std::atomic_bool& network_worker_running;
    std::mutex& network_mutex;
};

class IpcServers {
public:
    explicit IpcServers(IpcServersContext context);

    void start();
    void send_udp_rx(const uint8_t* data, uint16_t len);
    void enqueue_network_command(std::function<void()> command);
    void process_pending_commands();
    std::string run_network_command_sync(const std::function<std::string()>& command);
    std::pair<bool, std::string> send_at_command_sync(const std::string& cmd, uint32_t timeout_ms);

private:
    void configure_data_ipc();
    void configure_coap_ipc();
    void configure_at_ipc();

    IpcServersContext context_;
    IpcServer ipc_;
    IpcServer coap_ipc_;
    IpcServer at_ipc_;
    std::mutex command_queue_mutex_;
    std::deque<std::function<void()>> command_queue_;
};

} // namespace app