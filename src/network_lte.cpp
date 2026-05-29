#include "modem/network_lte.h"
#include "modem/log.h"
#include "modem/timer_factory.h"

namespace modem {

NetworkLte::NetworkLte(xE310& modem, const NetworkLteConfig& config, DataReceivedCallback on_data_received,
                       std::unique_ptr<TimerInterface> timer)
    : modem_(modem), lteConfig(config), on_data_received_(std::move(on_data_received)),
      timer_(std::move(timer)) {
        st_timer = modem::create_platform_timer();
    }

// --- Accessors ---
const RegistrationInfo& NetworkLte::registration_info() const { return regInfo; }
const SignalQuality&    NetworkLte::signal_quality()    const { return signalQuality; }
const std::string&      NetworkLte::iccid()             const { return modemInfo.iccid; }
const std::string&      NetworkLte::imsi()              const { return modemInfo.imsi; }
const NetworkLteConfig& NetworkLte::config()            const { return lteConfig; }
void NetworkLte::set_config(const NetworkLteConfig& config) { lteConfig = config; }

NetworkLteState NetworkLte::state() const { return state_; }
NetworkLteEvent NetworkLte::event() const { return event_; }

uint8_t NetworkLte::get_network_attempts() const { return nNetworkAttempts; }
void NetworkLte::set_network_attempts(uint8_t n) { nNetworkAttempts = n; }
uint8_t NetworkLte::get_attach_retries() const { return nAttachRetries; }
uint8_t NetworkLte::get_pdp_retries() const { return nPdpRetries; }
void NetworkLte::set_attach_retries(uint8_t n) { nAttachRetries = n; }
void NetworkLte::set_pdp_retries(uint8_t n) { nPdpRetries = n; }

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
    return timer_->elapsed_ms() / 1000u;
}

void NetworkLte::on_timer_expired() {
    on_modem_event(NetworkLteEvent::timeout);
}

bool NetworkLte::network_connect() {
    if(state_ == NetworkLteState::data_ready){
        MODEM_LOG_INF("Already connected to network");
        return true; // already connected to network
    }
    go_to_state(NetworkLteState::data_ready); // trigger attach flow in idle mode
    if(state_ == NetworkLteState::data_ready){
        MODEM_LOG_INF("Successfully connected to network");
        return true;
    } else {
        MODEM_LOG_ERR("Failed to connect to network");
        return false;
    }
}
void NetworkLte::new_connection(uint8_t conn_id, const std::string& protocol, const std::string& ip, const std::string& port){
    serverInfo[conn_id-1].state = ServerState::disconnected;
    serverInfo[conn_id-1].protocol = protocol;
    serverInfo[conn_id-1].address = ip;
    serverInfo[conn_id-1].port = std::stoi(port);
}

bool NetworkLte::server_connect(uint8_t conn_id, const std::string& protocol, const std::string& ip, const uint16_t port) {
    while(state_ != NetworkLteState::data_ready) {
        go_to_state(NetworkLteState::data_ready);
    }
    if(state_ == NetworkLteState::data_ready && serverInfo[conn_id-1].state != ServerState::connected){
        if(serverInfo[conn_id-1].state == ServerState::connected){
            MODEM_LOG_ERR("Already connected to a server, cannot connect to a different one without disconnecting first");
            return false; // already connected to a server, cannot connect to a different one without disconnecting first
        }
        if(protocol == "UDP"){
            MODEM_LOG_INF("Attempting to connect to UDP server at %s:%d", ip.c_str(), port);
            auto status = modem_.udp_open(conn_id, ip, port);
            if(status != ModemStatus::ok){
                MODEM_LOG_ERR("Failed to connect to server at %s:%d", ip.c_str(), port);
                return false;
            }else{
                serverInfo[conn_id-1].state = ServerState::connected;
                serverInfo[conn_id-1].protocol = protocol;
                serverInfo[conn_id-1].address = ip;
                serverInfo[conn_id-1].port = port;
                MODEM_LOG_INF("Successfully connected to server at %s:%d", ip.c_str(), port);
                return true;
            }
        }else if(protocol == "TCP"){
            MODEM_LOG_WRN("TCP protocol is not currently supported, cannot connect to server at %s:%d", ip.c_str(), port);
            return false; // TCP not supported, cannot connect
        }else{
            MODEM_LOG_ERR("Unknown protocol for server connection, cannot connect to server at %s:%d", ip.c_str(), port);
            return false; // unknown protocol, cannot connect
        }
    } else {
        MODEM_LOG_ERR("Failed to connect to network, cannot connect to server");
        return false;
    }
}

