#include "modem/network_lte.h"
#include "modem/log.h"
#include "modem/timer_factory.h"
#include "modem/message_queue_factory.h"
#include <algorithm>
#include <chrono>
#include <string>

namespace modem {

// Forward declarations of static helpers defined at bottom of file
static const char* action_to_str(ModemAction a);
static const char* state_to_str(NetworkLteState s);

static bool is_error_event(NetworkLteEvent event) {
    switch (event) {
        case NetworkLteEvent::network_error:
        case NetworkLteEvent::attach_error:
        case NetworkLteEvent::context_error:
        case NetworkLteEvent::at_command_no_response: return true;
        default: return false;
    }
}

NetworkLte::NetworkLte(xE310& modem, const NetworkLteConfig& config, DataReceivedCallback on_data_received,
                       TimerHandle timer)
    : modem_(modem),
      lteConfig(config),
      on_data_received_(std::move(on_data_received)),
      timer_(std::move(timer)),
      st_timer(modem::create_platform_timer()),
      message_queue_(modem::create_platform_message_queue()) {}

// --- Accessors ---
const RegistrationInfo& NetworkLte::registration_info() const {
    return regInfo;
}
const SignalQuality& NetworkLte::signal_quality() const {
    return signalQuality;
}
const FixedString<MODEM_SHORT_STR>& NetworkLte::iccid() const {
    return modemInfo.iccid;
}
const FixedString<MODEM_SHORT_STR>& NetworkLte::imsi() const {
    return modemInfo.imsi;
}
const FixedString<MODEM_SHORT_STR>& NetworkLte::clock() const {
    return modemClock;
}
const NetworkLteConfig& NetworkLte::config() const {
    return lteConfig;
}
void NetworkLte::set_config(const NetworkLteConfig& config) {
    lteConfig = config;
}

const ModemInfo& NetworkLte::modem_info() const {
    return modemInfo;
}
SimStatus NetworkLte::sim_status() const {
    return simStatus;
}
RadioTech NetworkLte::radio_tech() const {
    return radioTech;
}
RegStatus NetworkLte::reg_status() const {
    return regStatus;
}
const NetworkInfo& NetworkLte::network_info() const {
    return networkInfo;
}
PsmMode NetworkLte::psm_mode() const {
    return psmMode;
}
const CpsmsConfig& NetworkLte::cpsms_config() const {
    return cpsmsConfig;
}
const TelitCpsmsConfig& NetworkLte::telit_cpsms_config() const {
    return telitCpsmsConfig;
}
const TelitCpsmsStatus& NetworkLte::telit_cpsms_status() const {
    return telitCpsmsStatus;
}
const NetworkSurveyResult& NetworkLte::network_survey_result() const {
    return networkSurveyResult;
}
const StaticVector<Operator, xE310::MAX_OPERATORS>& NetworkLte::available_operators() const {
    return operatorList;
}
const CsurvResult& NetworkLte::csurv_result() const {
    return csurvResult;
}
const ServerInfo* NetworkLte::server_info_array() const {
    return serverInfo;
}

bool NetworkLte::scan_networks(uint32_t start_ch, uint32_t end_ch) {
    auto status = modem_.scan_networks(csurvResult, start_ch, end_ch);
    return status == ModemStatus::ok;
}

NetworkLteState NetworkLte::state() const {
    return state_;
}
NetworkLteEvent NetworkLte::event() const {
    return event_;
}

uint8_t NetworkLte::get_network_attempts() const {
    return nNetworkAttempts;
}
void NetworkLte::set_network_attempts(uint8_t n) {
    nNetworkAttempts = n;
}
uint8_t NetworkLte::get_attach_retries() const {
    return nAttachRetries;
}
uint8_t NetworkLte::get_pdp_retries() const {
    return nPdpRetries;
}
void NetworkLte::set_attach_retries(uint8_t n) {
    nAttachRetries = n;
}
void NetworkLte::set_pdp_retries(uint8_t n) {
    nPdpRetries = n;
}

bool NetworkLte::network_connect() {
    if (fWarmBoot) modem_.network_detach();

    // States that cannot transition to data_ready without explicit user action:
    // return false immediately to avoid an infinite loop inside go_to_state().
    if (state_ == NetworkLteState::transparent_mode) {
        NETWORK_LOG_ERR("Cannot connect to network while in transparent mode, exit transparent mode first");
        return false;
    }
    if (state_ == NetworkLteState::modem_fota) {
        NETWORK_LOG_ERR("Cannot connect to network while a FOTA update is in progress");
        return false;
    }
    if (state_ == NetworkLteState::done) {
        NETWORK_LOG_ERR("Cannot connect to network from done state (max retries reached), reset the modem first");
        return false;
    }

    if (state_ == NetworkLteState::data_ready) {
        NETWORK_LOG_INF("Already connected to network");
        return true; // already connected to network
    }
    go_to_state(NetworkLteState::data_ready); // trigger attach flow in idle mode
    if (state_ == NetworkLteState::data_ready) {
        NETWORK_LOG_INF("Successfully connected to network");
        return true;
    } else {
        NETWORK_LOG_ERR("Failed to connect to network");
        return false;
    }
}

bool NetworkLte::network_disconnect() {

    if (true || state_ == NetworkLteState::data_ready) { // NOLINT(readability-simplify-boolean-expr)
        auto status = modem_.network_detach();
        if (status == ModemStatus::ok)
            change_state(NetworkLteState::network_detached);
        else
            NETWORK_LOG_ERR("Failed to detach from network");
        return true; // already connected to network
    } else {
        NETWORK_LOG_ERR("Not currently connected to network, cannot disconnect");
        return true;
    }
}

void NetworkLte::new_connection(uint8_t conn_id, std::string_view protocol, std::string_view ip,
                                std::string_view port) {
    serverInfo[conn_id - 1].state = ServerState::disconnected;
    serverInfo[conn_id - 1].protocol = protocol;
    serverInfo[conn_id - 1].address = ip;
    char port_buf[8] = {};
    auto n = port.size() < sizeof(port_buf) - 1 ? port.size() : sizeof(port_buf) - 1;
    std::memcpy(port_buf, port.data(), n);
    serverInfo[conn_id - 1].port = static_cast<uint16_t>(std::atoi(port_buf));
}

bool NetworkLte::server_connect(uint8_t conn_id, std::string_view protocol, std::string_view ip, uint16_t port) {

    if (state_ != NetworkLteState::data_ready && state_ != NetworkLteState::sleep_mode) {
        NETWORK_LOG_INF("Not currently in data ready mode or PSM, connect to the network first");
        return false; // not connected to network, cannot connect to server
    }

    NETWORK_LOG_DBG("Attempting to connect to server with CID %d, protocol %.*s, IP %.*s, port %d", conn_id,
                    static_cast<int>(protocol.size()), protocol.data(), static_cast<int>(ip.size()), ip.data(), port);
    if (state_ == NetworkLteState::data_ready) {
        uint8_t conn_state = 0;
        NETWORK_LOG_DBG("Checking current connection state for CID %d before connecting to server", conn_id);
        modem_.udp_status(conn_id, conn_state);
        serverInfo[conn_id - 1].state = static_cast<ServerState>(conn_state);
        if (serverInfo[conn_id - 1].state == ServerState::connected) {
            NETWORK_LOG_INF(
                "Already connected to a server, cannot connect to a different one without disconnecting first");
            return true; // already connected to a server, cannot connect to a different one without disconnecting first
        }
        if (serverInfo[conn_id - 1].state == ServerState::suspended) {
            NETWORK_LOG_INF("Resuming suspended connection to server at %.*s:%d", static_cast<int>(ip.size()),
                            ip.data(), port);
            auto status = modem_.udp_close(conn_id);
            if (status != ModemStatus::ok) {
                NETWORK_LOG_ERR("Failed to close connection to server at %.*s:%d", static_cast<int>(ip.size()),
                                ip.data(), port);
                return false;
            } else {
                serverInfo[conn_id - 1].state = ServerState::connected;
                NETWORK_LOG_INF("Successfully closed connection to server at %.*s:%d", static_cast<int>(ip.size()),
                                ip.data(), port);
            }
        }
        /*
        if( serverInfo[conn_id-1].state == ServerState::pending_data){
            // read data
        }
        if( serverInfo[conn_id-1].state == ServerState::listening){
            // what to do??
        }
        if( serverInfo[conn_id-1].state == ServerState::incoming_connection){
            // accept connection
        }
        */
        if (serverInfo[conn_id - 1].state == ServerState::dns_resolving ||
            serverInfo[conn_id - 1].state == ServerState::connecting) {
            NETWORK_LOG_ERR("Already in the process of connecting to a server, cannot connect to a different one "
                            "without disconnecting first");
            return false; // already in the process of connecting to a server, cannot connect to a different one without
                          // disconnecting first
        }

        if (protocol == "UDP") {
            NETWORK_LOG_INF("Connecting to UDP server at %.*s:%d", static_cast<int>(ip.size()), ip.data(), port);
            auto status = modem_.udp_open(conn_id, ip, port);

            if (status != ModemStatus::ok) {
                modem_.udp_close(conn_id); // ensure we close any half-open connection
                NETWORK_LOG_ERR("Failed to connect to server at %.*s:%d", static_cast<int>(ip.size()), ip.data(), port);
                return false;
            } else {
                serverInfo[conn_id - 1].state = ServerState::connected;
                serverInfo[conn_id - 1].protocol = protocol;
                serverInfo[conn_id - 1].address = ip;
                serverInfo[conn_id - 1].port = port;
                NETWORK_LOG_INF("Successfully connected to server at %.*s:%d", static_cast<int>(ip.size()), ip.data(),
                                port);
                return true;
            }
        } else if (protocol == "TCP") {
            NETWORK_LOG_WRN("TCP protocol is not currently supported, cannot connect to server at %.*s:%d",
                            static_cast<int>(ip.size()), ip.data(), port);
            return false; // TCP not supported, cannot connect
        } else {
            NETWORK_LOG_ERR("Unknown protocol for server connection, cannot connect to server at %.*s:%d",
                            static_cast<int>(ip.size()), ip.data(), port);
            return false; // unknown protocol, cannot connect
        }
    } else {
        NETWORK_LOG_ERR("Failed to connect to network, cannot connect to server");
        return false;
    }
}

bool NetworkLte::server_disconnect(uint8_t conn_id) {
    if (serverInfo[conn_id - 1].state != ServerState::connected) {
        NETWORK_LOG_ERR("Not currently connected to a server, cannot disconnect");
        return false; // not connected to server, cannot disconnect
    }
    if (serverInfo[conn_id - 1].protocol == "UDP") {
        NETWORK_LOG_INF("Attempting to disconnect from UDP server at %s:%d", serverInfo[conn_id - 1].address.c_str(),
                        serverInfo[conn_id - 1].port);
        auto status = modem_.udp_close(conn_id);
        if (status != ModemStatus::ok) {
            NETWORK_LOG_ERR("Failed to disconnect from UDP server for CID %d", conn_id);
            return false;
        } else {
            serverInfo[conn_id - 1].state = ServerState::disconnected;
            NETWORK_LOG_INF("Successfully disconnected from UDP server for CID %d", conn_id);
            return true;
        }
    } else if (serverInfo[conn_id - 1].protocol == "TCP") {
        NETWORK_LOG_WRN("TCP protocol is not currently supported, cannot disconnect from server at %s:%d",
                        serverInfo[conn_id - 1].address.c_str(), serverInfo[conn_id - 1].port);
        return false; // TCP not supported, cannot disconnect
    } else {
        NETWORK_LOG_ERR("Unknown protocol for server connection, cannot disconnect");
        return false;
    }
}

QueueError NetworkLte::tx_write(uint8_t conn_id, const uint8_t* data, size_t length) {
    if (!message_queue_) return QueueError::invalid_id;
    return message_queue_->tx_push(conn_id, data, length);
}

QueueError NetworkLte::rx_read(uint8_t conn_id, QueueMessage& msg) {
    if (!message_queue_) return QueueError::invalid_id;
    return message_queue_->rx_pop(conn_id, msg);
}

bool NetworkLte::force_psm() {

    if (state_ != NetworkLteState::data_ready) {
        NETWORK_LOG_INF("Currently not in data ready state, cannot force PSM mode");
        return false;
    }

    // stop timers and URCs to avoid interference with PSM flow
    if (st_timer) {
        st_timer->stop();
    }

    modem_.set_psm_urc(false);          // enable PSM URCs in normal mode
    modem_.set_registration_urc(false); // enable registration URCs in normal mode
    modem_.set_pdp_urc(false);          // enable PDP URCs in normal mode
    modem_.set_plmnsearchexh_notify(false);

    bool fPsmModeAttached = false;
    // It will try to connect to each availale operator and enter PSM mode, if the modem and network support it. It will
    // stay in PSM mode for a short time and then exit, to demonstrate the PSM enter and exit flows and events. This is
    // meant to be used for testing purposes, to force the modem into PSM mode and trigger the corresponding events.
    StaticVector<Operator, xE310::MAX_OPERATORS> availableOperators;
    auto status = modem_.get_available_operators(availableOperators); // NOLINT(clang-analyzer-deadcode.DeadStores)
    for (const auto& op : availableOperators) {
        NETWORK_LOG_INF("Trying to register to operator %s with radio tech %d to force PSM mode", op.long_name.c_str(),
                        static_cast<int>(op.act));
        if (op.act == RadioTech::cat_m1 || op.act == RadioTech::nb_iot)
            NETWORK_LOG_INF("Operator %s supports tech with PSM, trying to register to it", op.long_name.c_str());
        modem_.set_operator_manual(op.numeric, op.act);
        RegistrationInfo reg_info;
        RadioTech tech = RadioTech::unknown;
        modem_.get_registration_status(reg_info, tech);
        if (reg_info.stat == RegStatus::registered_home || reg_info.stat == RegStatus::registered_roaming) {
            TelitCpsmsStatus tCpsmsStatus;
            status = modem_.get_telit_psm(tCpsmsStatus);
            if (status != ModemStatus::ok) {
                NETWORK_LOG_ERR("Failed to get PSM configuration");
                continue; // try next operator
            }
            NETWORK_LOG_INF(
                "PSM status for operator %s: status=%d, mode=%d, t3412=%u, t3324=%u psm_version=%d, psm_threshold=%u",
                op.long_name.c_str(), tCpsmsStatus.status, static_cast<int>(tCpsmsStatus.mode), tCpsmsStatus.t3412,
                tCpsmsStatus.t3324, static_cast<int>(tCpsmsStatus.psm_version), tCpsmsStatus.psm_threshold);
            if (tCpsmsStatus.mode == PsmMode::disable) {
                NETWORK_LOG_WRN("PSM is disabled for this operator, cannot enter PSM mode");
                continue; // try next operator
            } else if (tCpsmsStatus.t3412 == 0 && tCpsmsStatus.t3324 == 0) {
                NETWORK_LOG_WRN("PSM timers are not configured for this operator, cannot enter PSM mode");
                continue; // try next operator
            } else if (tCpsmsStatus.t3412 == lteConfig.psm_t3412 && tCpsmsStatus.t3324 == lteConfig.psm_t3324) {
                NETWORK_LOG_INF("PSM timers were accepted by network for this operator");
                fPsmModeAttached = true;
                break;
            } else {
                NETWORK_LOG_WRN(
                    "PSM timers were not accepted by network for this operator, but different timers were granted");
                fPsmModeAttached = true;
                break;
            }
        } else {
            NETWORK_LOG_WRN("Failed to register to operator %s, cannot enter PSM mode", op.long_name.c_str());
            continue; // try next operator
        }
    }

    if (fPsmModeAttached)
        change_state(NetworkLteState::data_ready);
    else
        change_state(
            NetworkLteState::idle_mode); // if we couldn't enter PSM mode with any operator, go back to idle mode

    // resume timers and URCs after PSM flow
    if (st_timer) {
        st_timer->start(lteConfig.data_ready_timeout_sec * 1000, [this]() { on_timer_expired(); });
    }

    modem_.set_psm_urc(true);          // enable PSM URCs in normal mode
    modem_.set_registration_urc(true); // enable registration URCs in normal mode
    modem_.set_pdp_urc(true);          // enable PDP URCs in normal mode
    modem_.set_plmnsearchexh_notify(true);

    return fPsmModeAttached; // return whether PSM mode was successfully entered
}

bool NetworkLte::enter_sleep() {
    if (state_ != NetworkLteState::sleep_mode && state_ != NetworkLteState::off_mode &&
        state_ != NetworkLteState::switched_off) {
        go_to_state(NetworkLteState::sleep_mode);
    } else {
        NETWORK_LOG_INF("Already in sleep mode or off mode");
        return true; // already in sleep or off mode, consider it a success
    }
    if (state_ != NetworkLteState::sleep_mode && state_ != NetworkLteState::off_mode &&
        state_ != NetworkLteState::switched_off) {
        NETWORK_LOG_ERR("Failed to enter sleep mode or off mode");
        return false; // failed to enter sleep mode
    }
    NETWORK_LOG_INF("Successfully entered sleep mode");
    return true; // in off mode or switched off, consider it a success
}

bool NetworkLte::enter_transparent_mode() {
    if (state_ != NetworkLteState::transparent_mode) {
        go_to_state(NetworkLteState::transparent_mode);
    }
    if (state_ == NetworkLteState::transparent_mode) {
        NETWORK_LOG_INF("Successfully entered transparent mode");
        return true;
    }
    return false;
}

bool NetworkLte::exit_transparent_mode() {
    if (state_ != NetworkLteState::transparent_mode) {
        NETWORK_LOG_ERR("Not currently in transparent mode, cannot exit");
        return false; // not in transparent mode, cannot exit
    }
    go_to_state(
        NetworkLteState::idle_mode); // after exiting transparent mode, go back to idle mode to restart normal flow
    if (state_ != NetworkLteState::transparent_mode) {
        NETWORK_LOG_INF("Successfully exited transparent mode");
        return true;
    }
    return false;
}

bool NetworkLte::leave_transparent_mode() {
    if (state_ != NetworkLteState::transparent_mode) {
        NETWORK_LOG_ERR("Not currently in transparent mode, cannot leave");
        return false;
    }
    // Use the action handler so URCs are properly re-enabled before state changes.
    call_action(ModemAction::leave_transparent_mode);
    execute_actions();
    return state_ != NetworkLteState::transparent_mode;
}

bool NetworkLte::send_at_command(std::string_view command, FixedString<AT_RESPONSE_MAX>& response,
                                 uint32_t timeout_ms) {
    // send command
    if (state_ != NetworkLteState::transparent_mode) {
        NETWORK_LOG_ERR("Modem is not in transparent mode, cannot send AT command");
        response = "ERROR: Not in transparent mode";
        return false;
    }
    auto status = modem_.send_at_command(command, response, timeout_ms);
    if (status == ModemStatus::timeout) {
        NETWORK_LOG_ERR("Timeout while sending AT command: %.*s", static_cast<int>(command.size()), command.data());
        response = "ERROR: Timeout";
        return false;
    } else if (status == ModemStatus::busy) {
        NETWORK_LOG_ERR("Failed to send AT command: %.*s", static_cast<int>(command.size()), command.data());
        response = "ERROR: Busy";
        return false;
    } else if (status != ModemStatus::ok) {
        NETWORK_LOG_ERR("Failed to send AT command: %.*s", static_cast<int>(command.size()), command.data());
        response = "ERROR: Failed to send AT command";
        return false;
    }
    return true;
}

bool NetworkLte::update_modem(std::string_view /*firmware_url*/) {
    if (state_ != NetworkLteState::modem_fota) {
        go_to_state(NetworkLteState::modem_fota);
    }
    if (state_ != NetworkLteState::modem_fota) {
        NETWORK_LOG_ERR("Failed to enter modem FOTA mode");
        return false;
    } else {
        // send AT commands to perform FOTA update, e.g. set server, start download, etc.
        NETWORK_LOG_INF("Successfully entered modem FOTA mode, starting update process");
        NETWORK_LOG_WRN("Note: FOTA update commands are not implemented");
        fColdBoot = true; // after FOTA, we can consider the next boot as the first one to re-apply configuration
        go_to_state(NetworkLteState::setup_mode); // after FOTA, go back to setup mode to re-apply configuration and
                                                  // re-attach to network
        return true;
    }
}

// ---------------------------------------------------------------------------
// loop — state-transition logic (keep it fed)
// ---------------------------------------------------------------------------
NetworkLteState NetworkLte::loop(NetworkLteState target_state) {
    // In transparent mode the AT passthrough thread owns UART I/O.
    // Calling poll_urc() here would race against send_raw() on that thread
    // and steal the AT command response before it can be read.
    if (state_ != NetworkLteState::transparent_mode) {
        for (const auto& urc : modem_.poll_urc()) {
            handle_urc(urc);
        }
    }

    // Check if the last AT command resulted in a timeout error and trigger recovery.
    if (modem_.last_status() == ModemStatus::timeout) {
        NETWORK_LOG_ERR("AT command timeout detected, triggering recovery");
        on_event(NetworkLteEvent::at_command_no_response);
    }

    // !! only last event is processed for now!!
    // modem events change state without further action, actions are forbidden here for now!!
    switch (get_event()) {
        case NetworkLteEvent::psm_enter:
            NETWORK_LOG_INF("Entered PSM mode");
            change_state(NetworkLteState::sleep_mode);
            break;
        case NetworkLteEvent::psm_exit:
            NETWORK_LOG_INF("Exited PSM mode");
            if (prev_state_ == NetworkLteState::data_ready) {
                change_state(NetworkLteState::data_ready); // assume we were in sleep mode with an active connection, we
                                                           // can go directly to data ready state
            } else {
                change_state(NetworkLteState::idle_mode); // otherwise go back to idle and restart attach flow
            }
            break;
        case NetworkLteEvent::network_detached: {
            NETWORK_LOG_ERR("Network PDP detached, no longer registered to network");
            uint8_t i = 0;
            while (i < MAX_SERVER_CONNECTIONS) {
                serverInfo[i++].state = ServerState::disconnected;
            }
            if (state_ != NetworkLteState::switched_off && state_ != NetworkLteState::off_mode &&
                state_ != NetworkLteState::sleep_mode) {
                change_state(NetworkLteState::network_detached);
            }
        } break;
        case NetworkLteEvent::network_attached:
            NETWORK_LOG_INF("Network PDP attached, registered to network");
            change_state(
                NetworkLteState::pdp_context_closed); // after network attach, we can assume any previous PDP context is
                                                      // now deactivated, so we can go to context deactivated state and
                                                      // trigger PDP activation flow from there
            break;
        case NetworkLteEvent::context_closed: {
            NETWORK_LOG_ERR("PDP context closed by network");
            uint8_t i = 0;
            while (i < MAX_SERVER_CONNECTIONS) {
                serverInfo[i++].state = ServerState::disconnected;
            }
            change_state(NetworkLteState::network_detached); // after context is closed, we can assume we are detached
                                                             // from network, so we can go to detached state and restart
                                                             // attach flow from there
        } break;
        case NetworkLteEvent::context_opened:
            NETWORK_LOG_INF("Network PDP context opened, IP address assigned: %s", networkInfo.ip_address.c_str());
            change_state(NetworkLteState::data_ready);
            break;
        case NetworkLteEvent::data_available: {
            NETWORK_LOG_INF("Data available event received on conn_id %d", last_data_conn_id_);
            uint8_t conn = (last_data_conn_id_ > 0) ? last_data_conn_id_ : lteConfig.conn_id;
            StaticVector<uint8_t, xE310::UDP_MAX_BYTES> buf;
            if (modem_.udp_receive(conn, buf) == ModemStatus::ok && !buf.empty()) {
                // Push to RX queue for this connection
                if (message_queue_) {
                    auto err = message_queue_->rx_push(conn, buf.data(), buf.size());
                    if (err != QueueError::ok) {
                        NETWORK_LOG_WRN("RX queue full for conn_id %d, dropping message", conn);
                    }
                }
                std::string_view payload(reinterpret_cast<const char*>(buf.data()), buf.size());
                if (on_data_received_) {
                    on_data_received_(conn, payload, static_cast<uint16_t>(buf.size()));
                }
            }
        } break;
        case NetworkLteEvent::timeout: {
            if (state_ == NetworkLteState::data_ready) {
                call_action(ModemAction::enter_sleep); // if we are in data ready state and we receive a timeout event,
                                                       // we can assume the connection is lost, so we can trigger radio
                                                       // shutdown to restart the attach flow
            } else if (state_ == NetworkLteState::transparent_mode) {
                call_action(ModemAction::leave_transparent_mode); // if we are in transparent mode and we receive a
                                                                  // timeout event, we can assume the connection is
                                                                  // lost, so we can trigger exit transparent mode to
                                                                  // restart the normal flow and eventually go back to
                                                                  // data ready if the connection is still active
            } else {
                call_action(ModemAction::attach_network);
            }
            change_state(NetworkLteState::done);
        } break;
        case NetworkLteEvent::attach_error: {
            call_action(ModemAction::enter_sleep); // Couldn't attach, force sleep mode
        } break;
        case NetworkLteEvent::at_command_no_response:
            call_action(ModemAction::check_responsiveness); // if we receive an AT command error, we can check if the
                                                            // modem is still responsive, if not, we can trigger a
                                                            // reboot to try to recover
            break;
        default: break; // other events are handled in the state switch below
    }

    if (state_ == target_state && target_state != NetworkLteState::none) {
        return state_; // already in target state, no need to process further
    }

    execute_actions();

    return state_;
}

// change to a target state - public
bool NetworkLte::go_to_state(NetworkLteState target_state) {

    if (state_ == target_state) {
        return true; // already in target state
    }

    while (state_ != target_state) {
        // This function generates the necessary sequence of events to transition from the current state to the target
        // state.
        // For simplicity, we will just call loop() in a loop until we reach the target state, relying on the fact that
        // loop() will process events and update the state accordingly. In a more complex implementation, we could have
        // a predefined map of required events for each state transition to speed this up and avoid unnecessary
        // iterations.
        switch (state_) {
            case NetworkLteState::switched_off:
                if (target_state == NetworkLteState::data_ready || target_state == NetworkLteState::transparent_mode) {
                    call_action(ModemAction::power_on);
                }
                break;
            case NetworkLteState::off_mode:
                if (target_state == NetworkLteState::data_ready || target_state == NetworkLteState::transparent_mode) {
                    call_action(ModemAction::turn_on_radio);
                }
                break;
            case NetworkLteState::sleep_mode:
                if (target_state == NetworkLteState::data_ready || target_state == NetworkLteState::transparent_mode) {
                    call_action(ModemAction::wake_up); // trigger wake up to go back to data ready state
                }
                break;
            case NetworkLteState::transparent_mode:
                if (target_state == NetworkLteState::data_ready || target_state == NetworkLteState::idle_mode) {
                    call_action(ModemAction::leave_transparent_mode); // exit transparent mode, re-enable URCs and
                                                                      // restart normal flow toward data_ready
                }
                break;
            case NetworkLteState::done:
                NETWORK_LOG_ERR("Cannot transition from done state to %s, reset the modem first",
                                state_to_str(target_state));
                return false;
            case NetworkLteState::modem_fota:
                NETWORK_LOG_ERR("Cannot transition from modem_fota state to %s, wait for FOTA to complete",
                                state_to_str(target_state));
                return false;
            default:
                if (target_state == NetworkLteState::transparent_mode) {
                    call_action(ModemAction::enter_transparent_mode);
                }
                break; // for other states, we will rely on the normal event flow to transition to the target state
        }

        loop(target_state); // process events and update state until we reach the target state
        // In a real implementation, we might want to add a timeout here to avoid infinite loops in case of unexpected
        // conditions. if timeout reached:
        //     NETWORK_LOG_ERR("Timeout while trying to transition from %s to %s", state_to_str(state_),
        //     state_to_str(target_state)); return false;
        switch (event_) {
            case NetworkLteEvent::network_error:
            case NetworkLteEvent::attach_error:
            case NetworkLteEvent::context_error:
                NETWORK_LOG_ERR("Error event received while trying to transition to target state, aborting transition");
                return false; // if we receive an error event during the transition, we can consider the transition
                              // failed and return false
            default: break;   // for other events, we will just continue processing
        }
    }
    return true;
}

// perform actions - internal
void NetworkLte::execute_actions() {
    ModemAction action = get_action();
    if (action != ModemAction::none) NETWORK_LOG_DBG("Executing action: %s", action_to_str(action));
    switch (action) {
        case ModemAction::reboot: {
            auto status = modem_.reboot();
            if (check_status_response(status)) {
                change_state(NetworkLteState::rebooting); // after reboot, we can consider
                call_action(ModemAction::power_on); // after reboot, check responsiveness to continue with the flow
            }
        } break;
        case ModemAction::check_responsiveness: {
            // check if modem is responsive by sending an AT command
            while (true) {
                auto status = modem_.at_ok();
                if (status != ModemStatus::ok) {
                    NETWORK_LOG_ERR("Modem is not responsive");
                    // call_action(ModemAction::reboot); // try rebooting the modem to recover responsiveness
                } else {
                    NETWORK_LOG_INF("Modem is responsive");
                    change_state(NetworkLteState::idle_mode); // if modem is responsive, we can go to idle mode and
                                                              // continue with the flow
                    call_action(ModemAction::query_network_status); // trigger radio setup to start attach flow
                    break;
                }
                delay_ms(2000); // wait for 2 seconds before trying again
            }
        } break;
        case ModemAction::factory_reset: {
            /*
            auto status = modem_.factory_reset();
            if(status == ModemStatus::ok){
                change_state(NetworkLteState::rebooting); // after factory reset, the modem will reboot, so we can
            consider it in rebooting state and wait for it to be responsive again before continuing with the flow
            }
            */
        } break;
        case ModemAction::power_on: {
            // power on modem
            // modem_.power_on();
            modem_.set_baudrate(115200);
            modem_.set_echo(false);
            // check if modem is responsive            AtResponse response;
            {
                auto status = modem_.at_ok();
                if (status != ModemStatus::ok) {
                    NETWORK_LOG_ERR("Failed to power on modem");
                    change_state(NetworkLteState::switched_off);
                    break;
                }
                modem_.set_echo(false);
                NETWORK_LOG_INF("Modem powered on and responsive");
                change_state(NetworkLteState::idle_mode);
                call_action(ModemAction::setup_radio); // trigger radio setup to start attach flow
                fColdBoot = true;
                nNetworkAttempts = 0;
            }
        } break;
        case ModemAction::power_off: {
            // modem_.power_off();
            NETWORK_LOG_INF("Fake power off!!");
            change_state(NetworkLteState::switched_off);
        } break;
        case ModemAction::turn_on_radio: {
            // which command should I send to leave the off mode ?
            change_state(NetworkLteState::idle_mode);
            call_action(ModemAction::setup_radio); // trigger attach flow in idle mode
        } break;
        case ModemAction::switch_off_radio: {
#if defined(PLATFORM_ZEPHYR) || defined(__ZEPHYR__)
            if (lteConfig.fCfunSleep) {
                modem_.shutdown();
                change_state(NetworkLteState::off_mode);
            } else {
                // Don't shutdown bcs then modem cannot be waked up
                NETWORK_LOG_INF("Fake shutdown!!");
            }
#else
            if (lteConfig.fCfunSleep) {
                modem_.shutdown();
                change_state(NetworkLteState::off_mode);
            } else {
                // Don't shutdown bcs then modem cannot be waked up
                NETWORK_LOG_INF("Fake shutdown!!");
            }
#endif
        } break;
        case ModemAction::enter_sleep: {
            // check if modem is in PSM modem by reading GPIO state or PSM status, if not, we can trigger radio shutdown
            // to restart the attach flow and eventually enter PSM if it is enabled in the configuration
            call_action(ModemAction::switch_off_radio); // if PSM is not enabled, we can trigger radio shutdown to
                                                        // restart the attach flow
        } break;
        case ModemAction::wake_up: {
            // drive GPIO0 + CFUN=1, Warm boot
            if (prev_state_ == NetworkLteState::data_ready) {
                NETWORK_LOG_INF(
                    "Waking up from sleep mode, previous state was data ready, trying to go back to data ready state");
                change_state(NetworkLteState::data_ready); // assuming we were in sleep mode with an active connection,
                                                           // we can try to go back to data ready state directly
                call_action(ModemAction::query_network_status);
            } else {
                NETWORK_LOG_INF("Waking up from sleep mode, previous state was not data ready, going to idle mode to "
                                "restart attach flow");
                fWarmBoot = true;
                change_state(NetworkLteState::idle_mode); // after waking up, we can go back to idle mode and check
                                                          // network status to continue with the flow
                call_action(ModemAction::setup_radio);    // trigger attach flow in idle mode
            }
        } break;
        case ModemAction::setup_radio: {
            // get modem info
            modem::ModemStatus status;
            if (false) { // NOLINT(readability-simplify-boolean-expr)
                status = modem_.request_imei_sv(modemInfo.imei_sv);
                if (status != ModemStatus::ok) {
                    // flag error
                }
                status = modem_.request_model_id(modemInfo.model_id);
                if (status != ModemStatus::ok) {
                    // flag error
                }
                status = modem_.request_sw_package_version(modemInfo.sw_package_version);
                if (status != ModemStatus::ok) {
                    // flag error
                }
                status = modem_.request_telit_id(modemInfo.telit_id);
                if (status != ModemStatus::ok) {
                    // flag error
                }
                status = modem_.request_identification(modemInfo.identification);
                if (status != ModemStatus::ok) {
                    // flag error
                }
                status = modem_.get_imei(modemInfo.imei);
                if (status != ModemStatus::ok) {
                    // flag error
                }
            }
            // get modem configuration (bands, iot tech, apn) and apply it

            if (fChangeBands || fChangeRAT) {
                bool fReboot = false;
                if (fChangeBands) {
                    fChangeBands = false;
                    if (nAttachRetries == 0)
                        status = modem_.set_lte_bands(lteConfig.default_lte_bands);
                    else
                        status = modem_.set_lte_bands(lteConfig.fallback_lte_bands);

                    if (status != ModemStatus::ok) {
                        NETWORK_LOG_ERR("Failed to set LTE bands");
                        // flag error
                    } else {
                        fReboot = true;
                    }
                }
                if (fChangeRAT) {
                    fChangeRAT = false;
                    if (nAttachRetries == 0)
                        status = modem_.set_iot_tech(lteConfig.default_iot_tech);
                    else
                        status = modem_.set_iot_tech(lteConfig.fallback_iot_tech);

                    if (status != ModemStatus::ok) {
                        NETWORK_LOG_ERR("Failed to set IoT technology");
                        // flag error
                    } else {
                        fReboot = true;
                    }
                }
                // reboot modem to apply new bands configuration
                if (fReboot) call_action(ModemAction::reboot);
            } else {
                if (fWarmBoot || fColdBoot) {
                    NETWORK_LOG_INF("Re-applying modem configuration after boot");
                    fWarmBoot = false;
                    fColdBoot = false;
                    TelitCpsmsConfig cfg = {
                        PsmMode::enable,
                        false,
                        0,
                        false,
                        0,
                        true,
                        lteConfig.psm_t3412, // periodic TAU timer in seconds
                        true,
                        lteConfig.psm_t3324, // active time timer in seconds
                        false,
                        PsmVersion::rel12_retain, // Rel12 retain
                        false,
                        60 // min duration to enter PSM in seconds
                    };
                    if (!lteConfig.fPsmEnable)
                        modem_.disable_telit_psm();
                    else
                        modem_.set_telit_psm(cfg);

                    modem_.set_psm_urc(true);          // enable PSM URCs in normal mode
                    modem_.set_registration_urc(true); // enable registration URCs in normal mode
                    modem_.set_pdp_urc(true);          // enable PDP URCs in normal mode
                    modem_.set_plmnsearchexh_notify(true);
                }
                change_state(NetworkLteState::idle_mode);
                call_action(ModemAction::query_network_status); // check network status
            }
        } break;
        case ModemAction::query_network_status: {
            auto status = modem_.get_registration_status(regInfo, RadioTech::cat_m1);
            if (!check_status_response(status)) break;
            if (regInfo.stat == RegStatus::denied || regInfo.stat == RegStatus::not_registered ||
                regInfo.stat == RegStatus::searching) {
                change_state(NetworkLteState::network_detached); // start attach with fallback config
            } else if (regInfo.stat == RegStatus::registered_home || regInfo.stat == RegStatus::registered_roaming) {
                call_action(ModemAction::query_pdp_context); // trigger PDP activation flow in context closed state
            } else if (regInfo.stat == RegStatus::unknown) {
                // How to deal with it ?
                // let's assume we are connected for now
                NETWORK_LOG_DBG("Registration status unknown..");
                change_state(NetworkLteState::network_detached); // start attach with fallback config
            }
        } break;
        case ModemAction::attach_network: {
            modem_.power_radio();
            // modem_.set_iot_tech(lteConfig.default_iot_tech); // needs reboot
            // modem_.network_attach();
            if (nAttachRetries == 0) {

                auto status = modem_.get_apn(lteConfig.cid, regInfo.apn);
                if (status == ModemStatus::ok) {
                    if (regInfo.apn != lteConfig.default_apn) {
                        NETWORK_LOG_WRN(
                            "Current APN %s is different from default config %s, changing it to default config",
                            regInfo.apn.c_str(), lteConfig.default_apn.c_str());
                        status = modem_.set_apn(lteConfig.cid, lteConfig.default_apn);
                        if (status != ModemStatus::ok) {
                            NETWORK_LOG_ERR("Failed to set APN");
                            // flag error
                        }
                    }
                }

                // check default iot_tech
                RadioTech current_tech;
                uint8_t gsm_priority;
                status = modem_.get_iot_tech(current_tech, gsm_priority);
                if (status == ModemStatus::ok) {
                    if (current_tech != lteConfig.default_iot_tech) {
                        NETWORK_LOG_WRN("Current IoT technology %d is different from default config %d, changing it to "
                                        "default config and rebooting to apply",
                                        static_cast<int>(current_tech), static_cast<int>(lteConfig.default_iot_tech));
                        fChangeRAT = true;
                    }
                }
                // check default bands
                BandConfig bands;
                status = modem_.get_bands(bands);

                if (status == ModemStatus::ok) {
                    if (bands.lte_mask != lteConfig.default_lte_bands) {
                        NETWORK_LOG_WRN("Current LTE bands %llu are different from default config %llu, changing it to "
                                        "default config and rebooting to apply",
                                        static_cast<unsigned long long>(bands.lte_mask),
                                        static_cast<unsigned long long>(lteConfig.default_lte_bands));
                        fChangeBands = true;
                    }
                }

                // if any diferent set it to default config and reboot to apply, then start attach flow in idle mode
                if (fChangeRAT || fChangeBands) {
                    call_action(ModemAction::setup_radio); // if we need to change either RAT or bands, we can just
                                                           // reboot once and apply both changes at the same time
                    break;
                }

                NETWORK_LOG_INF("Starting network attach with default configuration");
                change_state(NetworkLteState::network_attaching);
                status = modem_.set_operator_manual(lteConfig.plmn, lteConfig.default_iot_tech);
                if (status != ModemStatus::ok) {
                    NETWORK_LOG_ERR("Failed to set operator manual for fallback configuration");
                    change_state(NetworkLteState::network_detached);
                    // flag error and stay in the same state to retry later
                }
            } else if (nAttachRetries < lteConfig.max_attach_retries) {
                NETWORK_LOG_INF("Retrying network attach with fallback configuration, attempt %d", nAttachRetries);

                auto status = modem_.get_apn(lteConfig.cid, regInfo.apn);
                if (status == ModemStatus::ok) {
                    if (regInfo.apn != lteConfig.fallback_apn) {
                        NETWORK_LOG_WRN(
                            "Current APN %s is different from fallback config %s, changing it to fallback config",
                            regInfo.apn.c_str(), lteConfig.fallback_apn.c_str());
                        status = modem_.set_apn(lteConfig.cid, lteConfig.fallback_apn);
                        if (status != ModemStatus::ok) {
                            NETWORK_LOG_ERR("Failed to set APN");
                            // flag error
                        }
                    }
                }

                // check fallback iot_tech
                RadioTech current_tech;
                uint8_t gsm_priority;
                status = modem_.get_iot_tech(current_tech, gsm_priority);
                if (status == ModemStatus::ok) {
                    if (current_tech != lteConfig.fallback_iot_tech) {
                        NETWORK_LOG_WRN("Current IoT technology %d is different from fallback config %d, changing it "
                                        "to fallback config and rebooting to apply",
                                        static_cast<int>(current_tech), static_cast<int>(lteConfig.fallback_iot_tech));
                        fChangeRAT = true;
                    }
                }
                // check fallback bands
                BandConfig bands;
                status = modem_.get_bands(bands);

                if (status == ModemStatus::ok) {
                    if (bands.lte_mask != lteConfig.fallback_lte_bands) {
                        NETWORK_LOG_WRN("Current LTE bands %llu are different from fallback config %llu, changing it "
                                        "to fallback config and rebooting to apply",
                                        static_cast<unsigned long long>(bands.lte_mask),
                                        static_cast<unsigned long long>(lteConfig.fallback_lte_bands));
                        fChangeBands = true;
                    }
                }

                // if any diferent set it to fallback config and reboot to apply, then start attach flow in idle mode
                if (fChangeRAT || fChangeBands) {
                    call_action(ModemAction::setup_radio); // if we need to change either RAT or bands, we can just
                                                           // reboot once and apply both changes at the same time
                    break;
                }

                change_state(NetworkLteState::network_attaching);
                status = modem_.set_operator_auto();
                if (status != ModemStatus::ok) {
                    NETWORK_LOG_ERR("Failed to set operator manual for fallback configuration");
                    // flag error and stay in the same state to retry later
                    change_state(NetworkLteState::network_detached);
                }

            } else {
                NETWORK_LOG_ERR("Max attach retries reached, giving up");
                on_event(NetworkLteEvent::attach_error); // flag attach error to trigger power off in the next step
            }
            nAttachRetries++;
        } break;
        case ModemAction::query_pdp_context: {
            bool fPdpActive = false;
            auto status = modem_.get_pdp_state(lteConfig.cid, fPdpActive);
            if (status != ModemStatus::ok) {
                NETWORK_LOG_ERR("Failed to query PDP context state");
            } else if (!fPdpActive) {
                change_state(NetworkLteState::pdp_context_closed); // if context is inactive, we can consider it closed
                                                                   // and trigger PDP activation flow
                call_action(ModemAction::open_pdp_context);        // trigger PDP activation flow
            } else {
                status = modem_.get_ip_address(lteConfig.cid, networkInfo.ip_address);
                if (status != ModemStatus::ok) {
                    NETWORK_LOG_ERR("Failed to get IP address");
                    // flag error and stay in the same state to retry later
                } else {
                    NETWORK_LOG_DBG("Already registered with IP: %s", networkInfo.ip_address.c_str());
                    change_state(
                        NetworkLteState::data_ready); // already have IP, we can go directly to data ready state
                }
            }
        } break;
        case ModemAction::open_pdp_context: {
            auto status = modem_.activate_pdp(lteConfig.cid);
            if (status == ModemStatus::ok) {
                // change_state(NetworkLteState::pdp_context_opening);
                change_state(NetworkLteState::data_ready);
            } else {
                NETWORK_LOG_ERR("Failed to activate PDP context");
                // flag error and stay in the same state to retry later
            }
            break;
        }
        case ModemAction::send_data: {
            if (state_ != NetworkLteState::data_ready) {
                NETWORK_LOG_WRN("Ignoring send_data action: state=%d (requires data_ready)", static_cast<int>(state_));
                break;
            }
            if (!message_queue_) {
                break;
            }

            QueueMessage msg;
            while (message_queue_->tx_pop_next(msg) == QueueError::ok) {
                const uint8_t id = msg.cid;
                if (id < 1 || id > MAX_SERVER_CONNECTIONS) {
                    NETWORK_LOG_ERR("Invalid conn_id %d in TX queue, dropping message", id);
                    continue;
                }
                if (serverInfo[id - 1].state != ServerState::connected) {
                    NETWORK_LOG_WRN("conn_id %d not connected, state: %d dropping queued TX message", id,
                                    static_cast<int>(serverInfo[id - 1].state));
                    continue;
                }

                NETWORK_LOG_INF("id: %d, sending message of length %d", id, static_cast<int>(msg.length));
                NETWORK_LOG_INF("protocol: %s", serverInfo[id - 1].protocol.c_str());
                if (serverInfo[id - 1].protocol == "UDP") {
                    auto status = modem_.udp_send(id, msg.data.data(), msg.length);
                    if (status != ModemStatus::ok) {
                        NETWORK_LOG_ERR("Failed to send UDP data on conn_id %d", id);
                    }
                } else if (serverInfo[id - 1].protocol == "TCP") {
                    NETWORK_LOG_WRN("TCP send not implemented for conn_id %d", id);
                } else {
                    NETWORK_LOG_ERR("Unknown protocol for conn_id %d, cannot send", id);
                }
            }
        } break;
        case ModemAction::read_data:
            // for testing, we can just read data from the modem and print it, in real implementation, we would pass it
            // to a buffer
            {
                StaticVector<uint8_t, xE310::UDP_MAX_BYTES> buf;
                if (modem_.udp_receive(lteConfig.conn_id, buf) == ModemStatus::ok && !buf.empty()) {
                    char payload[xE310::UDP_MAX_BYTES + 1];
                    size_t len = std::min(buf.size(), sizeof(payload) - 1);
                    std::memcpy(payload, buf.data(), len);
                    payload[len] = '\0';
                    NETWORK_LOG_INF("Received data: %s", payload);
                } else {
                    NETWORK_LOG_ERR("Failed to read data or no data available");
                    // flag error
                }
            }
            break;
        case ModemAction::data_complete:
            // signal server that we are going offline for a while (e.g. going to sleep mode) so it should not expect
            // data from us and we can close the connection gracefully
            break;
        case ModemAction::enter_transparent_mode: {
            // switch modem to transparent mode, in this mode we can just send data to the modem and it will be sent
            // directly without needing to use UDP send/receive functions, this is useful for low latency applications
            // where we want to minimize the time between sending data and it being sent over the network do any task
            // needed to switch to transparent mode (e.g. close existing PDP context, set up new PDP context with
            // transparent mode settings, etc.) Change state FIRST so loop() stops calling poll_urc() before any AT
            // command goes out — otherwise the main thread races against send_raw() on the IPC thread and steals the
            // command responses via poll_urc().
            change_state(NetworkLteState::transparent_mode);
            modem_.set_psm_urc(
                false); // disable PSM URCs in transparent mode to avoid interfering with raw data reception
            modem_.set_registration_urc(
                false); // disable registration URCs in transparent mode to avoid interfering with raw data reception
            modem_.set_pdp_urc(
                false); // disable PDP URCs in transparent mode to avoid interfering with raw data reception
            modem_.set_plmnsearchexh_notify(false);
            NETWORK_LOG_INF("Modem in transparent mode (ready to receive AT commands)");
        } break;

        case ModemAction::leave_transparent_mode: {
            // switch modem to normal mode, in this mode we use UDP send/receive functions for data transmission
            // do any task needed to switch to normal mode (e.g. close existing PDP context, set up new PDP context with
            // normal mode settings, etc.)
            NETWORK_LOG_INF("Modem leaving transparent mode (ready to receive AT commands)");
            modem_.set_psm_urc(true);          // enable PSM URCs in normal mode
            modem_.set_registration_urc(true); // enable registration URCs in normal mode
            modem_.set_pdp_urc(true);          // enable PDP URCs in normal mode
            modem_.set_plmnsearchexh_notify(true);
            change_state(NetworkLteState::idle_mode); // after leaving transparent mode, we can go back to idle mode and
                                                      // restart attach flow to ensure we are properly connected before
                                                      // sending/receiving data
            call_action(ModemAction::query_network_status); // trigger attach flow to ensure we are properly connected
                                                            // before sending/receiving data
        } break;
    }
}

// called from state_machine - internal
// actions to perform on state change, such as starting/stopping timers, logging state transitions, etc.
void NetworkLte::change_state(NetworkLteState new_state) {

    if (new_state == state_) {
        return; // no state change
    }

    prev_state_ = state_;
    state_ = new_state;
    log_state();

    // stop any timers that are running for the previous state, and log elapsed time for debugging/analytics purposes
    switch (prev_state_) {
        case NetworkLteState::network_attaching: {
            uint32_t elasped = st_timer->elapsed_ms();
            NETWORK_LOG_INF("Time spent in network_attaching state: %u ms", elasped);
        }
            st_timer->stop(); // stop attach timer if running
            break;
        case NetworkLteState::pdp_context_opening: {
            uint32_t elasped = st_timer->elapsed_ms();
            NETWORK_LOG_INF("Time spent in pdp_context_opening state: %u ms", elasped);
        }
            st_timer->stop(); // stop PDP activation timer if running
            break;
        case NetworkLteState::data_ready: {
            uint32_t elasped = st_timer->elapsed_ms();
            NETWORK_LOG_INF("Time spent in data_ready state: %u ms", elasped);
        }
            st_timer->stop(); // stop PDP activation timer if running
            break;
        case NetworkLteState::transparent_mode: {
            uint32_t elasped = st_timer->elapsed_ms();
            NETWORK_LOG_INF("Time spent in transparent_mode state: %u ms", elasped);
        }
            st_timer->stop(); // stop any timers related to transparent mode if needed
            break;
        default: break; // no special handling needed for other states when exiting
    }

    // init timers for states that require timeouts to trigger retries or error handling if we get stuck in those states
    // for too long (e.g. if we are attaching to the network but it is taking too long, we can trigger a retry or
    // fallback to a different configuration)
    switch (state_) {
        case NetworkLteState::network_attaching:
            st_timer->start(lteConfig.attach_timeout_sec * 1000,
                            [this]() { on_timer_expired(); }); // example timeout, adjust as needed
            break;
        case NetworkLteState::pdp_context_opening:
            st_timer->start(lteConfig.pdp_timeout_sec * 1000,
                            [this]() { on_timer_expired(); }); // example timeout, adjust as needed
            break;
        case NetworkLteState::data_ready: {

            // modem_.get_iot_tech(regInfo.act, networkInfo.gsm_priority); // update IoT tech in info struct after
            // applying it to modem modem_.get_registration_status(regInfo, RadioTech::gsm); // update registration info
            // in info struct after applying it to modem
            modem_.get_clock(modemClock);             // update clock from modem
            modem_.get_signal_quality(signalQuality); // update signal quality in info struct after applying it to modem
            modem_.get_operator(
                regInfo.operator_name); // update operator name in info struct after applying it to modem
            modem_.get_apn(lteConfig.cid, regInfo.apn); // parse +CGDCONT? into regInfo.apn and regInfo.ip_address
            // modem_.get_pdp_state(lteConfig.cid, regInfo.fPdpActive); // update PDP state in info struct after
            // applying it to modem

            TelitCpsmsStatus psm_state;
            auto status =
                modem_.get_telit_psm(psm_state); // update PSM config in info struct after applying it to modem
            if (status == ModemStatus::ok) {
                /*
                networkInfo.psm_mode = psm_state.mode;
                networkInfo.psm_t3324 = psm_state.t3324;
                networkInfo.psm_t3412 = psm_state.t3412;
                networkInfo.psm_version = psm_state.psm_version;
                networkInfo.psm_threshold = psm_state.psm_threshold;
                */
                NETWORK_LOG_INF("PSM state: %u", static_cast<unsigned>(psm_state.mode));
                NETWORK_LOG_INF("PSM t3324: %u", psm_state.t3324);
                NETWORK_LOG_INF("PSM t3412: %u", psm_state.t3412);
                NETWORK_LOG_INF("PSM psm_version: %u", static_cast<unsigned>(psm_state.psm_version));
                NETWORK_LOG_INF("PSM psm_threshold: %u", psm_state.psm_threshold);
                NETWORK_LOG_INF("PSM mode: %u", static_cast<unsigned>(psm_state.mode));
            } else {
                NETWORK_LOG_ERR("Failed to get PSM status");
            }
            st_timer->start(lteConfig.data_ready_timeout_sec * 1000,
                            [this]() { on_timer_expired(); }); // example timeout, adjust as needed
        } break;
        case NetworkLteState::transparent_mode:
            st_timer->start(lteConfig.transparent_timeout_sec * 1000,
                            [this]() { on_timer_expired(); }); // example timeout, adjust as needed
            break;
        default: break; // no special handling needed for other states when entering
    }

    switch (state_) {
        case NetworkLteState::network_detached:
            NETWORK_LOG_INF("State changed to network_detached, resetting network info and server states");
            networkInfo = {}; // reset network info
            {
                uint8_t i = 0;
                while (i < MAX_SERVER_CONNECTIONS) {
                    serverInfo[i++].state = ServerState::disconnected; // reset server states
                }
            }
            call_action(ModemAction::attach_network); // trigger attach flow to try to re-attach to network
            break;
        case NetworkLteState::pdp_context_closed:
            NETWORK_LOG_INF("State changed to pdp_context_closed, resetting IP address and server states");
            networkInfo.ip_address = ""; // reset IP address
            {
                uint8_t i = 0;
                while (i < MAX_SERVER_CONNECTIONS) {
                    serverInfo[i++].state = ServerState::disconnected; // reset server states
                }
            }
            call_action(ModemAction::open_pdp_context); // trigger PDP activation flow to try to open PDP context and
                                                        // get an IP address
            break;
    }
}

void NetworkLte::handle_urc(std::string_view urc) {
    // +CREG / +CEREG URC can be either:
    //   +CEREG: <stat>,<tac>,<ci>,<AcT>...
    // or (query-style payload):
    //   +CEREG: <n>,<stat>...
    NETWORK_LOG_DBG("Handle URC: %.*s", static_cast<int>(urc.size()), urc.data());
    if (urc.substr(0, 6) == "+CREG:" || urc.substr(0, 7) == "+CEREG:") {
        // Extract numeric CSV fields after ':' while preserving empty/quoted fields.
        int stat = -1;
        auto colon = urc.find(':');
        if (colon == std::string_view::npos) {
            return;
        }

        auto trim_sv = [](std::string_view s) -> std::string_view {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
            return s;
        };

        auto is_uint_sv = [](std::string_view s) {
            if (s.empty()) return false;
            return std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
        };

        std::string_view payload = urc.substr(colon + 1);
        // Parse CSV fields into a small fixed array
        constexpr size_t MAX_FIELDS = 8;
        std::string_view fields[MAX_FIELDS];
        size_t field_count = 0;
        size_t pos = 0;
        while (pos <= payload.size() && field_count < MAX_FIELDS) {
            auto comma = payload.find(',', pos);
            if (comma == std::string_view::npos) {
                fields[field_count++] = trim_sv(payload.substr(pos));
                break;
            }
            fields[field_count++] = trim_sv(payload.substr(pos, comma - pos));
            pos = comma + 1;
        }

        if (field_count > 0 && is_uint_sv(fields[0])) {
            // Default for URC format: first field is <stat>.
            stat = std::atoi(std::string(fields[0]).c_str());

            // Query-style payload is <n>,<stat> with both numeric first fields.
            // In this case use second field as registration status.
            if (field_count > 1 && is_uint_sv(fields[1])) {
                int first = std::atoi(std::string(fields[0]).c_str());
                int second = std::atoi(std::string(fields[1]).c_str());
                if (first >= 0 && first <= 5 && second >= 0 && second <= 10) {
                    stat = second;
                }
            }
        }

        if (stat == 1 || stat == 5) {                    // registered home / roaming
            call_action(ModemAction::query_pdp_context); // trigger PDP activation flow
            nAttachRetries = 0;
        } else if (stat == 3) { // denied
            // on_event(NetworkLteEvent::network_detached);
        }
        return;
    }
    // +CGEV: various PDP/PS events
    if (urc.substr(0, 6) == "+CGEV:") {
        // body is everything after "+CGEV:" with leading space stripped
        std::string_view body = urc.substr(6);
        auto s = body.find_first_not_of(' ');
        std::string_view ev = (s == std::string_view::npos) ? std::string_view{} : body.substr(s);

        if (ev.substr(0, 8) == "NW_DEACT" || // network forced context deactivation
            ev.substr(0, 8) == "ME DEACT") { // ME forced context deactivation
            on_event(NetworkLteEvent::context_closed);
        } else if (ev.substr(0, 9) == "NW_DETACH" || // network PS detach (all contexts lost)
                   ev.substr(0, 9) == "ME_DETACH" ||
                   ev.substr(0, 9) == "ME DETACH") { // ME PS detach (all contexts lost)
            on_event(NetworkLteEvent::network_detached);
        } else if (ev.substr(0, 6) == "REJECT") { // context activation rejected
            on_event(NetworkLteEvent::context_rejected);
        }
        // NW REACT (network requesting reactivation) — no action needed
        return;
    }
    // #PSMURC: <ActiveTime>,<PSMTime>  →  modem entered PSM
    if (urc.substr(0, 8) == "#PSMURC:") {
        on_event(NetworkLteEvent::psm_enter);
        return;
    }
    if (urc.substr(0, 25) == "%NOTIFYEV:\"PLMNSEARCHEXH\"") {
        on_event(NetworkLteEvent::network_detached);
        return;
    }
    // SRING: <conn_id>  →  new data available on socket
    if (urc.substr(0, 6) == "SRING:") {
        NETWORK_LOG_DBG("Data available URC received");
        std::string_view body = urc.substr(6);
        auto s = body.find_first_not_of(' ');
        if (s != std::string_view::npos) {
            int id = std::atoi(std::string(body.substr(s)).c_str());
            last_data_conn_id_ = static_cast<uint8_t>(id);
            NETWORK_LOG_DBG("Data available on id: %d", id);
            on_event(NetworkLteEvent::data_available);
        }
        return;
    }
    if (urc.substr(0, 10) == "NO CARRIER") {
        uint8_t conn_id = MAX_SERVER_CONNECTIONS;
        while (conn_id > 0 && conn_id <= MAX_SERVER_CONNECTIONS &&
               serverInfo[conn_id - 1].state != ServerState::disconnected) {
            --conn_id;
        }
    }
}

// replace all occurrences of event_ = with on_event(event) to ensure that all events go through the on_event handler
// for better tracking and debugging
void NetworkLte::on_event(NetworkLteEvent event) {
    event_ = event;
    log_event();
    if (is_error_event(event)) {
        log_error(event);
        push_trace(NetworkTraceKind::error, NetworkLteState::none, NetworkLteState::none, event, ModemAction::none);
    }
}

NetworkLteEvent NetworkLte::get_event() {
    NetworkLteEvent current_event = event_;
    event_ = NetworkLteEvent::none; // reset event after reading it to avoid processing the same event multiple times,
                                    // if we want to keep a history of events, we can store them in a vector instead of
                                    // resetting it
    return current_event;
}

void NetworkLte::call_action(ModemAction action) {
    modem_action_ = action;
    log_action();
}

ModemAction NetworkLte::get_action() {
    ModemAction action = modem_action_;
    modem_action_ = ModemAction::none; // reset action after reading it, since actions are one-time triggers that should
                                       // be executed once and then cleared until the next time they are triggered
    return action;
}

static const char* state_to_str(NetworkLteState s) {
    switch (s) {
        case NetworkLteState::switched_off: return "switched_off";
        case NetworkLteState::off_mode: return "off_mode";
        case NetworkLteState::sleep_mode: return "sleep_mode";
        case NetworkLteState::setup_mode: return "setup_mode";
        case NetworkLteState::idle_mode: return "idle_mode";
        case NetworkLteState::network_detached: return "network_detached";
        case NetworkLteState::network_attaching: return "network_attaching";
        case NetworkLteState::pdp_context_closed: return "pdp_context_closed";
        case NetworkLteState::pdp_context_opening: return "pdp_context_opening";
        case NetworkLteState::data_ready: return "data_ready";
        case NetworkLteState::modem_fota: return "modem_fota";
        case NetworkLteState::transparent_mode: return "transparent_mode";
        case NetworkLteState::done: return "done";
        default: return "unknown";
    }
}

static const char* event_to_str(NetworkLteEvent e) {
    switch (e) {
        case NetworkLteEvent::none: return "none";
        // modem urc events
        case NetworkLteEvent::psm_enter: return "psm_enter";
        case NetworkLteEvent::psm_exit: return "psm_exit";
        case NetworkLteEvent::network_detached: return "network_detached";
        case NetworkLteEvent::network_attached: return "network_attached";
        case NetworkLteEvent::context_opened: return "context_opened";
        case NetworkLteEvent::context_closed: return "context_closed";
        case NetworkLteEvent::context_rejected: return "context_rejected";
        case NetworkLteEvent::data_available: return "data_available";
        // Action completion events (generated by step())
        case NetworkLteEvent::attach_started: return "attach_started";
        case NetworkLteEvent::pdp_opening: return "pdp_opening";
        // // Asynchronous modem / server events
        case NetworkLteEvent::timeout: return "timeout";
        // Error events (generated by step() after retries exhausted)
        case NetworkLteEvent::network_error: return "network_error";
        case NetworkLteEvent::attach_error: return "attach_error";
        case NetworkLteEvent::context_error: return "context_error";
        default: return "unknown";
    }
}

static const char* modem_event_to_str(NetworkLteEvent e) {
    switch (e) {
        case NetworkLteEvent::psm_enter: return "psm_enter";
        case NetworkLteEvent::psm_exit: return "psm_exit";
        case NetworkLteEvent::network_detached: return "network_detached";
        case NetworkLteEvent::network_attached: return "network_attached";
        case NetworkLteEvent::context_opened: return "context_opened";
        case NetworkLteEvent::context_closed: return "context_closed";
        case NetworkLteEvent::context_rejected: return "context_rejected";
        case NetworkLteEvent::data_available: return "data_available";
        default: return "unknown_modem_event";
    }
}

static const char* action_to_str(ModemAction a) {
    switch (a) {
        case ModemAction::none: return "none";
        // recovering actions
        case ModemAction::reboot: return "reboot";
        case ModemAction::factory_reset: return "factory_reset";
        case ModemAction::check_responsiveness: return "check_responsiveness";
        // power events
        case ModemAction::power_on: return "power_on";
        case ModemAction::power_off: return "power_off";
        case ModemAction::turn_on_radio: return "turn_on_radio";
        case ModemAction::switch_off_radio: return "switch_off_radio";
        case ModemAction::enter_sleep: return "enter_sleep";
        case ModemAction::wake_up: return "wake_up";
        case ModemAction::setup_radio: return "setup_radio";
        // internal actions to get current state
        case ModemAction::query_network_status: return "query_network_status";
        case ModemAction::query_pdp_context: return "query_pdp_context";
        // internal actions to drive state machine forwared
        case ModemAction::attach_network: return "attach_network";
        case ModemAction::open_pdp_context: return "open_pdp_context";
        // data actions
        case ModemAction::send_data: return "send_data";
        case ModemAction::read_data: return "read_data";
        case ModemAction::data_complete: return "data_complete";
        // sepecial modes
        case ModemAction::enter_transparent_mode: return "enter_transparent_mode";
        case ModemAction::leave_transparent_mode: return "leave_transparent_mode";
        default: return "unknown_action";
    }
}

void NetworkLte::log_state() {
    NETWORK_LOG_DBG("new state: %s", state_to_str(state_));
    push_trace(NetworkTraceKind::state_change, prev_state_, state_, NetworkLteEvent::none, ModemAction::none);
}

void NetworkLte::log_event() {
    NETWORK_LOG_DBG("new event: %s", event_to_str(event_));
    push_trace(NetworkTraceKind::event_set, NetworkLteState::none, NetworkLteState::none, event_, ModemAction::none);
}

void NetworkLte::log_action() {
    if (modem_action_ != ModemAction::none) NETWORK_LOG_DBG("new action: %s", action_to_str(modem_action_));
    push_trace(NetworkTraceKind::action_set, NetworkLteState::none, NetworkLteState::none, NetworkLteEvent::none,
               modem_action_);
}

void NetworkLte::log_error(NetworkLteEvent error_event) {
    NETWORK_LOG_ERR("new error event: %s", event_to_str(error_event));
}

void NetworkLte::push_trace(NetworkTraceKind kind, NetworkLteState previous_state, NetworkLteState current_state,
                            NetworkLteEvent event, ModemAction action) {
    std::scoped_lock lock(trace_mutex_);

    NetworkTraceEntry entry;
    entry.kind = kind;
    entry.previous_state = previous_state;
    entry.current_state = current_state;
    entry.event = event;
    entry.action = action;

    size_t write_index = 0;
    if (trace_count_ < TRACE_CAPACITY) {
        write_index = (trace_head_ + trace_count_) % TRACE_CAPACITY;
        trace_count_++;
    } else {
        write_index = trace_head_;
        trace_head_ = (trace_head_ + 1) % TRACE_CAPACITY;
    }

    trace_buffer_[write_index] = entry;
    trace_cv_.notify_one();
}

bool NetworkLte::pop_trace(NetworkTraceEntry& entry, uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(trace_mutex_);
    const auto has_data = [this]() { return trace_count_ > 0; };

    if (timeout_ms == 0) {
        if (!has_data()) {
            return false;
        }
    } else {
        if (!trace_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), has_data)) {
            return false;
        }
    }

    entry = trace_buffer_[trace_head_];
    trace_head_ = (trace_head_ + 1) % TRACE_CAPACITY;
    trace_count_--;
    return true;
}

