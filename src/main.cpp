#include "modem/network_lte.h"
#include "modem/xe310.h"
#include "modem/modem_controller.h"
#include "modem/uart_factory.h"
#include "modem/message_queue_interface.h"
#include "modem/log.h"
#include "ipc_server.h"
#include "rpc_helpers.h"
#include "modem/timer_interface.h"
#include "modem/timer_factory.h"
#include <memory>
#include <algorithm>
#include <atomic>
#include <condition_variable>
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
    std::atomic_bool network_worker_running{false};
    std::mutex command_queue_mutex;
    std::deque<std::function<void()>> command_queue;
    struct RadioStateRequest {
        modem::RadioLteRequestMsg msg;
        std::mutex mutex;
        std::condition_variable event;
        bool done = false;
        bool ok = false;
        std::string payload;
    };
    std::mutex radio_state_queue_mutex;
    std::deque<std::shared_ptr<RadioStateRequest>> radio_state_queue;

    auto enqueue_network_command = [&](std::function<void()> command) {
        std::scoped_lock command_queue_lock(command_queue_mutex);
        command_queue.emplace_back(std::move(command));
    };

    auto process_radio_state_request = [&](const modem::RadioLteRequestMsg& req) -> std::pair<bool, std::string> {
        switch (req.type) {
            case modem::RadioLteRequestType::get_registration_info:
                return {true, rpc::to_json(network.registration_info())};
            case modem::RadioLteRequestType::get_signal_quality: return {true, rpc::to_json(network.signal_quality())};
            case modem::RadioLteRequestType::get_iccid:
                return {true, "\"" + std::string(network.iccid().c_str()) + "\""};
            case modem::RadioLteRequestType::get_imsi: return {true, "\"" + std::string(network.imsi().c_str()) + "\""};
            case modem::RadioLteRequestType::get_modem_info: return {true, rpc::to_json(network.modem_info())};
            case modem::RadioLteRequestType::get_sim_status:
                return {true, "\"" + std::string(rpc::to_str(network.sim_status())) + "\""};
            case modem::RadioLteRequestType::get_radio_tech:
                return {true, "\"" + std::string(rpc::to_str(network.radio_tech())) + "\""};
            case modem::RadioLteRequestType::get_reg_status:
                return {true, "\"" + std::string(rpc::to_str(network.reg_status())) + "\""};
            case modem::RadioLteRequestType::get_network_info: return {true, rpc::to_json(network.network_info())};
            case modem::RadioLteRequestType::get_psm_mode:
                return {true, "\"" + std::string(rpc::to_str(network.psm_mode())) + "\""};
            case modem::RadioLteRequestType::get_cpsms_config: return {true, rpc::to_json(network.cpsms_config())};
            case modem::RadioLteRequestType::get_telit_cpsms_config:
                return {true, rpc::to_json(network.telit_cpsms_config())};
            case modem::RadioLteRequestType::get_telit_cpsms_status:
                return {true, rpc::to_json(network.telit_cpsms_status())};
            case modem::RadioLteRequestType::get_network_survey_result:
                return {true, rpc::to_json(network.network_survey_result())};
            case modem::RadioLteRequestType::get_available_operators:
                return {true, rpc::to_json(network.available_operators())};
            case modem::RadioLteRequestType::get_csurv_result: return {true, rpc::to_json(network.csurv_result())};
            case modem::RadioLteRequestType::scan_networks:
                network.scan_networks(req.arg0, req.arg1);
                return {true, rpc::to_json(network.csurv_result())};
            case modem::RadioLteRequestType::get_server_info_array: {
                const auto* arr = network.server_info_array();
                if (req.arg0 >= 1 && req.arg0 <= MAX_SERVER_CONNECTIONS) {
                    return {true, rpc::to_json(arr[req.arg0 - 1])};
                }
                std::string out = "[";
                for (int i = 0; i < MAX_SERVER_CONNECTIONS; ++i) {
                    if (i > 0) out += ',';
                    out += rpc::to_json(arr[i]);
                }
                out += "]";
                return {true, out};
            }
            case modem::RadioLteRequestType::get_config: return {true, rpc::config_to_json(network.config())};
            default: return {false, "ERROR: unsupported request"};
        }
    };

    auto request_radio_state = [&](modem::RadioLteRequestMsg msg,
                                   uint32_t timeout_ms = 5000U) -> std::pair<bool, std::string> {
        if (!network_worker_running.load()) {
            std::scoped_lock network_lock(network_mutex);
            return process_radio_state_request(msg);
        }

        auto request = std::make_shared<RadioStateRequest>();
        request->msg = msg;
        {
            std::scoped_lock queue_lock(radio_state_queue_mutex);
            radio_state_queue.emplace_back(request);
        }

        std::unique_lock<std::mutex> req_lock(request->mutex);
        bool signaled =
            request->event.wait_for(req_lock, std::chrono::milliseconds(timeout_ms), [&]() { return request->done; });
        if (!signaled) return {false, "ERROR: network thread timeout"};
        return {request->ok, request->payload};
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
            if (sub == "ALL") {
                auto config = request_radio_state({modem::RadioLteRequestType::get_config, 0U, 0U}).second;
                auto modem_info = request_radio_state({modem::RadioLteRequestType::get_modem_info, 0U, 0U}).second;
                auto sim_status = request_radio_state({modem::RadioLteRequestType::get_sim_status, 0U, 0U}).second;
                auto radio_tech = request_radio_state({modem::RadioLteRequestType::get_radio_tech, 0U, 0U}).second;
                auto reg_status = request_radio_state({modem::RadioLteRequestType::get_reg_status, 0U, 0U}).second;
                auto reg_info = request_radio_state({modem::RadioLteRequestType::get_registration_info, 0U, 0U}).second;
                auto network_info = request_radio_state({modem::RadioLteRequestType::get_network_info, 0U, 0U}).second;
                auto signal_quality =
                    request_radio_state({modem::RadioLteRequestType::get_signal_quality, 0U, 0U}).second;
                auto psm_mode = request_radio_state({modem::RadioLteRequestType::get_psm_mode, 0U, 0U}).second;
                auto cpsms_config = request_radio_state({modem::RadioLteRequestType::get_cpsms_config, 0U, 0U}).second;
                auto telit_cpsms_config =
                    request_radio_state({modem::RadioLteRequestType::get_telit_cpsms_config, 0U, 0U}).second;
                auto telit_cpsms_status =
                    request_radio_state({modem::RadioLteRequestType::get_telit_cpsms_status, 0U, 0U}).second;
                auto survey_result =
                    request_radio_state({modem::RadioLteRequestType::get_network_survey_result, 0U, 0U}).second;
                auto operator_list =
                    request_radio_state({modem::RadioLteRequestType::get_available_operators, 0U, 0U}).second;
                auto server_info =
                    request_radio_state({modem::RadioLteRequestType::get_server_info_array, 0U, 0U}).second;
                auto state =
                    run_network_command_sync([&]() { return std::string("\"") + rpc::to_str(network.state()) + "\""; });

                return std::string("{") + "\"config\":" + config + "," + "\"state\":" + state + "," +
                       "\"modem_info\":" + modem_info + "," + "\"sim_status\":" + sim_status + "," +
                       "\"radio_tech\":" + radio_tech + "," + "\"reg_status\":" + reg_status + "," +
                       "\"reg_info\":" + reg_info + "," + "\"network_info\":" + network_info + "," +
                       "\"signal_quality\":" + signal_quality + "," + "\"psm_mode\":" + psm_mode + "," +
                       "\"cpsms_config\":" + cpsms_config + "," + "\"telit_cpsms_config\":" + telit_cpsms_config + "," +
                       "\"telit_cpsms_status\":" + telit_cpsms_status + "," + "\"survey_result\":" + survey_result +
                       "," + "\"operator_list\":" + operator_list + "," + "\"server_info\":" + server_info + "}";
            }
            return "ERROR: unknown GET resource";
        }

        if (cmd == "SET") {
            auto sp2 = sub.find(' ');
            std::string res = sub.substr(0, sp2 == std::string::npos ? sub.size() : sp2);
            std::string args =
                sp2 == std::string::npos ? "" : sub_orig.substr(sp2 + 1); // preserve original case for values
            if (res == "NETWORKCONNECT" || res == "CONNECT") {
                bool fOk = run_network_command_sync([&]() { return network.network_connect(); });
                return std::string("{") + "\"resource\":\"NETWORKCONNECT\"," +
                       "\"network_connect\":" + (fOk ? "true" : "false") + "}";
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

                return run_network_command_sync([&, conn_id]() {
                    bool fServerOk = network.server_disconnect(static_cast<uint8_t>(conn_id));
                    bool fNetworkOk = network.network_disconnect();

                    return std::string("{") + "\"resource\":\"NETWORKDISCONNECT\"," +
                           "\"conn_id\":" + std::to_string(conn_id) + "," +
                           "\"server_disconnect\":" + (fServerOk ? "true" : "false") + "," +
                           "\"network_disconnect\":" + (fNetworkOk ? "true" : "false") + "}";
                });
            }
            if (res == "CONFIG") {
                return run_network_command_sync([&, args]() {
                    auto cfg = network.config();
                    if (rpc::apply_config_fields(args, cfg)) {
                        network.set_config(cfg);
                        return rpc::config_to_json(network.config());
                    }
                    return std::string("ERROR: no valid fields provided (use key=value pairs)");
                });
            }
            if (res == "FORCEPSM") {
                run_network_command_sync([&]() {
                    network.force_psm();
                    return true;
                });
                return "{\"resource\":\"FORCEPSM\",\"status\":\"ok\"}";
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
    std::thread network_thread([&]() {
        while (true) {
            std::deque<std::function<void()>> pending_commands;
            std::deque<std::shared_ptr<RadioStateRequest>> pending_radio_requests;
            {
                std::scoped_lock command_queue_lock(command_queue_mutex);
                pending_commands.swap(command_queue);
            }
            {
                std::scoped_lock radio_state_lock(radio_state_queue_mutex);
                pending_radio_requests.swap(radio_state_queue);
            }

            {
                std::scoped_lock network_lock(network_mutex);
                for (const auto& command : pending_commands) {
                    command();
                }
                for (const auto& request : pending_radio_requests) {
                    auto [ok, payload] = process_radio_state_request(request->msg);
                    {
                        std::scoped_lock req_lock(request->mutex);
                        request->ok = ok;
                        request->payload = std::move(payload);
                        request->done = true;
                    }
                    request->event.notify_one();
                }

                network.loop();

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

    network_thread.join();
    return 0;
}