bool NetworkLte::server_disconnect(uint8_t conn_id) {
    if(serverInfo[conn_id-1].state != ServerState::connected){
        MODEM_LOG_ERR("Not currently connected to a server, cannot disconnect");
        return false; // not connected to server, cannot disconnect
    }
    if(serverInfo[conn_id-1].protocol == "UDP"){
        MODEM_LOG_INF("Attempting to disconnect from UDP server at %s:%d", serverInfo[conn_id-1].address.c_str(), serverInfo[conn_id-1].port);
        auto status = modem_.udp_close(conn_id);
        if(status != ModemStatus::ok){
            MODEM_LOG_ERR("Failed to disconnect from UDP server for CID %d", conn_id);
            return false;
        }else{
            serverInfo[conn_id-1].state = ServerState::disconnected;
            MODEM_LOG_INF("Successfully disconnected from UDP server for CID %d", conn_id);
            return true;
        }
    } else if(serverInfo[conn_id-1].protocol == "TCP"){
        MODEM_LOG_WRN("TCP protocol is not currently supported, cannot disconnect from server at %s:%d", serverInfo[conn_id-1].address.c_str(), serverInfo[conn_id-1].port);
        return false; // TCP not supported, cannot disconnect
    } else {
        MODEM_LOG_ERR("Unknown protocol for server connection, cannot disconnect");
        return false;
    }
}

bool NetworkLte::send_data(uint8_t conn_id, uint8_t* data, size_t length) { 
     if(state_ != NetworkLteState::data_ready && state_ != NetworkLteState::sleep_mode) {
        MODEM_LOG_INF("Not currently in data ready or sleep mode, attempting to go to data ready mode before sending data");
        go_to_state(NetworkLteState::data_ready);
    }
    if( state_ != NetworkLteState::data_ready) {
        MODEM_LOG_ERR("Not currently connected to network, cannot send data");
        return false;
    }
    if( serverInfo[conn_id-1].state != ServerState::connected){
        MODEM_LOG_ERR("Not currently connected to a server, cannot send data");
        return false; // not connected to server, cannot send data
    }
    auto status = modem_.udp_send(conn_id, std::vector<uint8_t>(data, data + length));
    if(status != ModemStatus::ok){
        MODEM_LOG_ERR("Failed to send data to UDP server for CID %d", conn_id);
        return false;
    }
    return true;
}

bool NetworkLte::enter_sleep() {
    if(state_ != NetworkLteState::sleep_mode && state_ != NetworkLteState::off_mode && state_ != NetworkLteState::switched_off) {
        go_to_state(NetworkLteState::sleep_mode);
    }else{
        MODEM_LOG_INF("Already in sleep mode or off mode");
        return true; // already in sleep or off mode, consider it a success
    }
    if(state_ != NetworkLteState::sleep_mode && state_ != NetworkLteState::off_mode && state_ != NetworkLteState::switched_off) {
        MODEM_LOG_ERR("Failed to enter sleep mode or off mode");
        return false; // failed to enter sleep mode
    }
    MODEM_LOG_INF("Successfully entered sleep mode");
    return true; // in off mode or switched off, consider it a success
}

bool NetworkLte::enter_transparent_mode() {
    if( state_ != NetworkLteState::transparent_mode) {
        go_to_state(NetworkLteState::transparent_mode);
    }
    if(state_ == NetworkLteState::transparent_mode){
        MODEM_LOG_INF("Successfully entered transparent mode");
        return true;
    }
    return false;    
}

bool NetworkLte::exit_transparent_mode() {
    if( state_ != NetworkLteState::transparent_mode) {
        MODEM_LOG_ERR("Not currently in transparent mode, cannot exit");
        return false; // not in transparent mode, cannot exit
        
    }
    go_to_state(NetworkLteState::idle_mode); // after exiting transparent mode, go back to idle mode to restart normal flow
    if(state_ != NetworkLteState::transparent_mode){
        MODEM_LOG_INF("Successfully exited transparent mode");
        return true;
    }
    return false;    
}

bool NetworkLte::send_at_command(std::string command, std::string& response, uint16_t timeout_ms) {
    // send command
    if(state_ != NetworkLteState::transparent_mode){
        MODEM_LOG_ERR("Modem is not in transparent mode, cannot send AT command");
        return false;
    }
    auto status = modem_.send_at_command(command, response, timeout_ms);
    if (status != ModemStatus::ok) {
        MODEM_LOG_ERR("Failed to send AT command: %s", command.c_str());
        return false;
    }
    return true;
}

