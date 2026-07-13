#include "app_rpc_server.h"

#include "modem/log.h"
#include "rpc_helpers.h"

#include <algorithm>
#include <cctype>
#include <string_view>

MODEM_LOG_MODULE_REGISTER(app_rpc_server);

namespace app {

namespace {

constexpr uint32_t RPC_TIMEOUT_DEFAULT_MS = 5000U;
constexpr uint32_t RPC_TIMEOUT_CONNECT_MS = 210000U;
constexpr uint32_t RPC_TIMEOUT_DISCONNECT_MS = 60000U;
constexpr uint32_t RPC_TIMEOUT_FORCE_PSM_MS = 30000U;

std::string to_upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

std::string normalize_token(std::string_view token) {
    std::string out;
    out.reserve(token.size());
    for (char c : token) {
        if (c == '_' || c == '-') continue;
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string config_key_from_resource(std::string_view resource) {
    const std::string key = normalize_token(resource);
    if (key == "CID") return "cid";
    if (key == "ATTACHTIMEOUTSEC") return "attach_timeout_sec";
    if (key == "PDPTIMEOUTSEC") return "pdp_timeout_sec";
    if (key == "DATAREADYTIMEOUTSEC") return "data_ready_timeout_sec";
    if (key == "TRANSPARENTTIMEOUTSEC") return "transparent_timeout_sec";
    if (key == "MAXNETWORKATTEMPTS") return "max_network_attempts";
    if (key == "MAXATTACHRETRIES") return "max_attach_retries";
    if (key == "MAXPDPRETRIES") return "max_pdp_retries";
    if (key == "DEFAULTLTEBANDS") return "default_lte_bands";
    if (key == "DEFAULTIOTTECH") return "default_iot_tech";
    if (key == "DEFAULTAPN") return "default_apn";
    if (key == "FALLBACKLTEBANDS") return "fallback_lte_bands";
    if (key == "FALLBACKIOTTECH") return "fallback_iot_tech";
    if (key == "FALLBACKAPN") return "fallback_apn";
    if (key == "PLMN") return "plmn";
    if (key == "FPSMENABLE" || key == "PSMENABLE") return "fPsmEnable";
    if (key == "FCFUNSLEEP" || key == "CFUNSLEEP") return "fCfunSleep";
    if (key == "PSMT3412") return "psm_t3412";
    if (key == "PSMT3324") return "psm_t3324";
    if (key == "CONNID") return "conn_id";
    return "";
}

} // namespace

RpcServer::RpcServer(RpcServerContext context)
    : context_(std::move(context)), rpc_ipc_(9003, nullptr, IpcServer::Mode::line) {}

void RpcServer::start() {
    rpc_ipc_.set_callback([this](const uint8_t* data, uint16_t len) {
        std::string request(reinterpret_cast<const char*>(data), len);
        MODEM_LOG_INF("RPC IPC >> %s", request.c_str());
        std::string response = handle_request(request) + "\r\n";
        rpc_ipc_.send(reinterpret_cast<const uint8_t*>(response.data()), static_cast<uint16_t>(response.size()));
    });

    if (!rpc_ipc_.start()) {
        MODEM_LOG_WRN("RPC IPC server failed to start on port 9003 (continuing without it)");
    } else {
        MODEM_LOG_INF("RPC IPC server listening on localhost:9003 (RPC get/set)");
    }
}

std::pair<bool, std::string> RpcServer::request_radio_state(modem::RadioLteRequestMsg msg, uint32_t timeout_ms) {
    return request_radio_state_impl(msg, timeout_ms);
}

std::pair<bool, std::string> RpcServer::request_csurv_result(uint32_t timeout_ms) {
    modem::RadioLteRequestMsg msg{modem::RadioLteRequestType::get_csurv_result, 0U, 0U};
    modem::MessageChannelError send_err = context_.channels.send_request(msg, timeout_ms);
    if (send_err != modem::MessageChannelError::ok) {
        return {false, "ERROR: failed to send request"};
    }

    uint32_t matched = context_.channels.wait(modem::MODEM_EVT_RESPONSE, true, timeout_ms);
    if ((matched & modem::MODEM_EVT_RESPONSE) == 0U) {
        return {false, "ERROR: timeout waiting response"};
    }

    modem::ModemTypedResponseMsg<modem::CsurvResult> resp{};
    if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
        return {false, "ERROR: invalid csurv_result response"};
    }
    return {resp.ok, rpc::to_json(resp.value)};
}

std::pair<bool, std::string> RpcServer::request_radio_state_impl(modem::RadioLteRequestMsg msg, uint32_t timeout_ms) {
    if (!context_.network_worker_running.load()) {
        std::scoped_lock network_lock(context_.network_mutex);
        return {false, "ERROR: network worker not running"};
    }

    modem::MessageChannelError send_err = context_.channels.send_request(msg, timeout_ms);
    if (send_err != modem::MessageChannelError::ok) {
        return {false, "ERROR: failed to send request"};
    }

    uint32_t matched = context_.channels.wait(modem::MODEM_EVT_RESPONSE, true, timeout_ms);
    if ((matched & modem::MODEM_EVT_RESPONSE) == 0U) {
        return {false, "ERROR: timeout waiting response"};
    }

    switch (msg.type) {
        case modem::RadioLteRequestType::get_registration_info: {
            modem::ModemTypedResponseMsg<modem::RegistrationInfo> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid registration_info response"};
            }
            return {resp.ok, rpc::to_json(resp.value)};
        }
        case modem::RadioLteRequestType::get_signal_quality: {
            modem::ModemTypedResponseMsg<modem::SignalQuality> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid signal_quality response"};
            }
            return {resp.ok, rpc::to_json(resp.value)};
        }
        case modem::RadioLteRequestType::get_iccid: {
            modem::ModemTypedResponseMsg<modem::FixedString<modem::MODEM_SHORT_STR>> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid iccid response"};
            }
            return {resp.ok, "\"" + std::string(resp.value.c_str()) + "\""};
        }
        case modem::RadioLteRequestType::get_imsi: {
            modem::ModemTypedResponseMsg<modem::FixedString<modem::MODEM_SHORT_STR>> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid imsi response"};
            }
            return {resp.ok, "\"" + std::string(resp.value.c_str()) + "\""};
        }
        case modem::RadioLteRequestType::get_modem_info: {
            modem::ModemTypedResponseMsg<modem::ModemInfo> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid modem_info response"};
            }
            return {resp.ok, rpc::to_json(resp.value)};
        }
        case modem::RadioLteRequestType::get_sim_status: {
            modem::ModemTypedResponseMsg<modem::SimStatus> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid sim_status response"};
            }
            return {resp.ok, "\"" + std::string(rpc::to_str(resp.value)) + "\""};
        }
        case modem::RadioLteRequestType::get_radio_tech: {
            modem::ModemTypedResponseMsg<modem::RadioTech> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid radio_tech response"};
            }
            return {resp.ok, "\"" + std::string(rpc::to_str(resp.value)) + "\""};
        }
        case modem::RadioLteRequestType::get_reg_status: {
            modem::ModemTypedResponseMsg<modem::RegStatus> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid reg_status response"};
            }
            return {resp.ok, "\"" + std::string(rpc::to_str(resp.value)) + "\""};
        }
        case modem::RadioLteRequestType::get_network_info: {
            modem::ModemTypedResponseMsg<modem::NetworkInfo> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid network_info response"};
            }
            return {resp.ok, rpc::to_json(resp.value)};
        }
        case modem::RadioLteRequestType::get_psm_mode: {
            modem::ModemTypedResponseMsg<modem::PsmMode> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid psm_mode response"};
            }
            return {resp.ok, "\"" + std::string(rpc::to_str(resp.value)) + "\""};
        }
        case modem::RadioLteRequestType::get_cpsms_config: {
            modem::ModemTypedResponseMsg<modem::CpsmsConfig> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid cpsms_config response"};
            }
            return {resp.ok, rpc::to_json(resp.value)};
        }
        case modem::RadioLteRequestType::get_telit_cpsms_config: {
            modem::ModemTypedResponseMsg<modem::TelitCpsmsConfig> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid telit_cpsms_config response"};
            }
            return {resp.ok, rpc::to_json(resp.value)};
        }
        case modem::RadioLteRequestType::get_telit_cpsms_status: {
            modem::ModemTypedResponseMsg<modem::TelitCpsmsStatus> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid telit_cpsms_status response"};
            }
            return {resp.ok, rpc::to_json(resp.value)};
        }
        case modem::RadioLteRequestType::get_network_survey_result: {
            modem::ModemTypedResponseMsg<modem::NetworkSurveyResult> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid network_survey_result response"};
            }
            return {resp.ok, rpc::to_json(resp.value)};
        }
        case modem::RadioLteRequestType::get_available_operators: {
            modem::ModemTypedResponseMsg<modem::StaticVector<modem::Operator, modem::xE310::MAX_OPERATORS>> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid available_operators response"};
            }
            return {resp.ok, rpc::to_json(resp.value)};
        }
        case modem::RadioLteRequestType::get_csurv_result: {
            modem::ModemTypedResponseMsg<modem::CsurvResult> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid csurv_result response"};
            }
            return {resp.ok, rpc::to_json(resp.value)};
        }
        case modem::RadioLteRequestType::scan_networks: {
            modem::ModemTypedResponseMsg<bool> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid scan_networks response"};
            }
            if (!resp.ok || !resp.value) {
                return {false, "ERROR: scan_networks failed"};
            }
            return request_csurv_result(timeout_ms);
        }
        case modem::RadioLteRequestType::get_server_info_array: {
            modem::ModemTypedResponseMsg<const modem::ServerInfo*> resp{};
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
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
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
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
            if (context_.channels.recv_typed_response(resp, 0) != modem::MessageChannelError::ok) {
                return {false, "ERROR: invalid response"};
            }
            return {resp.ok && resp.value, resp.value ? "true" : "false"};
        }
    }
}