// --- Timer ---

TimerError NetworkLte::start_timer(uint32_t timeout_ms) {
    if (!timer_) {
        return TimerError::not_running;
    }
    return timer_->start(timeout_ms, [this]() { on_timer_expired(); });
}

TimerError NetworkLte::stop_timer() {
    if (!timer_) {
        return TimerError::not_running;
    }
    return timer_->stop();
}

TimerError NetworkLte::reset_timer(uint32_t timeout_ms) {
    if (!timer_) {
        return TimerError::not_running;
    }
    return timer_->reset(timeout_ms);
}

bool NetworkLte::is_timer_running() const {
    return timer_ && timer_->is_running();
}

uint32_t NetworkLte::timer_elapsed_seconds() const {
    if (!timer_) {
        return 0;
    }
    return timer_->elapsed_ms() / 1000U;
}

void NetworkLte::on_timer_expired() {
    on_event(NetworkLteEvent::timeout);
}

bool NetworkLte::check_status_response(ModemStatus status) {
    if (status == ModemStatus::ok) {
        return true;
    } else {
        if (status == ModemStatus::timeout) {
            NETWORK_LOG_ERR("No response from modem, it might be unresponsive or powered off");
            on_event(NetworkLteEvent::at_command_no_response); // flag a generic network error for now, we can have more
                                                               // specific error events if needed (e.g. attach_error,
                                                               // context_error, etc.) and trigger them based on the
                                                               // current state and action that was being performed when
                                                               // the error occurred
        } else {
            NETWORK_LOG_ERR("Modem command failed with status: %d", static_cast<int>(status));
        }
    }
    return false;
}

} // namespace modem