bool NetworkLte::update_modem(std::string firmware_url){
    if(state_ != NetworkLteState::modem_fota) {
        go_to_state(NetworkLteState::modem_fota);
    }
    if(state_ != NetworkLteState::modem_fota){
        MODEM_LOG_ERR("Failed to enter modem FOTA mode");
        return false;
    }else{
        // send AT commands to perform FOTA update, e.g. set server, start download, etc.
        MODEM_LOG_INF("Successfully entered modem FOTA mode, starting update process");
        MODEM_LOG_WRN("Note: FOTA update commands are not implemented");
        fFirstTimeBoot = true; // after FOTA, we can consider the next boot as the first one to re-apply configuration
        go_to_state(NetworkLteState::setup_mode); // after FOTA, go back to setup mode to re-apply configuration and re-attach to network
        return true;
    }
}

// ---------------------------------------------------------------------------
// loop — state-transition logic
// ---------------------------------------------------------------------------
NetworkLteState NetworkLte::loop(NetworkLteState target_state) {
    // Poll for unsolicited result codes before processing the current state
    for (const auto& urc : modem_.poll_urc()) {
        handle_urc(urc);
    }

    if(state_ == target_state && target_state != NetworkLteState::none){
        return state_; // already in target state, no need to process further
    }

    // user desires takes precence over modem events, so we can trigger state transitions based on the target state even if we haven't received the corresponding modem events yet (e.g. we can trigger transparent mode entry before receiving the URC that confirms we are in transparent mode, and then when we receive that URC in the next loop cycle, we will already be in the correct state and can just continue with normal processing)
    switch(target_state){
        case NetworkLteState::data_ready:
            // do nothing here, just let the normal event flow handle the transition to data ready state
             break;
        case NetworkLteState::transparent_mode:
            if(state_ != NetworkLteState::transparent_mode){
                on_event(NetworkLteEvent::transparent_mode); // trigger transparent mode entry
            }
            break;
        case NetworkLteState::sleep_mode:
            // how can we force the modem to enter sleep mode (PSM) ? is there a specific command we can send to trigger it, or do we just need to wait for the network to trigger it based on our PSM configuration ?
            // for now, we will just rely on the normal event flow to enter sleep mode when
            on_event(NetworkLteEvent::enter_sleep); // trigger PSM event to check if we can enter sleep mode
            break;
        case NetworkLteState::switched_off:
            on_event(NetworkLteEvent::power_off); // trigger power off to enter switched off state
            break;
        case NetworkLteState::off_mode:
            on_event(NetworkLteEvent::switch_off_radio); // trigger radio off to enter off mode
            break;
        default:
            break; // otherwise, just process events and update state as they come
    }

    // modem events taken precedence over state-driven events (e.g. timeouts, attach completion)
    switch(modem_event_){
        case NetworkLteEvent::psm_enter:
            MODEM_LOG_INF("Entered PSM mode");
            change_state(NetworkLteState::sleep_mode);
            break;
        case NetworkLteEvent::psm_exit: 
            MODEM_LOG_INF("Exited PSM mode");
            if(prev_state_ == NetworkLteState::data_ready)
                change_state(NetworkLteState::data_ready); // assume we were in sleep mode with an active connection, we can go directly to data ready state
            else{
                change_state(NetworkLteState::idle_mode); // otherwise go back to idle and restart attach flow
                on_event(NetworkLteEvent::query_network_status); // trigger attach flow
            }
            break;
        case NetworkLteEvent::network_detached:
            MODEM_LOG_ERR("Network PDP detached, no longer registered to network");
            change_state(NetworkLteState::network_detached);
            break;
        case NetworkLteEvent::network_attached:
            MODEM_LOG_INF("Network PDP attached, registered to network");
            change_state(NetworkLteState::pdp_context_closed); // after network attach, we can assume any previous PDP context is now deactivated, so we can go to context deactivated state and trigger PDP activation flow from there
            break;
        case NetworkLteEvent::context_closed:
            MODEM_LOG_ERR("PDP context closed by network");
            change_state(NetworkLteState::network_detached); // after context is closed, we can assume we are detached from network, so we can go to detached state and restart attach flow from there
            break;
        case NetworkLteEvent::context_opened:
            MODEM_LOG_INF("Network PDP context opened, IP address assigned: %s", networkInfo.ip_address.c_str());
            change_state(NetworkLteState::data_ready);
            break;
        case NetworkLteEvent::data_available:
            MODEM_LOG_INF("Data available event received");
            if(on_data_received_){
                if (on_data_received_) {
                    std::vector<uint8_t> buf;
                    if (modem_.udp_receive(lteConfig.conn_id, buf) == ModemStatus::ok && !buf.empty()) {
                        std::string payload(buf.begin(), buf.end());
                        on_data_received_(lteConfig.conn_id, payload, static_cast<uint16_t>(buf.size()));
                    }
                }
            }
            break;
        default:
            break; // other events are handled in the state switch below
    }

    // forced events by user
    switch (event_){
        case NetworkLteEvent::transparent_mode:
            MODEM_LOG_INF("Transparent mode event triggered");
            change_state(NetworkLteState::transparent_mode);
             break;
            MODEM_LOG_INF("Wake up event triggered");
            break;
        case NetworkLteEvent::send_data:
            MODEM_LOG_INF("Send data event triggered");
            break;
        default:
            break; // other events are handled in the state switch below
    }

    // normal workflow events
    switch (state_) {

        case NetworkLteState::switched_off:
            if (event_ == NetworkLteEvent::power_on) {
                // power on modem
                //modem_.power_on();
                modem_.set_baudrate(115200);
                modem_.set_echo(false);
                // check if modem is responsive            AtResponse response;
                auto status = modem_.at_ok();
                if (status != ModemStatus::ok) {
                    MODEM_LOG_ERR("Failed to power on modem");
                    change_state(NetworkLteState::switched_off);
                    break;
                }
                //fFirstTimeBoot = true; // after powering on, we can consider the next boot as the first one to apply configuration
                change_state(NetworkLteState::idle_mode);
                if(fNewConfig){
                    on_event(NetworkLteEvent::setup_radio); // start setup flow to apply new config
                    fNewConfig = false;
                }else{
                    // wait for URCs to populate modem state (e.g. registration status) before deciding whether to start attach flow or not
                    //event_ = NetworkLteEvent::query_network_attached; // start attach flow
                }
                nNetworkAttempts = 0;
            }
            break;

        case NetworkLteState::off_mode:
            if (event_ == NetworkLteEvent::turn_on_radio) {
                // which command should I send to leave the off mode ?
                change_state(NetworkLteState::idle_mode);
                on_event(NetworkLteEvent::query_network_status); // start attach flow
                nNetworkAttempts = 0;
            }
            break;

        case NetworkLteState::sleep_mode:
            if (event_ == NetworkLteEvent::wake_up) {
                // do I need to send any command to wake up the modem ?
                if(prev_state_ == NetworkLteState::data_ready)
                    change_state(NetworkLteState::data_ready); // assuming we were in sleep mode with an active connection, we can go directly to data ready state
                else{
                    change_state(NetworkLteState::idle_mode); // otherwise go back to idle and restart attach flow
                    on_event(NetworkLteEvent::query_network_status); // trigger attach flow
                }
                nNetworkAttempts = 0;
            }
            break;
            
        case NetworkLteState::idle_mode:

            //modem_.read_iccid(modemInfo.iccid);
            //modem_.read_imsi(modemInfo.imsi);
            
            switch (event_) {
                case NetworkLteEvent::setup_radio:
                    change_state(NetworkLteState::setup_mode);
                    break;
                case NetworkLteEvent::query_network_status: {
                    auto status = modem_.get_registration_status(regInfo);
                    if(status != ModemStatus::ok){
                        MODEM_LOG_ERR("Failed to get registration status");
                        // keep on the same state to retry later
                        break;
                    } else {
                        if(regInfo.stat == RegStatus::denied ||regInfo.stat == RegStatus::not_registered || regInfo.stat == RegStatus::unknown){
                            nNetworkAttempts++;
                            nAttachRetries = 0; // reset attach retries when we have a new network attempt
                            nPdpRetries = 0; // reset PDP retries when we have a new network attempt
                            change_state(NetworkLteState::network_detached); // start attach with fallback config
                            on_event(NetworkLteEvent::attach_started); // trigger attach flow
                            break;
                        }
                        if(regInfo.stat == RegStatus::registered_home || regInfo.stat == RegStatus::registered_roaming){
                            on_event(NetworkLteEvent::query_network_context); // trigger attach flow
                            break;
                        }
                    }
                    break;
                }
                case NetworkLteEvent::query_network_context: {
                    auto status = modem_.get_ip_address(lteConfig.cid, networkInfo.ip_address);
                    if (status != ModemStatus::ok) {
                        // flag error
                        break;
                    }else{
                        if(networkInfo.ip_address.empty()){
                            if(nNetworkAttempts >= lteConfig.max_network_attempts){
                                change_state(NetworkLteState::done);
                                on_event(NetworkLteEvent::switch_off_radio); // power off modem
                                break;
                            }
                            MODEM_LOG_ERR("Registered but no IP address assigned");
                            change_state(NetworkLteState::pdp_context_closed); // try to open PDP context to get IP
                            on_event(NetworkLteEvent::pdp_opening); // trigger PDP activation flow
                            break;
                        } else {
                            MODEM_LOG_DBG("Already registered with IP: %s", networkInfo.ip_address.c_str());
                            change_state(NetworkLteState::data_ready); // already have IP, we can go directly to data ready state
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            break;

        case NetworkLteState::setup_mode:
            // get modem info
            if( fFirstTimeBoot){
                fFirstTimeBoot = false;
                auto status = modem_.request_imei_sv(modemInfo.imei_sv);
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
                status = modem_.set_psm_urc(true);
                if (status != ModemStatus::ok) {
                    // flag error
                }
                status = modem_.set_registration_urc(true);
                if (status != ModemStatus::ok) {
                    // flag error
                }
                status = modem_.set_pdp_urc(true);
                if (status != ModemStatus::ok) {
                    // flag error
                }
            }
            // get modem configuration (bands, iot tech, apn) and apply it
            
            //modem_.set_lte_bands(lteConfig.default_lte_bands);
            //modem_.set_iot_technology(lteConfig.default_iot_tech);
            modem_.set_apn(1, lteConfig.default_apn);
            //modem_.set_psm(cfg);
            modem_.set_telit_psm(telitCpsmsConfig);
            
            change_state(NetworkLteState::idle_mode);
            on_event(NetworkLteEvent::query_network_status); // check network status
            break;

        case NetworkLteState::network_detached:
            if (event_ == NetworkLteEvent::attach_started) {
                if(nAttachRetries >= lteConfig.max_attach_retries){
                    change_state(NetworkLteState::done);
                    on_event(NetworkLteEvent::attach_error); // flag attach error to trigger power off in the next step
                    break;
                }
                else if(nAttachRetries == 0){
                    //modem_.set_lte_bands(lteConfig.default_lte_bands);
                    //modem_.set_iot_technology(lteConfig.default_iot_tech);
                    modem_.set_operator_manual(lteConfig.plmn, RadioTech::lte);
                }else{
                    //modem_.set_lte_bands(lteConfig.fallback_lte_bands);
                    //modem_.set_iot_technology(lteConfig.fallback_iot_tech);
                    modem_.set_operator_auto();
                }
                nAttachRetries++;
                change_state(NetworkLteState::network_attaching); // trigger attach flow
            }
            break;

        case NetworkLteState::network_attaching:
            // wait for network to attach, triggered by +creg URC
            if(event_ == NetworkLteEvent::network_attached) {
                //nAttachRetries = 0; // do not reset attach retries after successful attach, endless loop if network is failing
                change_state(NetworkLteState::pdp_context_closed); // start PDP activation flow
                on_event(NetworkLteEvent::pdp_opening);
            } else if (event_ == NetworkLteEvent::timeout) {
                change_state(NetworkLteState::network_detached); // retry attach
            } else if (event_ == NetworkLteEvent::network_detached) {
                change_state(NetworkLteState::network_detached); // retry attach
            }
            break;

        case NetworkLteState::pdp_context_closed:
            if (event_ == NetworkLteEvent::pdp_opening) {
                if(nPdpRetries >= lteConfig.max_pdp_retries){
                    change_state(NetworkLteState::done);
                    on_event(NetworkLteEvent::context_error); // flag context error to trigger power off in the next step
                    break;
                }else if(nPdpRetries == 0){
                    modem_.activate_pdp(lteConfig.cid);
                } else {
                    // try to recover by re-applying APN and other PDP settings before re-activating
                    modem_.set_apn(1, lteConfig.fallback_apn);
                    on_event(NetworkLteEvent::pdp_opening); // trigger PDP activation flow again
                }
                nPdpRetries++;
                change_state(NetworkLteState::pdp_context_opening);
            } else if (event_ == NetworkLteEvent::network_detached) {
                change_state(NetworkLteState::network_detached);
            }
            break;

        case NetworkLteState::pdp_context_opening:
            if (event_ == NetworkLteEvent::context_opened) {
                change_state(NetworkLteState::data_ready);
            } else if (event_ == NetworkLteEvent::context_closed ||
                    event_ == NetworkLteEvent::timeout ||
                    event_ == NetworkLteEvent::context_rejected) {
                if (nNetworkAttempts < lteConfig.max_pdp_retries) {
                    change_state(NetworkLteState::pdp_context_closed); // trigger attach flow again
                } else {
                    change_state(NetworkLteState::idle_mode); // trigger attach flow again
                }
            } else if (event_ == NetworkLteEvent::network_detached) {
                change_state(NetworkLteState::network_detached);
            }
            break;

        case NetworkLteState::data_ready:
            switch(event_){
                case NetworkLteEvent::data_complete:
                    change_state(NetworkLteState::done);
                    on_event(NetworkLteEvent::psm_enter); // power off modem to reset state and retry later
                    break;
                case NetworkLteEvent::network_detached:
                    change_state(NetworkLteState::network_detached);
                    break;
                case NetworkLteEvent::context_closed:
                    change_state(NetworkLteState::pdp_context_closed);
                    break;
                default:
                    break;
            }
            break;

        case NetworkLteState::done:
            switch (event_) {
                case NetworkLteEvent::enter_sleep: // check if is needed !!
                    change_state(NetworkLteState::sleep_mode);
                    break;
                case NetworkLteEvent::switch_off_radio: {
                    auto state = modem_.shutdown();
                    if(state != ModemStatus::ok){
                        MODEM_LOG_ERR("Failed to shutdown modem");
                        on_event(NetworkLteEvent::power_off);
                        break;
                    }
                    change_state(NetworkLteState::off_mode);
                    break;
                }
                case NetworkLteEvent::power_off:
                    modem_.power_off();
                    change_state(NetworkLteState::switched_off);
                    break;
                case NetworkLteEvent::timeout:
                    MODEM_LOG_ERR("Operation timed out in done state, check what to do..");
                    break;
                case NetworkLteEvent::attach_error:
                    modem_.power_off();
                    change_state(NetworkLteState::switched_off);
                    break;
                case NetworkLteEvent::context_error:
                    modem_.power_off();
                    change_state(NetworkLteState::switched_off);
                default:
                    break;
            }
    }

    return state_;
}

void NetworkLte::handle_urc(const std::string& urc) {
    // +CREG / +CEREG: <n>,<stat>  or  <stat>
    if (urc.rfind("+CREG:", 0) == 0 || urc.rfind("+CEREG:", 0) == 0) {
        // stat field is the last token (after optional <n>,)
        int stat = -1;
        auto comma = urc.rfind(',');
        auto colon = urc.find(':');
        std::string val = (comma != std::string::npos)
            ? urc.substr(comma + 1)
            : urc.substr(colon + 1);
        // strip whitespace
        while (!val.empty() && (val.front() == ' ' || val.front() == '\r')) val.erase(0, 1);
        if (!val.empty()) stat = val.front() - '0';
        if (stat == 1 || stat == 5) {          // registered home / roaming
            on_event(NetworkLteEvent::network_attached);
        } else if (stat == 0 || stat == 3) {   // not registered / denied
            on_event(NetworkLteEvent::network_detached);
        }
        return;
    }
    // +CGEV: various PDP/PS events
    if (urc.rfind("+CGEV:", 0) == 0) {
        // body is everything after "+CGEV:" with leading space stripped
        std::string body = urc.substr(6);
        auto s = body.find_first_not_of(' ');
        const std::string ev = (s == std::string::npos) ? "" : body.substr(s);

        if (ev.rfind("NW_DEACT", 0) == 0 ||   // network forced context deactivation
            ev.rfind("ME DEACT",  0) == 0) {   // ME forced context deactivation
            on_event(NetworkLteEvent::context_closed);
        } else if (ev.rfind("NW_DETACH", 0) == 0 ||  // network PS detach (all contexts lost)
                   ev.rfind("ME_DETACH",  0) == 0) {  // ME PS detach (all contexts lost)
            on_event(NetworkLteEvent::network_detached);
        } else if (ev.rfind("REJECT", 0) == 0) {     // context activation rejected
            on_event(NetworkLteEvent::context_rejected);
        }
        // NW REACT (network requesting reactivation) — no action needed
        return;
    }
    // #PSMURC: <ActiveTime>,<PSMTime>  →  modem entered PSM
    if (urc.rfind("#PSMURC:", 0) == 0) {
        on_event(NetworkLteEvent::psm_enter);
        return;
    }
    // SRING: <conn_id>  →  new data available on socket
    if (urc.rfind("SRING:", 0) == 0) {
        std::string body = urc.substr(6);
        auto s = body.find_first_not_of(' ');
        if (s != std::string::npos) {
            int id = std::stoi(body.substr(s));
            if (id == lteConfig.conn_id) {
                on_event(NetworkLteEvent::data_available);
            }
        }
        return;
    }
}

void NetworkLte::on_modem_event(NetworkLteEvent event) {    
    modem_event_ = event;
    // loop();
    // record uptime and event history for debugging/analytics
}
// replace all occurrences of event_ = with on_event(event) to ensure that all events go through the on_event handler for better tracking and debugging
void NetworkLte::on_event(NetworkLteEvent event) {    
    event_ = event;
    // loop();
    // record uptime and event history for debugging/analytics
}

bool NetworkLte::go_to_state(NetworkLteState target_state) {

    if(state_ == target_state) {
        return true; // already in target state
    }
    
    // This function generates the necessary sequence of events to transition from the current state to the target state.
    // For simplicity, we will just call loop() in a loop until we reach the target state, relying on the fact that loop() will process events and update the state accordingly.
    // In a more complex implementation, we could have a predefined map of required events for each state transition to speed this up and avoid unnecessary iterations.
    switch(state_){
        case NetworkLteState::switched_off:
            if(target_state == NetworkLteState::data_ready || target_state == NetworkLteState::transparent_mode || target_state == NetworkLteState::network_detached){
                on_event(NetworkLteEvent::power_on); // trigger power on to start setup/attach flow             
            }
            break;
        case NetworkLteState::off_mode:
            if(target_state == NetworkLteState::idle_mode || target_state == NetworkLteState::setup_mode || target_state == NetworkLteState::network_detached){
                on_event(NetworkLteEvent::turn_on_radio); // trigger turn on radio to start attach flow
            }
            break;
        case NetworkLteState::sleep_mode:
            if(target_state == NetworkLteState::data_ready){
                on_event(NetworkLteEvent::wake_up); // trigger wake up to go back to data ready state
            }
            break;
        default:
            break; // for other states, we will rely on the normal event flow to transition to the target state
    }

    while (state_ != target_state) {
        loop(target_state); // process events and update state until we reach the target state
        // In a real implementation, we might want to add a timeout here to avoid infinite loops in case of unexpected conditions.
        // if timeout reached:
        //     MODEM_LOG_ERR("Timeout while trying to transition from %s to %s", state_to_str(state_), state_to_str(target_state));
        //     return false;
    }
    return true;
}

// called from state_machine
void NetworkLte::change_state(NetworkLteState new_state) {

    if(new_state == state_){
        return; // no state change
    }
    prev_state_ = state_;
    state_ = new_state;
    
    log_state();
    switch(state_){
        case NetworkLteState::switched_off:
            MODEM_LOG_INF("Modem switched off");
            break;
        case NetworkLteState::off_mode:
            MODEM_LOG_INF("Modem in off mode (radio off)");
            break;
        case NetworkLteState::sleep_mode:
            MODEM_LOG_INF("Modem in sleep mode (PSM)");
            break;
        case NetworkLteState::setup_mode:
            MODEM_LOG_INF("Modem in setup mode (applying configuration)");
            break;
        case NetworkLteState::idle_mode:
            MODEM_LOG_INF("Modem in idle mode (powered on, waiting for attach)");
            if(nNetworkAttempts == 0){
                MODEM_LOG_INF("Attach attempt %d", nNetworkAttempts);
                MODEM_LOG_INF("Applying default configuration: APN=%s", lteConfig.default_apn.c_str());
            } else if(nNetworkAttempts == 1){
                MODEM_LOG_INF("Attach attempt %d", nNetworkAttempts);
                MODEM_LOG_INF("Applying fallback configuration: APN=%s", lteConfig.fallback_apn.c_str());
                MODEM_LOG_WRN("Change eSim profile");
            }else{
                MODEM_LOG_ERR("Attach attempts reached maximum: %d", nNetworkAttempts);
            }
            break;
        case NetworkLteState::network_detached:
            {
                MODEM_LOG_INF("Modem detached from network");
                uint8_t i = 0;
                while(i < MAX_SERVER_CONNECTIONS){
                    serverInfo[i++].state = ServerState::disconnected;
                }
            }
            break;
        case NetworkLteState::network_attaching:
            MODEM_LOG_INF("Modem network_attaching to network");
            // launch timer to monitor attach timeout and trigger retry if needed
            break;
        case NetworkLteState::pdp_context_closed:
            MODEM_LOG_INF("Modem registered but PDP context not active");
            {
                uint8_t i = 0;
                while(i < MAX_SERVER_CONNECTIONS){
                    serverInfo[i++].state = ServerState::disconnected;
                }
            }
            break;
        case NetworkLteState::pdp_context_opening:
            MODEM_LOG_INF("Modem opening PDP context");
            // launch timer to monitor PDP activation timeout and trigger retry if needed
            break;
        case NetworkLteState::data_ready:
            MODEM_LOG_INF("Modem ready to send/receive data");
            break;
        case NetworkLteState::done:
            MODEM_LOG_INF("Modem in done state (terminal state for current session)");
            break;
        case NetworkLteState::transparent_mode:
            // rut
            MODEM_LOG_INF("Modem in transparent mode (ready to receive AT commands)");
            modem_.set_psm_urc(false); // disable PSM URCs in transparent mode to avoid interfering with raw data reception
            modem_.set_registration_urc(false); // disable registration URCs in transparent mode to avoid interfering with raw data reception
            modem_.set_pdp_urc(false); // disable PDP URCs in transparent mode to avoid interfering with raw data reception
            break;
        case NetworkLteState::modem_fota:
            MODEM_LOG_INF("Modem in FOTA mode (ready to receive firmware update)");
            break;
        default:
            MODEM_LOG_WRN("Unknown modem state");
    }

    if(prev_state_ == NetworkLteState::transparent_mode){
        // re-enable URCs when exiting transparent mode to resume normal modem operation
        modem_.set_psm_urc(true);
        modem_.set_registration_urc(true);
        modem_.set_pdp_urc(true);
    }
}

// used for tests to trigger state transitions with specific events without relying on the full modem interaction flow
void NetworkLte::step() {
    loop();
}

static const char* state_to_str(NetworkLteState s) {
    switch (s) {
        case NetworkLteState::switched_off:        return "switched_off";
        case NetworkLteState::off_mode:            return "off_mode";
        case NetworkLteState::sleep_mode:          return "sleep_mode";
        case NetworkLteState::setup_mode:          return "setup_mode";
        case NetworkLteState::idle_mode:           return "idle_mode";
        case NetworkLteState::network_detached:    return "network_detached";
        case NetworkLteState::network_attaching:   return "network_attaching";
        case NetworkLteState::pdp_context_closed:  return "pdp_context_closed";
        case NetworkLteState::pdp_context_opening: return "pdp_context_opening";
        case NetworkLteState::data_ready:          return "data_ready";
        case NetworkLteState::modem_fota:          return "modem_fota";
        case NetworkLteState::transparent_mode:    return "transparent_mode";
        case NetworkLteState::done:                return "done";
        default:                                   return "unknown";
    }
}

static const char* event_to_str(NetworkLteEvent e) {
    switch (e) {
        case NetworkLteEvent::none:                      return "none";
        // Externa / user events
        case NetworkLteEvent::wake_up:                   return "wake_up";
        case NetworkLteEvent::send_data:                 return "send_data";
        case NetworkLteEvent::transparent_mode:         return "transparent_mode";
        case NetworkLteEvent::data_complete:             return "data_complete";
        // power events
        case NetworkLteEvent::power_on:                  return "power_on";
        case NetworkLteEvent::power_off:                 return "power_off";
        case NetworkLteEvent::turn_on_radio:             return "turn_on_radio";
        case NetworkLteEvent::switch_off_radio:          return "switch_off_radio";
        case NetworkLteEvent::enter_sleep:               return "enter_sleep";
        case NetworkLteEvent::setup_radio:               return "setup_radio";
        // modem urc events
        case NetworkLteEvent::psm_enter:                 return "psm_enter";
        case NetworkLteEvent::psm_exit:                  return "psm_exit";
        case NetworkLteEvent::network_detached:          return "network_detached";
        case NetworkLteEvent::network_attached:          return "network_attached";
        case NetworkLteEvent::context_opened:            return "context_opened";
        case NetworkLteEvent::context_closed:            return "context_closed";
        case NetworkLteEvent::context_rejected:          return "context_rejected";
        case NetworkLteEvent::data_available:            return "data_available";
        // Routing events (generated by step() in idle_mode)
        case NetworkLteEvent::query_network_status:      return "query_network_status";
        case NetworkLteEvent::query_network_context:     return "query_network_context";
        // Action completion events (generated by step())
        case NetworkLteEvent::attach_started:            return "attach_started";
        case NetworkLteEvent::pdp_opening:               return "pdp_opening";
        // // Asynchronous modem / server events
        case NetworkLteEvent::timeout:                   return "timeout";
        // Error events (generated by step() after retries exhausted)
        case NetworkLteEvent::network_error:             return "network_error";
        case NetworkLteEvent::attach_error:              return "attach_error";
        case NetworkLteEvent::context_error:             return "context_error";
        default:                                         return "unknown";
    }
}

void NetworkLte::log_state() const {
    MODEM_LOG_DBG("new state: %s", state_to_str(state_));
}

void NetworkLte::log_event() const {
    MODEM_LOG_DBG("new event: %s", event_to_str(event_));
}

} // namespace modem