std::string RpcServer::handle_request(const std::string& request) {
    auto sp = request.find(' ');
    std::string cmd = to_upper(sp == std::string::npos ? request : request.substr(0, sp));
    std::string sub_orig = sp == std::string::npos ? "" : request.substr(sp + 1);
    std::string sub = to_upper(sub_orig);

    if (cmd == "GET") {
        if (sub == "SCANSURVEY") {
            auto [ok, payload] =
                request_radio_state({modem::RadioLteRequestType::scan_networks, 0U, 0U}, static_cast<uint32_t>(210000));
            if (!ok) return payload;
            return payload;
        }
        if (sub == "CONFIG")
            return request_radio_state({modem::RadioLteRequestType::get_config, 0U, 0U}, RPC_TIMEOUT_DEFAULT_MS).second;
        if (sub == "MODEMINFO")
            return request_radio_state({modem::RadioLteRequestType::get_modem_info, 0U, 0U}, RPC_TIMEOUT_DEFAULT_MS)
                .second;
        if (sub == "SIMSTATUS")
            return request_radio_state({modem::RadioLteRequestType::get_sim_status, 0U, 0U}, RPC_TIMEOUT_DEFAULT_MS)
                .second;
        if (sub == "RADIOTECH")
            return request_radio_state({modem::RadioLteRequestType::get_radio_tech, 0U, 0U}, RPC_TIMEOUT_DEFAULT_MS)
                .second;
        if (sub == "REGSTATUS")
            return request_radio_state({modem::RadioLteRequestType::get_reg_status, 0U, 0U}, RPC_TIMEOUT_DEFAULT_MS)
                .second;
        if (sub == "REGINFO") {
            return request_radio_state({modem::RadioLteRequestType::get_registration_info, 0U, 0U},
                                       RPC_TIMEOUT_DEFAULT_MS)
                .second;
        }
        if (sub == "NETWORKINFO") {
            return request_radio_state({modem::RadioLteRequestType::get_network_info, 0U, 0U}, RPC_TIMEOUT_DEFAULT_MS)
                .second;
        }
        if (sub == "SIGNALQUALITY") {
            return request_radio_state({modem::RadioLteRequestType::get_signal_quality, 0U, 0U}, RPC_TIMEOUT_DEFAULT_MS)
                .second;
        }
        if (sub == "PSMMODE")
            return request_radio_state({modem::RadioLteRequestType::get_psm_mode, 0U, 0U}, RPC_TIMEOUT_DEFAULT_MS)
                .second;
        if (sub == "CPSMSCONFIG") {
            return request_radio_state({modem::RadioLteRequestType::get_cpsms_config, 0U, 0U}, RPC_TIMEOUT_DEFAULT_MS)
                .second;
        }
        if (sub == "TELITCPSMSCONFIG") {
            return request_radio_state({modem::RadioLteRequestType::get_telit_cpsms_config, 0U, 0U},
                                       RPC_TIMEOUT_DEFAULT_MS)
                .second;
        }
        if (sub == "TELITCPSMSSTATUS") {
            return request_radio_state({modem::RadioLteRequestType::get_telit_cpsms_status, 0U, 0U},
                                       RPC_TIMEOUT_DEFAULT_MS)
                .second;
        }
        if (sub == "SURVEYRESULT") {
            return request_radio_state({modem::RadioLteRequestType::get_network_survey_result, 0U, 0U},
                                       RPC_TIMEOUT_DEFAULT_MS)
                .second;
        }
        if (sub == "OPERATORLIST") {
            return request_radio_state({modem::RadioLteRequestType::get_available_operators, 0U, 0U},
                                       RPC_TIMEOUT_DEFAULT_MS)
                .second;
        }
        if (sub == "STATE") {
            return context_.get_network_state();
        }
        if (sub.rfind("SERVERINFO", 0) == 0) {
            std::string arg = sub.size() > 10 ? sub.substr(11) : "";
            if (!arg.empty()) {
                int n = 0;
                bool is_valid = true;
                for (char c : arg) {
                    if (c < '0' || c > '9') {
                        is_valid = false;
                        break;
                    }
                    n = (n * 10) + (c - '0');
                }
                if (!is_valid) return "ERROR: invalid conn_id";
                if (n >= 1 && n <= MAX_SERVER_CONNECTIONS) {
                    return request_radio_state(
                               {modem::RadioLteRequestType::get_server_info_array, static_cast<uint32_t>(n), 0U},
                               RPC_TIMEOUT_DEFAULT_MS)
                        .second;
                }
                return "ERROR: conn_id out of range (1-" + std::to_string(MAX_SERVER_CONNECTIONS) + ")";
            }
            return request_radio_state({modem::RadioLteRequestType::get_server_info_array, 0U, 0U},
                                       RPC_TIMEOUT_DEFAULT_MS)
                .second;
        }

        return "ERROR: unknown GET resource";
    }

    if (cmd == "SET") {
        auto sp2 = sub.find(' ');
        std::string res = sub.substr(0, sp2 == std::string::npos ? sub.size() : sp2);
        std::string args = sp2 == std::string::npos ? "" : sub_orig.substr(sp2 + 1);

        auto apply_config_update = [&](const std::string& update_expr) -> std::string {
            modem::ModemSetConfigMsg cfg_request{};
            {
                std::scoped_lock network_lock(context_.network_mutex);
                cfg_request.config = context_.network.config();
            }

            if (!rpc::apply_config_fields(update_expr, cfg_request.config)) {
                return "ERROR: no valid fields provided (use key=value pairs)";
            }

            modem::MessageChannelError send_err = context_.channels.send_request(cfg_request, 5000U);
            if (send_err != modem::MessageChannelError::ok) {
                return "ERROR: failed to send set_config request";
            }

            uint32_t matched = context_.channels.wait(modem::MODEM_EVT_RESPONSE, true, 5000U);
            if ((matched & modem::MODEM_EVT_RESPONSE) == 0U) {
                return "ERROR: timeout waiting set_config response";
            }

            modem::ModemTypedResponseMsg<bool> set_response{};
            if (context_.channels.recv_typed_response(set_response, 0) != modem::MessageChannelError::ok ||
                !set_response.ok || !set_response.value) {
                return "ERROR: no valid fields provided (use key=value pairs)";
            }

            auto [cfg_ok, cfg_payload] =
                request_radio_state({modem::RadioLteRequestType::get_config, 0U, 0U}, RPC_TIMEOUT_DEFAULT_MS);
            if (!cfg_ok) return cfg_payload;
            return cfg_payload;
        };

        if (res == "NETWORKCONNECT" || res == "CONNECT") {
            auto [ok, payload] =
                request_radio_state({modem::RadioLteRequestType::network_connect, 0U, 0U}, RPC_TIMEOUT_CONNECT_MS);
            if (!ok) return payload;
            return std::string("{") + "\"resource\":\"NETWORKCONNECT\"," + "\"network_connect\":" + payload + "}";
        }

        if (res == "NETWORKDISCONNECT" || res == "DISCONNECT") {
            int conn_id = context_.lte_config.conn_id;
            if (!args.empty()) {
                bool is_valid = true;
                conn_id = 0;
                for (char c : args) {
                    if (c < '0' || c > '9') {
                        is_valid = false;
                        break;
                    }
                    conn_id = (conn_id * 10) + (c - '0');
                }
                if (!is_valid) return "ERROR: invalid conn_id";
                if (conn_id < 1 || conn_id > MAX_SERVER_CONNECTIONS) {
                    return "ERROR: conn_id out of range (1-" + std::to_string(MAX_SERVER_CONNECTIONS) + ")";
                }
            }

            auto [server_ok, server_payload] =
                request_radio_state({modem::RadioLteRequestType::server_disconnect, static_cast<uint32_t>(conn_id), 0U},
                                    RPC_TIMEOUT_DISCONNECT_MS);
            if (!server_ok) return server_payload;

            auto [network_ok, network_payload] = request_radio_state(
                {modem::RadioLteRequestType::network_disconnect, 0U, 0U}, RPC_TIMEOUT_DISCONNECT_MS);
            if (!network_ok) return network_payload;

            return std::string("{") + "\"resource\":\"NETWORKDISCONNECT\"," + "\"conn_id\":" + std::to_string(conn_id) +
                   "," + "\"server_disconnect\":" + server_payload + "," + "\"network_disconnect\":" + network_payload +
                   "}";
        }

        if (res == "CONFIG") {
            return apply_config_update(args);
        }

        const std::string config_key = config_key_from_resource(res);
        if (!config_key.empty()) {
            if (args.empty()) {
                return "ERROR: missing value";
            }
            return apply_config_update(config_key + "=" + args);
        }

        if (res == "FORCEPSM") {
            auto [ok, payload] =
                request_radio_state({modem::RadioLteRequestType::force_psm, 0U, 0U}, RPC_TIMEOUT_FORCE_PSM_MS);
            if (!ok) return payload;
            return std::string("{") + "\"resource\":\"FORCEPSM\"," + "\"status\":" + payload + "}";
        }

        return "ERROR: unknown SET resource";
    }

    return "ERROR: unknown command - use GET <resource>, SET CONFIG <key>=<value>, or SET NETWORKDISCONNECT [conn_id]";
}

} // namespace app