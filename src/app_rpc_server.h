#pragma once

#include "ipc_server.h"
#include "modem/i_radio_lte.h"
#include "modem/network_lte.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace app {

using GetNetworkStateFn = std::function<std::string()>;

struct RpcServerContext {
    modem::NetworkLte& network;
    modem::NetworkLteConfig& lte_config;
    modem::RadioLteChannels& channels;
    std::atomic_bool& network_worker_running;
    std::mutex& network_mutex;
    GetNetworkStateFn get_network_state;
};

class RpcServer {
public:
    explicit RpcServer(RpcServerContext context);

    void start();

private:
    std::string handle_request(const std::string& request);
    std::pair<bool, std::string> request_radio_state(modem::RadioLteRequestMsg msg, uint32_t timeout_ms = 5000U);
    std::pair<bool, std::string> request_radio_state_impl(modem::RadioLteRequestMsg msg, uint32_t timeout_ms);

    RpcServerContext context_;
    IpcServer rpc_ipc_;
};

} // namespace app
