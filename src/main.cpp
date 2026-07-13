#include "modem/network_lte.h"
#include "modem/xe310.h"
#include "modem/modem_controller.h"
#include "modem/uart_factory.h"
#include "modem/message_queue_interface.h"
#include "modem/i_radio_lte.h"
#include "modem/log.h"
#include "ipc_server.h"
#include "rpc_helpers.h"
#include "modem/timer_interface.h"
#include "modem/timer_factory.h"
#include <memory>
#include <algorithm>
#include <atomic>
#include <deque>
#include <functional>
#include <future>
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

    // IPC server: messages from external process (e.g. LwM2M agent) are
    // queued for TX. Wire format: newline-delimited text — compatible with nc.
    // Usage: nc localhost 9000   then type messages and press Enter.
    IpcServer ipc(9000, nullptr, IpcServer::Mode::line); // callback set after network is constructed

    // CoAP IPC server on port 9001: binary framed [uint16_t len LE][payload].
    // Usage: send CoAP binary frames from a LwM2M agent or test tool.
    IpcServer coap_ipc(9001, nullptr, IpcServer::Mode::framed);

    // on_data_received is a lambda so it can capture ipc and forward replies.
    auto on_data_received = [&](uint8_t cid, std::string_view data, uint16_t n_bytes) {
        MODEM_LOG_INF("Data received on CID %d (%u bytes): %.*s", cid, n_bytes, static_cast<int>(data.size()),
                      data.data());
        // ipc.send(reinterpret_cast<const uint8_t*>(data.data()),static_cast<uint16_t>(n_bytes));
    };

    modem::NetworkLte network(modem, lteConfig, on_data_received);
    modem::RadioLteChannels channels;
    std::atomic_bool network_worker_running{false};
    std::mutex command_queue_mutex;
    std::deque<std::function<void()>> command_queue;

    auto enqueue_network_command = [&](std::function<void()> command) {
        std::scoped_lock command_queue_lock(command_queue_mutex);
        command_queue.emplace_back(std::move(command));
    };

    std::function<std::pair<bool, std::string>(modem::RadioLteRequestMsg, uint32_t)> request_radio_state_impl;
    request_radio_state_impl = [&](modem::RadioLteRequestMsg msg,
                                   uint32_t timeout_ms) -> std::pair<bool, std::string> {
        if (!network_worker_running.load()) {
            std::scoped_lock network_lock(network_mutex);
            return {false, "ERROR: network worker not running"};
        }

        modem::MessageChannelError send_err = channels.send_request(msg, timeout_ms);
        if (send_err != modem::MessageChannelError::ok) {
            return {false, "ERROR: failed to send request"};
        }

        uint32_t matched = channels.wait(modem::MODEM_EVT_RESPONSE, true, timeout_ms);
        if ((matched & modem::MODEM_EVT_RESPONSE) == 0U) {
            return {false, "ERROR: timeout waiting response"};
        }

        switch (msg.type) {
            case modem::RadioLteRequestType::get_registration_info: {
                modem::ModemTypedResponseMsg<modem::RegistrationInfo> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid registration_info response"};
                }
                return {resp.ok, rpc::to_json(resp.value)};
            }
            case modem::RadioLteRequestType::get_signal_quality: {
                modem::ModemTypedResponseMsg<modem::SignalQuality> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid signal_quality response"};
                }
                return {resp.ok, rpc::to_json(resp.value)};
            }
            case modem::RadioLteRequestType::get_iccid: {
                modem::ModemTypedResponseMsg<modem::FixedString<modem::MODEM_SHORT_STR>> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid iccid response"};
                }
                return {resp.ok, "\"" + std::string(resp.value.c_str()) + "\""};
            }
            case modem::RadioLteRequestType::get_imsi: {
                modem::ModemTypedResponseMsg<modem::FixedString<modem::MODEM_SHORT_STR>> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid imsi response"};
                }
                return {resp.ok, "\"" + std::string(resp.value.c_str()) + "\""};
            }
            case modem::RadioLteRequestType::get_modem_info: {
                modem::ModemTypedResponseMsg<modem::ModemInfo> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid modem_info response"};
                }
                return {resp.ok, rpc::to_json(resp.value)};
            }
            case modem::RadioLteRequestType::get_sim_status: {
                modem::ModemTypedResponseMsg<modem::SimStatus> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid sim_status response"};
                }
                return {resp.ok, "\"" + std::string(rpc::to_str(resp.value)) + "\""};
            }
            case modem::RadioLteRequestType::get_radio_tech: {
                modem::ModemTypedResponseMsg<modem::RadioTech> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid radio_tech response"};
                }
                return {resp.ok, "\"" + std::string(rpc::to_str(resp.value)) + "\""};
            }
            case modem::RadioLteRequestType::get_reg_status: {
                modem::ModemTypedResponseMsg<modem::RegStatus> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid reg_status response"};
                }
                return {resp.ok, "\"" + std::string(rpc::to_str(resp.value)) + "\""};
            }
            case modem::RadioLteRequestType::get_network_info: {
                modem::ModemTypedResponseMsg<modem::NetworkInfo> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid network_info response"};
                }
                return {resp.ok, rpc::to_json(resp.value)};
            }
            case modem::RadioLteRequestType::get_psm_mode: {
                modem::ModemTypedResponseMsg<modem::PsmMode> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid psm_mode response"};
                }
                return {resp.ok, "\"" + std::string(rpc::to_str(resp.value)) + "\""};
            }
            case modem::RadioLteRequestType::get_cpsms_config: {
                modem::ModemTypedResponseMsg<modem::CpsmsConfig> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid cpsms_config response"};
                }
                return {resp.ok, rpc::to_json(resp.value)};
            }
            case modem::RadioLteRequestType::get_telit_cpsms_config: {
                modem::ModemTypedResponseMsg<modem::TelitCpsmsConfig> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid telit_cpsms_config response"};
                }
                return {resp.ok, rpc::to_json(resp.value)};
            }
            case modem::RadioLteRequestType::get_telit_cpsms_status: {
                modem::ModemTypedResponseMsg<modem::TelitCpsmsStatus> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid telit_cpsms_status response"};
                }
                return {resp.ok, rpc::to_json(resp.value)};
            }
            case modem::RadioLteRequestType::get_network_survey_result: {
                modem::ModemTypedResponseMsg<modem::NetworkSurveyResult> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid network_survey_result response"};
                }
                return {resp.ok, rpc::to_json(resp.value)};
            }
            case modem::RadioLteRequestType::get_available_operators: {
                modem::ModemTypedResponseMsg<modem::StaticVector<modem::Operator, modem::xE310::MAX_OPERATORS>> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid available_operators response"};
                }
                return {resp.ok, rpc::to_json(resp.value)};
            }
            case modem::RadioLteRequestType::get_csurv_result: {
                modem::ModemTypedResponseMsg<modem::CsurvResult> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid csurv_result response"};
                }
                return {resp.ok, rpc::to_json(resp.value)};
            }
            case modem::RadioLteRequestType::scan_networks: {
                modem::ModemTypedResponseMsg<bool> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid scan_networks response"};
                }
                if (!resp.ok || !resp.value) {
                    return {false, "ERROR: scan_networks failed"};
                }
                return request_radio_state_impl({modem::RadioLteRequestType::get_csurv_result, 0U, 0U}, timeout_ms);
            }
            case modem::RadioLteRequestType::get_server_info_array: {
                modem::ModemTypedResponseMsg<const modem::ServerInfo*> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid server_info_array response"};
                }
                if (!resp.ok || resp.value == nullptr) {
                    return {false, "ERROR: server_info_array unavailable"};
                }
                if (msg.arg0 >= 1 && msg.arg0 <= MAX_SERVER_CONNECTIONS) {
                    return {true, rpc::to_json(resp.value[msg.arg0 - 1])};
                }
                std::string out = "[";
                for (int i = 0; i < MAX_SERVER_CONNECTIONS; ++i) {
                    if (i > 0) out += ',';
                    out += rpc::to_json(resp.value[i]);
                }
                out += "]";
                return {true, out};
            }
            case modem::RadioLteRequestType::get_config: {
                modem::ModemTypedResponseMsg<modem::NetworkLteConfig> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid config response"};
                }
                return {resp.ok, rpc::config_to_json(resp.value)};
            }
            case modem::RadioLteRequestType::set_config:
            case modem::RadioLteRequestType::network_connect:
            case modem::RadioLteRequestType::network_disconnect:
            case modem::RadioLteRequestType::server_disconnect:
            case modem::RadioLteRequestType::force_psm:
            default: {
                modem::ModemTypedResponseMsg<bool> resp{};
                if (channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                    return {false, "ERROR: invalid response"};
                }
                return {resp.ok && resp.value, resp.value ? "true" : "false"};
            }
        }
    };

    auto request_radio_state = [&](modem::RadioLteRequestMsg msg,
                                   uint32_t timeout_ms = 5000U) -> std::pair<bool, std::string> {
        return request_radio_state_impl(msg, timeout_ms);
    };

    auto run_network_command_sync = [&](auto command) {
        using Result = decltype(command());
        if (!network_worker_running.load()) {
            std::scoped_lock network_lock(network_mutex);
            return command();
        }

        auto done = std::make_shared<std::promise<Result>>();
        auto result = done->get_future();
        enqueue_network_command([command = std::move(command), done]() mutable { done->set_value(command()); });
        return result.get();
    };

    ipc.set_callback([&](const uint8_t* data, uint16_t len) {
        std::vector<uint8_t> payload(data, data + len);
        enqueue_network_command([&, payload = std::move(payload), len]() {
            if (network.network_connect()) {
                MODEM_LOG_INF("Successfully connected to network");
            } else {
                MODEM_LOG_ERR("Failed to connect to network");
                ipc.stop();
                return;
            }
            bool fRes = network.server_connect(lteConfig.conn_id, "UDP", "185.205.209.91", 10000);
            if (fRes) {
                network.tx_write(lteConfig.conn_id, payload.data(), len);
                network.call_action(modem::ModemAction::send_data);
                MODEM_LOG_INF("IPC: queued %u bytes for TX on conn %d", len, lteConfig.conn_id);
            } else {
                MODEM_LOG_ERR("Failed to connect to server");
                ipc.stop();
            }
        });
        return 0;
    });
    if (!ipc.start()) {
        MODEM_LOG_WRN("IPC server failed to start on port 9000 (continuing without it)");
    } else {
        MODEM_LOG_INF("IPC server listening on localhost:9000");
    }

    coap_ipc.set_callback([&](const uint8_t* data, uint16_t len) {
        std::vector<uint8_t> payload(data, data + len);
        enqueue_network_command([&, payload = std::move(payload), len]() {
            network.tx_write(lteConfig.conn_id, payload.data(), len);
            network.call_action(modem::ModemAction::send_data);
            MODEM_LOG_INF("CoAP IPC: queued %u bytes for TX on conn %d", len, lteConfig.conn_id);
        });
    });
    if (!coap_ipc.start()) {
        MODEM_LOG_WRN("CoAP IPC server failed to start on port 9001 (continuing without it)");
    } else {
        MODEM_LOG_INF("CoAP IPC server listening on localhost:9001 (framed binary)");
    }

    // AT command passthrough server on port 9002 (line mode, nc compatible).
    // On connect  → modem enters transparent mode (raw AT).
    // On message  → forward as AT command, reply with response.
    // On disconnect → modem leaves transparent mode.
    IpcServer at_ipc(9002, nullptr, IpcServer::Mode::line);
    at_ipc.set_connect_callback([&]() {
        enqueue_network_command([&]() {
            MODEM_LOG_INF("AT IPC: client connected, entering transparent mode");
            network.enter_transparent_mode();
        });
    });
    at_ipc.set_callback([&](const uint8_t* data, uint16_t len) {
        struct AtCommandResult {
            bool ok = false;
            std::string response;
        };

        std::string cmd(reinterpret_cast<const char*>(data), len);
        AtCommandResult cmd_result = run_network_command_sync([&]() {
            modem::FixedString<modem::AT_RESPONSE_MAX> response;
            MODEM_LOG_INF("AT IPC >> %s", cmd.c_str());
            AtCommandResult result{};
            result.ok = network.send_at_command(cmd, response, static_cast<uint32_t>(210000));
            if (result.ok) {
                if (!response.empty()) response.append("\r\n");
                response.append("OK\r\n");
                MODEM_LOG_INF("AT IPC << %s", response.c_str());
            } else {
                MODEM_LOG_ERR("AT IPC: command failed");
            }
            result.response = response.c_str();
            return result;
        });

        at_ipc.send(reinterpret_cast<const uint8_t*>(cmd_result.response.data()),
                    static_cast<uint16_t>(cmd_result.response.size()));
    });
    at_ipc.set_disconnect_callback([&]() {
        enqueue_network_command([&]() {
            MODEM_LOG_INF("AT IPC: client disconnected, leaving transparent mode");
            network.leave_transparent_mode();
        });
    });
    if (!at_ipc.start()) {
        MODEM_LOG_WRN("AT IPC server failed to start on port 9002 (continuing without it)");
    } else {
        MODEM_LOG_INF("AT IPC server listening on localhost:9002 (AT command passthrough)");
    }

    // RPC server on port 9003 (line mode, nc compatible).
    // GET <RESOURCE>         → JSON response.
    // SET CONFIG <key>=<val> → update NetworkLteConfig fields, returns updated config as JSON.
    // SET NETWORKCONNECT → attach to network and activate PDP context.
    // SET NETWORKDISCONNECT [conn_id] → close server socket and move modem to sleep/off path.
    // SET FORCEPSM → force modem into PSM mode immediately.
    // Resources: CONFIG, MODEMINFO, SIMSTATUS, RADIOTECH, REGSTATUS, REGINFO, NETWORKINFO,
    //            SIGNALQUALITY, PSMMODE, CPSMSCONFIG, TELITCPSMSCONFIG, TELITCPSMSSTATUS,
    //            SURVEYRESULT, OPERATORLIST, SCANSURVEY, SERVERINFO [n], STATE, ALL
    IpcServer rpc_ipc(9003, nullptr, IpcServer::Mode::line);

    auto handle_rpc = [&](const std::string& req) -> std::string {
        auto to_upper = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)toupper(c); });
            return s;
        };
        auto sp = req.find(' ');
        std::string cmd = to_upper(sp == std::string::npos ? req : req.substr(0, sp));
        std::string sub_orig = sp == std::string::npos ? "" : req.substr(sp + 1);
        std::string sub = to_upper(sub_orig);

        if (cmd == "GET") {
            if (sub == "SCANSURVEY") {
                auto [ok, payload] = request_radio_state({modem::RadioLteRequestType::scan_networks, 0U, 0U},
                                                         static_cast<uint32_t>(210000));
                if (!ok) return payload;
                return payload;
            }
            if (sub == "CONFIG") return request_radio_state({modem::RadioLteRequestType::get_config, 0U, 0U}).second;
            if (sub == "MODEMINFO")
                return request_radio_state({modem::RadioLteRequestType::get_modem_info, 0U, 0U}).second;
            if (sub == "SIMSTATUS")
                return request_radio_state({modem::RadioLteRequestType::get_sim_status, 0U, 0U}).second;
            if (sub == "RADIOTECH")
                return request_radio_state({modem::RadioLteRequestType::get_radio_tech, 0U, 0U}).second;
            if (sub == "REGSTATUS")
                return request_radio_state({modem::RadioLteRequestType::get_reg_status, 0U, 0U}).second;
            if (sub == "REGINFO")
                return request_radio_state({modem::RadioLteRequestType::get_registration_info, 0U, 0U}).second;
            if (sub == "NETWORKINFO")
                return request_radio_state({modem::RadioLteRequestType::get_network_info, 0U, 0U}).second;
            if (sub == "SIGNALQUALITY")
                return request_radio_state({modem::RadioLteRequestType::get_signal_quality, 0U, 0U}).second;
            if (sub == "PSMMODE") return request_radio_state({modem::RadioLteRequestType::get_psm_mode, 0U, 0U}).second;
            if (sub == "CPSMSCONFIG")
                return request_radio_state({modem::RadioLteRequestType::get_cpsms_config, 0U, 0U}).second;
            if (sub == "TELITCPSMSCONFIG")
                return request_radio_state({modem::RadioLteRequestType::get_telit_cpsms_config, 0U, 0U}).second;
            if (sub == "TELITCPSMSSTATUS")
                return request_radio_state({modem::RadioLteRequestType::get_telit_cpsms_status, 0U, 0U}).second;
            if (sub == "SURVEYRESULT")
                return request_radio_state({modem::RadioLteRequestType::get_network_survey_result, 0U, 0U}).second;
            if (sub == "OPERATORLIST")
                return request_radio_state({modem::RadioLteRequestType::get_available_operators, 0U, 0U}).second;
            if (sub == "STATE") {
                return run_network_command_sync(
                    [&]() { return std::string("\"") + std::string(rpc::to_str(network.state())) + "\""; });
            }
            if (sub.rfind("SERVERINFO", 0) == 0) {
                std::string arg = sub.size() > 10 ? sub.substr(11) : "";
                if (!arg.empty()) {
                    int n = 0;
                    bool fValid = !arg.empty();
                    for (char c : arg) {
                        if (c < '0' || c > '9') {
                            fValid = false;
                            break;
                        }
                        n = (n * 10) + (c - '0');
                    }
                    if (!fValid) return "ERROR: invalid conn_id";
                    if (n >= 1 && n <= MAX_SERVER_CONNECTIONS) {
                        return request_radio_state(
                                   {modem::RadioLteRequestType::get_server_info_array, static_cast<uint32_t>(n), 0U})
                            .second;
                    }
                    return "ERROR: conn_id out of range (1-" + std::to_string(MAX_SERVER_CONNECTIONS) + ")";
                }
                return request_radio_state({modem::RadioLteRequestType::get_server_info_array, 0U, 0U}).second;
            }

            return "ERROR: unknown GET resource";
        }

        if (cmd == "SET") {
            auto sp2 = sub.find(' ');
            std::string res = sub.substr(0, sp2 == std::string::npos ? sub.size() : sp2);
            std::string args =
                sp2 == std::string::npos ? "" : sub_orig.substr(sp2 + 1); // preserve original case for values
            if (res == "NETWORKCONNECT" || res == "CONNECT") {
                auto [fOk, payload] = request_radio_state({modem::RadioLteRequestType::network_connect, 0U, 0U});
                if (!fOk) return payload;
                return std::string("{") + "\"resource\":\"NETWORKCONNECT\"," +
                       "\"network_connect\":" + payload + "}";
            }
            if (res == "NETWORKDISCONNECT" || res == "DISCONNECT") {
                int conn_id = lteConfig.conn_id;
                if (!args.empty()) {
                    bool fValid = true;
                    conn_id = 0;
                    for (char c : args) {
                        if (c < '0' || c > '9') {
                            fValid = false;
                            break;
                        }
                        conn_id = (conn_id * 10) + (c - '0');
                    }
                    if (!fValid) return "ERROR: invalid conn_id";
                    if (conn_id < 1 || conn_id > MAX_SERVER_CONNECTIONS) {
                        return "ERROR: conn_id out of range (1-" + std::to_string(MAX_SERVER_CONNECTIONS) + ")";
                    }
                }

                auto [server_ok, server_payload] = request_radio_state(
                    {modem::RadioLteRequestType::server_disconnect, static_cast<uint32_t>(conn_id), 0U});
                if (!server_ok) return server_payload;

                auto [network_ok, network_payload] =
                    request_radio_state({modem::RadioLteRequestType::network_disconnect, 0U, 0U});
                if (!network_ok) return network_payload;

                return std::string("{") + "\"resource\":\"NETWORKDISCONNECT\"," +
                       "\"conn_id\":" + std::to_string(conn_id) + "," +
                       "\"server_disconnect\":" + server_payload + "," +
                       "\"network_disconnect\":" + network_payload + "}";
            }
            if (res == "CONFIG") {
                modem::ModemSetConfigMsg cfg_req{};

                {
                    std::scoped_lock network_lock(network_mutex);
                    cfg_req.config = network.config();
                }

                if (!rpc::apply_config_fields(args, cfg_req.config)) {
                    return "ERROR: no valid fields provided (use key=value pairs)";
                }

                modem::MessageChannelError send_err = channels.send_request(cfg_req, 5000U);
                if (send_err != modem::MessageChannelError::ok) {
                    return "ERROR: failed to send set_config request";
                }

                uint32_t matched = channels.wait(modem::MODEM_EVT_RESPONSE, true, 5000U);
                if ((matched & modem::MODEM_EVT_RESPONSE) == 0U) {
                    return "ERROR: timeout waiting set_config response";
                }

                modem::ModemTypedResponseMsg<bool> set_resp{};
                if (channels.recv_typed_response(set_resp, 0) != modem::MessageChannelError::ok || !set_resp.ok ||
                    !set_resp.value) {
                    return "ERROR: no valid fields provided (use key=value pairs)";
                }

                auto [cfg_ok, cfg_payload] = request_radio_state({modem::RadioLteRequestType::get_config, 0U, 0U});
                if (!cfg_ok) return cfg_payload;
                return cfg_payload;
            }
            if (res == "FORCEPSM") {
                auto [ok, payload] = request_radio_state({modem::RadioLteRequestType::force_psm, 0U, 0U});
                if (!ok) return payload;
                return std::string("{") + "\"resource\":\"FORCEPSM\"," + "\"status\":" + payload + "}";
            }
            return "ERROR: unknown SET resource";
        }

        return "ERROR: unknown command — use GET <resource>, SET CONFIG <key>=<value>, or SET NETWORKDISCONNECT "
               "[conn_id]";
    };

    rpc_ipc.set_callback([&](const uint8_t* data, uint16_t len) {
        std::string req(reinterpret_cast<const char*>(data), len);
        MODEM_LOG_INF("RPC IPC >> %s", req.c_str());
        std::string resp = handle_rpc(req) + "\r\n";
        rpc_ipc.send(reinterpret_cast<const uint8_t*>(resp.data()), static_cast<uint16_t>(resp.size()));
    });
    if (!rpc_ipc.start()) {
        MODEM_LOG_WRN("RPC IPC server failed to start on port 9003 (continuing without it)");
    } else {
        MODEM_LOG_INF("RPC IPC server listening on localhost:9003 (RPC get/set)");
    }

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

    auto timer = modem::create_platform_timer();

    std::atomic_bool fForcePsmMode{false};
    timer->start(15000, [&]() {
        // fForcePsmMode = true;
    });

    network_worker_running.store(true);

    // Event consumer thread: waits for RadioLteChannels events and logs state
    // changes, responses, and log messages published by the network thread.
    std::thread event_thread([&]() {
        constexpr uint32_t ALL_EVENTS = modem::MODEM_EVT_STATE | modem::MODEM_EVT_LOG;
        while (true) {
            uint32_t matched = channels.wait(ALL_EVENTS, true, 1000);
            if (matched == 0) continue;

            if ((matched & modem::MODEM_EVT_STATE) != 0) {
                const auto& st = channels.current_state();
                /*
                MODEM_LOG_INF("Event thread: state=%u event=%u", static_cast<unsigned>(st.state),
                              static_cast<unsigned>(st.event));
                */
            }

            if ((matched & modem::MODEM_EVT_LOG) != 0) {
                modem::ModemLogMsg log_msg{};
                while (channels.recv_log(log_msg) == modem::MessageChannelError::ok) {
                    MODEM_LOG_INF("Event thread: log: %s", log_msg.text.c_str());
                }
            }
        }
    });

    std::thread network_thread([&]() {
        while (true) {
            std::deque<std::function<void()>> pending_commands;
            {
                std::scoped_lock command_queue_lock(command_queue_mutex);
                pending_commands.swap(command_queue);
            }

            {
                std::scoped_lock network_lock(network_mutex);
                for (const auto& command : pending_commands) {
                    command();
                }

                network.loop();

                // Process cross-thread requests via message channels
                modem::process_radio_requests(channels, network);

                // Publish current state via event flags
                channels.publish_state(network.state(), network.event());

                // Drain RX queue and forward to IPC client
                modem::QueueMessage rx_msg;
                while (network.rx_read(lteConfig.conn_id, rx_msg) == modem::QueueError::ok) {
                    std::string payload(rx_msg.data.begin(), rx_msg.data.end());
                    MODEM_LOG_INF("RX queue [conn %d]: %s (%zu bytes)", lteConfig.conn_id, payload.c_str(),
                                  rx_msg.data.size());
                    ipc.send(rx_msg.data.data(), static_cast<uint16_t>(rx_msg.data.size()));
                }

                if (fForcePsmMode.exchange(false)) {
                    MODEM_LOG_INF("Forcing PSM mode for testing purposes...");
                    network.force_psm();
                }
            }
            modem::delay_ms(10);
        }
    });

    event_thread.join();
    network_thread.join();
    return 0;
}
