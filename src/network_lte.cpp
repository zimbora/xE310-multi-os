#include "modem/network_lte.h"
#include "modem/log.h"
#include "modem/timer_factory.h"
#include "modem/message_queue_factory.h"

namespace modem {

// Forward declarations of static helpers defined at bottom of file
static const char* action_to_str(ModemAction a);

NetworkLte::NetworkLte(xE310& modem, const NetworkLteConfig& config, DataReceivedCallback on_data_received,
                       std::unique_ptr<TimerInterface> timer)
    : modem_(modem), lteConfig(config), on_data_received_(std::move(on_data_received)),
      timer_(std::move(timer)) {
        st_timer = modem::create_platform_timer();
        message_queue_ = modem::create_platform_message_queue();
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
    on_event(NetworkLteEvent::timeout);
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
    
    if(state_ != NetworkLteState::data_ready && state_ != NetworkLteState::sleep_mode){
        MODEM_LOG_INF("Not currently in data ready mode or PSM, connect to the network first");
        return false; // not connected to network, cannot connect to server
    }

    MODEM_LOG_DBG("Attempting to connect to server with CID %d, protocol %s, IP %s, port %d", conn_id, protocol.c_str(), ip.c_str(), port); 
    if(state_ == NetworkLteState::data_ready ){
        uint8_t state = 0;
        MODEM_LOG_DBG("Checking current connection state for CID %d before connecting to server", conn_id);
        modem_.udp_status(conn_id, state);
        serverInfo[conn_id-1].state = static_cast<ServerState>(state);
        if(serverInfo[conn_id-1].state == ServerState::connected){
            MODEM_LOG_ERR("Already connected to a server, cannot connect to a different one without disconnecting first");
            return true; // already connected to a server, cannot connect to a different one without disconnecting first
        }
        if(protocol == "UDP"){
            MODEM_LOG_INF("Connecting to UDP server at %s:%d", ip.c_str(), port);
            auto status = modem_.udp_open(conn_id, ip, port);
            if(status != ModemStatus::ok){
                modem_.udp_close(conn_id); // ensure we close any half-open connection
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

QueueError NetworkLte::tx_write(uint8_t conn_id, const uint8_t* data, size_t length) {
    if (!message_queue_) return QueueError::invalid_id;
    return message_queue_->tx_push(conn_id, data, length);
}

QueueError NetworkLte::rx_read(uint8_t conn_id, QueueMessage& msg) {
    if (!message_queue_) return QueueError::invalid_id;
    return message_queue_->rx_pop(conn_id, msg);
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
        fColdBoot = true; // after FOTA, we can consider the next boot as the first one to re-apply configuration
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

    // !! only last event is processed for now!!
    // modem events change state without further action, actions are forbidden here for now!!
    switch(get_event()){
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
            }
            break;
        case NetworkLteEvent::network_detached:
            {
                MODEM_LOG_ERR("Network PDP detached, no longer registered to network");
                uint8_t i = 0;
                while(i < MAX_SERVER_CONNECTIONS){
                    serverInfo[i++].state = ServerState::disconnected;
                }
                change_state(NetworkLteState::network_detached);
            }
            break;
        case NetworkLteEvent::network_attached:
            MODEM_LOG_INF("Network PDP attached, registered to network");
            change_state(NetworkLteState::pdp_context_closed); // after network attach, we can assume any previous PDP context is now deactivated, so we can go to context deactivated state and trigger PDP activation flow from there
            break;
        case NetworkLteEvent::context_closed:
            {
                MODEM_LOG_ERR("PDP context closed by network");
                uint8_t i = 0;
                while(i < MAX_SERVER_CONNECTIONS){
                    serverInfo[i++].state = ServerState::disconnected;
                }
                change_state(NetworkLteState::network_detached); // after context is closed, we can assume we are detached from network, so we can go to detached state and restart attach flow from there
            }
            break;
        case NetworkLteEvent::context_opened:
            MODEM_LOG_INF("Network PDP context opened, IP address assigned: %s", networkInfo.ip_address.c_str());
            change_state(NetworkLteState::data_ready);
            break;
        case NetworkLteEvent::data_available: {
            MODEM_LOG_INF("Data available event received on conn_id %d", last_data_conn_id_);
            uint8_t conn = (last_data_conn_id_ > 0) ? last_data_conn_id_ : lteConfig.conn_id;
            std::vector<uint8_t> buf;
            if (modem_.udp_receive(conn, buf) == ModemStatus::ok && !buf.empty()) {
                // Push to RX queue for this connection
                if (message_queue_) {
                    auto err = message_queue_->rx_push(conn, buf.data(), buf.size());
                    if (err != QueueError::ok) {
                        MODEM_LOG_WRN("RX queue full for conn_id %d, dropping message", conn);
                    }
                }
                std::string payload(buf.begin(), buf.end());
                if (on_data_received_) {
                    on_data_received_(conn, payload, static_cast<uint16_t>(buf.size()));
                }
            }
            break;
        }
        default:
            break; // other events are handled in the state switch below
    }

    if(state_ == target_state && target_state != NetworkLteState::none){
        return state_; // already in target state, no need to process further
    }
    /*
    // user desires takes precence over modem events, so we can trigger state transitions based on the target state even if we haven't received the corresponding modem events yet (e.g. we can trigger transparent mode entry before receiving the URC that confirms we are in transparent mode, and then when we receive that URC in the next loop cycle, we will already be in the correct state and can just continue with normal processing)
    switch(target_state){
        case NetworkLteState::data_ready:
            switch(state_){
                // stationary states that we can trigger actions from to go to data ready state
                case NetworkLteState::switched_off:
                    call_action(ModemAction::power_on); // trigger PDP activation flow in context closed state
                    break;
                case NetworkLteState::off_mode:
                    call_action(ModemAction::turn_on_radio); // trigger PDP activation flow in attached state
                    break;
                case NetworkLteState::sleep_mode:
                    call_action(ModemAction::wake_up); // trigger attach flow in detached state
                    break;
                case NetworkLteState::transparent_mode:
                    call_action(ModemAction::leave_transparent_mode); // trigger exit transparent mode flow to go back to idle mode and restart normal flow
                    break;
                default:
                    break; // otherwise, just process events and update state as they come
            }
            // do nothing here, just let the normal event flow handle the transition to data ready state
             break;
        case NetworkLteState::transparent_mode:
            if(state_ != NetworkLteState::transparent_mode){
                call_action(ModemAction::enter_transparent_mode); // trigger transparent mode entry
            }
            break;
        case NetworkLteState::sleep_mode:
            // how can we force the modem to enter sleep mode (PSM) ? is there a specific command we can send to trigger it, or do we just need to wait for the network to trigger it based on our PSM configuration ?
            // for now, we will just rely on the normal event flow to enter sleep mode when
            call_action(ModemAction::enter_sleep); // trigger PSM event to check if we can enter sleep mode
            break;
        case NetworkLteState::switched_off:
            call_action(ModemAction::power_off); // trigger power off to enter switched off state
            break;
        case NetworkLteState::off_mode:
            call_action(ModemAction::switch_off_radio); // trigger radio off to enter off mode
            break;
        default:
            break; // otherwise, just process events and update state as they come
    }
    */
    execute_actions();

    return state_;
}

// perform actions
void NetworkLte::execute_actions() {
    ModemAction action = get_action();
    if(action != ModemAction::none)
        MODEM_LOG_DBG("Executing action: %s", action_to_str(action));
    switch(action){
        case ModemAction::reboot:
            {
                auto status = modem_.reboot();
                if(status == ModemStatus::ok){
                    change_state(NetworkLteState::rebooting); // after reboot, we can consider
                }
            }
            break;
        case ModemAction::check_responsiveness:
            {
                // check if modem is responsive by sending an AT command
                std::string response;
                auto status = modem_.at_ok();
                if(status != ModemStatus::ok){
                    MODEM_LOG_ERR("Modem is not responsive");
                    call_action(ModemAction::reboot); // try rebooting the modem to recover responsiveness
                }else{
                    MODEM_LOG_INF("Modem is responsive");
                }
            }
            break;
        case ModemAction::factory_reset:
            {
                /*
                auto status = modem_.factory_reset();
                if(status == ModemStatus::ok){
                    change_state(NetworkLteState::rebooting); // after factory reset, the modem will reboot, so we can consider it in rebooting state and wait for it to be responsive again before continuing with the flow
                }
                */
            }
            break;
        case ModemAction::power_on:
            // power on modem
            //modem_.power_on();
            modem_.set_baudrate(115200);
            modem_.set_echo(false);
            // check if modem is responsive            AtResponse response;
            {
                auto status = modem_.at_ok();
                if (status != ModemStatus::ok) {
                    MODEM_LOG_ERR("Failed to power on modem");
                    change_state(NetworkLteState::switched_off);
                    break;
                }
                MODEM_LOG_INF("Modem powered on and responsive");
                change_state(NetworkLteState::idle_mode);
                call_action(ModemAction::setup_radio); // trigger radio setup to start attach flow
                fColdBoot = true; 
                nNetworkAttempts = 0;
            }
            break;
        case ModemAction::power_off:
            modem_.power_off();
            change_state(NetworkLteState::switched_off);
            break;
        case ModemAction::turn_on_radio:
            // which command should I send to leave the off mode ?
            change_state(NetworkLteState::idle_mode);
            call_action(ModemAction::setup_radio); // trigger attach flow in idle mode
            break;
        case ModemAction::switch_off_radio:
            modem_.shutdown();
            change_state(NetworkLteState::off_mode);
            break;
        case ModemAction::enter_sleep:
            // how can we force the modem to enter sleep mode (PSM) ? is there a specific command we can send to trigger it, or do we just need to wait for the network to trigger it based on our PSM configuration ?
            // for now, we will just rely on the normal event flow to enter sleep mode when the network triggers it based on our PSM configuration
            // if PSM is active, do nothing and wait for event
            // otherwise call off mode action
            break;
        case ModemAction::wake_up:
            // drive GPIO0 + CFUN=1, Warm boot
            if(prev_state_ == NetworkLteState::data_ready){
                MODEM_LOG_INF("Waking up from sleep mode, previous state was data ready, trying to go back to data ready state");
                change_state(NetworkLteState::data_ready); // assuming we were in sleep mode with an active connection, we can try to go back to data ready state directly
                call_action(ModemAction::query_network_status);
            } else {
                MODEM_LOG_INF("Waking up from sleep mode, previous state was not data ready, going to idle mode to restart attach flow");
                fWarmBoot = true;
                change_state(NetworkLteState::idle_mode); // after waking up, we can go back to idle mode and check network status to continue with the flow
                call_action(ModemAction::setup_radio); // trigger attach flow in idle mode
            }
            break;
        case ModemAction::setup_radio:
            // get modem info
            modem::ModemStatus status;
            if(false){
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
            
            if(fChangeBands || fChangeRAT){
                if(fChangeBands){
                    fChangeBands = false;
                    auto status = modem_.set_lte_bands(lteConfig.default_lte_bands);
                    if (status != ModemStatus::ok) {
                        MODEM_LOG_ERR("Failed to set LTE bands");
                        // flag error
                    }
                }
                if(fChangeRAT){
                    fChangeRAT = false;
                    auto status = modem_.set_iot_tech(lteConfig.default_iot_tech);
                    if (status != ModemStatus::ok) {
                        MODEM_LOG_ERR("Failed to set IoT technology");
                        // flag error
                    }
                }
                // reboot modem to apply new bands configuration
                call_action(ModemAction::reboot);
            } else {
                if(fWarmBoot || fColdBoot){
                    MODEM_LOG_INF("Re-applying modem configuration after boot");
                    fWarmBoot = false;
                    fColdBoot = false;
                    TelitCpsmsConfig cfg = {
                        PsmMode::enable, 
                        false,
                        0,
                        false,
                        0,
                        true,
                        lteConfig.psm_t3412,
                        true,
                        lteConfig.psm_t3324
                    };
                    modem_.set_telit_psm(cfg);
                    modem_.set_psm_urc(true); // enable PSM URCs in normal mode
                    modem_.set_registration_urc(true); // enable registration URCs in normal mode
                    modem_.set_pdp_urc(true); // enable PDP URCs in normal mode

                }
                change_state(NetworkLteState::idle_mode);
                call_action(ModemAction::query_network_status); // check network status
            }
            break;
        
        case ModemAction::query_network_status:
            {
                auto status = modem_.get_registration_status(regInfo, RadioTech::cat_m1);
                if(regInfo.stat == RegStatus::denied ||
                    regInfo.stat == RegStatus::not_registered || 
                    regInfo.stat == RegStatus::searching ){
                    change_state(NetworkLteState::network_detached); // start attach with fallback config
                }
                else if(regInfo.stat == RegStatus::registered_home || regInfo.stat == RegStatus::registered_roaming){
                    call_action(ModemAction::query_pdp_context); // trigger PDP activation flow in context closed state
                }
                else if( regInfo.stat == RegStatus::unknown ){
                    // How to deal with it ?
                    // let's assume we are connected for now
                    MODEM_LOG_DBG("Registration status unknown..");
                }
            }
            break;
        
        case ModemAction::attach_network:
            //modem_.set_iot_tech(lteConfig.default_iot_tech); // needs reboot
            //modem_.network_attach();
            if(nAttachRetries == 0){
                // check default iot_tech
                RadioTech current_tech;
                uint8_t gsm_priority;
                auto status = modem_.get_iot_tech(current_tech,gsm_priority);
                if(status == ModemStatus::ok){
                    if(current_tech != lteConfig.default_iot_tech){
                        MODEM_LOG_WRN("Current IoT technology %d is different from default config %d, changing it to default config and rebooting to apply", static_cast<int>(current_tech), static_cast<int>(lteConfig.default_iot_tech));
                        fChangeRAT = true;
                    }
                }
                // check default bands
                std::string bands;
                status = modem_.get_bands(bands);
                /*
                if(status == ModemStatus::ok){
                    if(bands != lteConfig.default_lte_bands){
                        MODEM_LOG_WRN("Current LTE bands %s are different from default config %s, changing it to default config and rebooting to apply", networkInfo.lte_bands.c_str(), lteConfig.default_lte_bands.c_str());
                        fChangeBands = true;
                    }
                }
                */
                // if any diferent set it to default config and reboot to apply, then start attach flow in idle mode
                if(fChangeRAT || fChangeBands){
                    call_action(ModemAction::reboot); // if we need to change either RAT or bands, we can just reboot once and apply both changes at the same time
                    break;
                }

                MODEM_LOG_INF("Starting network attach with default configuration");
                status = modem_.set_operator_manual(lteConfig.plmn, lteConfig.default_iot_tech);
                if(status == ModemStatus::ok){
                    change_state(NetworkLteState::network_attaching);
                }else{
                    MODEM_LOG_ERR("Failed to set operator manual for fallback configuration");
                    // flag error and stay in the same state to retry later
                }
            } else if(nAttachRetries == 1){
                MODEM_LOG_INF("Retrying network attach with fallback configuration, attempt %d", nAttachRetries);
                status = modem_.set_operator_auto();
                if(status == ModemStatus::ok){
                    change_state(NetworkLteState::network_attaching);
                }else{
                    MODEM_LOG_ERR("Failed to set operator manual for fallback configuration");
                    // flag error and stay in the same state to retry later
                }
            }
            else if(nAttachRetries < lteConfig.max_attach_retries){
                MODEM_LOG_ERR("Retrying network attach with fallback configuration, attempt %d", nAttachRetries);
                // end operation if we have reached max attach retries
                change_state(NetworkLteState::done);
                on_event(NetworkLteEvent::attach_error); // flag attach error to trigger power off in the next step
            }
            nAttachRetries++;
            break;
        case ModemAction::query_pdp_context:
            {
                bool pdp_active = false;
                auto status = modem_.get_pdp_state(lteConfig.cid, pdp_active);
                if(!pdp_active){
                    change_state(NetworkLteState::pdp_context_closed); // if context is inactive, we can consider it closed and trigger PDP activation flow
                    call_action(ModemAction::open_pdp_context); // trigger PDP activation flow
                } else {
                    auto status = modem_.get_ip_address(lteConfig.cid, networkInfo.ip_address);
                    if(status != ModemStatus::ok){
                        MODEM_LOG_ERR("Failed to get IP address");
                        // flag error and stay in the same state to retry later
                    } else {
                        MODEM_LOG_DBG("Already registered with IP: %s", networkInfo.ip_address.c_str());
                        change_state(NetworkLteState::data_ready); // already have IP, we can go directly to data ready state
                    }
                }
            }
            break;
        case ModemAction::open_pdp_context:
            {
                auto status = modem_.activate_pdp(lteConfig.cid);
                if(status == ModemStatus::ok){
                    //change_state(NetworkLteState::pdp_context_opening);
                    change_state(NetworkLteState::data_ready);
                } else {
                    MODEM_LOG_ERR("Failed to activate PDP context");
                    // flag error and stay in the same state to retry later
                }
                break;
            
            }
            break;
        
        case ModemAction::send_data:
            {
                // Drain TX queues for all connection IDs, checking protocol per conn_id
                for (uint8_t id = 1; id <= MAX_SERVER_CONNECTIONS; ++id) {
                    if (!message_queue_ || message_queue_->tx_count(id) == 0) {
                        continue;
                    }
                    if (serverInfo[id - 1].state != ServerState::connected) {
                        MODEM_LOG_WRN("conn_id %d not connected, skipping TX drain", id);
                        continue;
                    }
                    QueueMessage msg;
                    while (message_queue_->tx_pop(id, msg) == QueueError::ok) {
                        if (serverInfo[id - 1].protocol == "UDP") {
                            auto status = modem_.udp_send(id, msg.data);
                            if (status != ModemStatus::ok) {
                                MODEM_LOG_ERR("Failed to send UDP data on conn_id %d", id);
                            }
                        } else if (serverInfo[id - 1].protocol == "TCP") {
                            MODEM_LOG_WRN("TCP send not implemented for conn_id %d", id);
                        } else {
                            MODEM_LOG_ERR("Unknown protocol for conn_id %d, cannot send", id);
                        }
                    }
                }
            }
            break;
        
        case ModemAction::read_data:
            // for testing, we can just read data from the modem and print it, in real implementation, we would pass it to a buffer
            {
                std::vector<uint8_t> buf;
                if (modem_.udp_receive(lteConfig.conn_id, buf) == ModemStatus::ok && !buf.empty()) {
                    std::string payload(buf.begin(), buf.end());
                    MODEM_LOG_INF("Received data: %s", payload.c_str());
                } else {
                    MODEM_LOG_ERR("Failed to read data or no data available");
                    // flag error
                }
            }
            break;
        case ModemAction::data_complete:
            // signal server that we are going offline for a while (e.g. going to sleep mode) so it should not expect data from us and we can close the connection gracefully
            break;
        
        case ModemAction::enter_transparent_mode:
            // switch modem to transparent mode, in this mode we can just send data to the modem and it will be sent directly without needing to use UDP send/receive functions, this is useful for low latency applications where we want to minimize the time between sending data and it being sent over the network
            // do any task needed to switch to transparent mode (e.g. close existing PDP context, set up new PDP context with transparent mode settings, etc.)
            MODEM_LOG_INF("Modem in transparent mode (ready to receive AT commands)");
            modem_.set_psm_urc(false); // disable PSM URCs in transparent mode to avoid interfering with raw data reception
            modem_.set_registration_urc(false); // disable registration URCs in transparent mode to avoid interfering with raw data reception
            modem_.set_pdp_urc(false); // disable PDP URCs in transparent mode to avoid interfering with raw data reception
            change_state(NetworkLteState::transparent_mode);
            break;

        case ModemAction::leave_transparent_mode:
            // switch modem to normal mode, in this mode we use UDP send/receive functions for data transmission
            // do any task needed to switch to normal mode (e.g. close existing PDP context, set up new PDP context with normal mode settings, etc.)
            MODEM_LOG_INF("Modem leaving transparent mode (ready to receive AT commands)");
            modem_.set_psm_urc(true); // enable PSM URCs in normal mode
            modem_.set_registration_urc(true); // enable registration URCs in normal mode
            modem_.set_pdp_urc(true); // enable PDP URCs in normal mode
            change_state(NetworkLteState::idle_mode); // after leaving transparent mode, we can go back to idle mode and restart attach flow to ensure we are properly connected before sending/receiving data
            call_action(ModemAction::query_network_status); // trigger attach flow to ensure we are properly connected before sending/receiving data
            break;
    }
}

void NetworkLte::handle_urc(const std::string& urc) {
    // +CREG / +CEREG: <n>,<stat>  or  <stat>
    MODEM_LOG_DBG("Handle URC: %s", urc.c_str());
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
            TelitCpsmsStatus status;
            modem_.get_telit_psm(status); // update PSM config in info struct after applying it to modem
            MODEM_LOG_INF("PSM status: %d", status.mode);
            MODEM_LOG_INF("PSM status: %d", status.mode);
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
        MODEM_LOG_DBG("Data available URC received");
        std::string body = urc.substr(6);
        auto s = body.find_first_not_of(' ');
        if (s != std::string::npos) {
            int id = std::stoi(body.substr(s));
            last_data_conn_id_ = static_cast<uint8_t>(id);
            MODEM_LOG_DBG("Data available on id: %d", id);
            on_event(NetworkLteEvent::data_available);
        }
        return;
    }
}

// replace all occurrences of event_ = with on_event(event) to ensure that all events go through the on_event handler for better tracking and debugging
void NetworkLte::on_event(NetworkLteEvent event) {    
    event_ = event;
    log_event();
    // record uptime and event history for debugging/analytics
}

NetworkLteEvent NetworkLte::get_event() {
    NetworkLteEvent event = event_;
    event_ = NetworkLteEvent::none; // reset event after reading it to avoid processing the same event multiple times, if we want to keep a history of events, we can store them in a vector instead of resetting it
    return event;
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
            if(target_state == NetworkLteState::data_ready || 
                target_state == NetworkLteState::transparent_mode || 
                target_state == NetworkLteState::network_detached){
                call_action(ModemAction::power_on);
            }
            break;
        case NetworkLteState::off_mode:
            if(target_state == NetworkLteState::idle_mode || target_state == NetworkLteState::setup_mode || target_state == NetworkLteState::network_detached){
                call_action(ModemAction::turn_on_radio);
            }
            break;
        case NetworkLteState::sleep_mode:
            if(target_state == NetworkLteState::data_ready){
                call_action(ModemAction::wake_up); // trigger wake up to go back to data ready state
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
        switch(event_){
            case NetworkLteEvent::network_error:
            case NetworkLteEvent::attach_error:
            case NetworkLteEvent::context_error:
                MODEM_LOG_ERR("Error event received while trying to transition to target state, aborting transition");
                return false; // if we receive an error event during the transition, we can consider the transition failed and return false
            default:
                break; // for other events, we will just continue processing
        }
    }
    return true;
}

// called from state_machine
// actions to perform on state change, such as starting/stopping timers, logging state transitions, etc.
void NetworkLte::change_state(NetworkLteState new_state) {

    if(new_state == state_){
        return; // no state change
    }

    prev_state_ = state_;
    state_ = new_state;
    log_state();

    // stop any timers that are running for the previous state, and log elapsed time for debugging/analytics purposes
    switch(prev_state_){
        case NetworkLteState::network_attaching:
            {
                uint32_t elasped = st_timer->elapsed_ms();
                MODEM_LOG_INF("Time spent in network_attaching state: %d ms", elasped);
            }
            st_timer->stop(); // stop attach timer if running
            break;
        case NetworkLteState::pdp_context_opening:
            {
                uint32_t elasped = st_timer->elapsed_ms();
                MODEM_LOG_INF("Time spent in pdp_context_opening state: %d ms", elasped);
            }
            st_timer->stop(); // stop PDP activation timer if running
            break;
        case NetworkLteState::data_ready:
            {
                uint32_t elasped = st_timer->elapsed_ms();
                MODEM_LOG_INF("Time spent in data_ready state: %d ms", elasped);
            }
            st_timer->stop(); // stop PDP activation timer if running
            break;
        case NetworkLteState::transparent_mode:
            {
                uint32_t elasped = st_timer->elapsed_ms();
                MODEM_LOG_INF("Time spent in transparent_mode state: %d ms", elasped);
            }
            st_timer->stop(); // stop any timers related to transparent mode if needed
            break;
        default:
            break; // no special handling needed for other states when exiting
    }
    
    // init timers for states that require timeouts to trigger retries or error handling if we get stuck in those states for too long (e.g. if we are attaching to the network but it is taking too long, we can trigger a retry or fallback to a different configuration)
    switch(state_){
        case NetworkLteState::network_attaching:
            st_timer->start(lteConfig.attach_timeout_sec*1000, [this](){ on_event(NetworkLteEvent::timeout); }); // example timeout, adjust as needed
            break;
        case NetworkLteState::pdp_context_opening:
            st_timer->start(lteConfig.pdp_timeout_sec*1000, [this](){ on_event(NetworkLteEvent::timeout); }); // example timeout, adjust as needed
            break;
        case NetworkLteState::data_ready:
            {
                TelitCpsmsStatus state;
                auto status = modem_.get_telit_psm(state); // update PSM config in info struct after applying it to modem
                if(status == ModemStatus::ok){
                    /*
                    networkInfo.psm_mode = state.mode;
                    networkInfo.psm_t3324 = state.t3324;
                    networkInfo.psm_t3412 = state.t3412;
                    networkInfo.psm_version = state.psm_version;
                    networkInfo.psm_threshold = state.psm_threshold;
                    */
                    MODEM_LOG_INF("PSM state: %d", state.mode);
                    MODEM_LOG_INF("PSM t3324: %d", state.t3324);
                    MODEM_LOG_INF("PSM t3412: %d", state.t3412);
                    MODEM_LOG_INF("PSM psm_version: %d", state.psm_version);
                    MODEM_LOG_INF("PSM psm_threshold: %d", state.psm_threshold);
                    MODEM_LOG_INF("PSM mode: %d", state.mode);
                } else {
                    MODEM_LOG_ERR("Failed to get PSM status");
                }
                st_timer->start(lteConfig.data_ready_timeout_sec*1000, [this](){ on_event(NetworkLteEvent::timeout); }); // example timeout, adjust as needed
            }
            break;
        case NetworkLteState::transparent_mode:
            st_timer->start(lteConfig.transparent_timeout_sec*1000, [this](){ on_event(NetworkLteEvent::timeout); }); // example timeout, adjust as needed
            break;
        default:
            break; // no special handling needed for other states when entering
    }
    
    switch(state_){
        case NetworkLteState::network_detached:
            MODEM_LOG_INF("State changed to network_detached, resetting network info and server states");
            networkInfo = {}; // reset network info
            {
                uint8_t i = 0;
                while(i < MAX_SERVER_CONNECTIONS){
                    serverInfo[i++].state = ServerState::disconnected; // reset server states
                }
            }
            call_action(ModemAction::attach_network); // trigger attach flow to try to re-attach to network
            break;
        case NetworkLteState::pdp_context_closed:
            MODEM_LOG_INF("State changed to pdp_context_closed, resetting IP address and server states");
            networkInfo.ip_address = ""; // reset IP address
            {
                uint8_t i = 0;
                while(i < MAX_SERVER_CONNECTIONS){
                    serverInfo[i++].state = ServerState::disconnected; // reset server states
                }
            }
            call_action(ModemAction::open_pdp_context); // trigger PDP activation flow to try to open PDP context and get an IP address
            break;
    }
}

void NetworkLte::call_action(ModemAction action){
    modem_action_ = action;
    log_action();
}

ModemAction NetworkLte::get_action() {
    ModemAction action = modem_action_;
    modem_action_ = ModemAction::none; // reset action after reading it, since actions are one-time triggers that should be executed once and then cleared until the next time they are triggered
    return action;
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
        // modem urc events
        case NetworkLteEvent::psm_enter:                 return "psm_enter";
        case NetworkLteEvent::psm_exit:                  return "psm_exit";
        case NetworkLteEvent::network_detached:          return "network_detached";
        case NetworkLteEvent::network_attached:          return "network_attached";
        case NetworkLteEvent::context_opened:            return "context_opened";
        case NetworkLteEvent::context_closed:            return "context_closed";
        case NetworkLteEvent::context_rejected:          return "context_rejected";
        case NetworkLteEvent::data_available:            return "data_available";
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

static const char* modem_event_to_str(NetworkLteEvent e) {
    switch (e) {
        case NetworkLteEvent::psm_enter:                 return "psm_enter";
        case NetworkLteEvent::psm_exit:                  return "psm_exit";
        case NetworkLteEvent::network_detached:          return "network_detached";
        case NetworkLteEvent::network_attached:          return "network_attached";
        case NetworkLteEvent::context_opened:            return "context_opened";
        case NetworkLteEvent::context_closed:            return "context_closed";
        case NetworkLteEvent::context_rejected:          return "context_rejected";
        case NetworkLteEvent::data_available:            return "data_available";
        default:                                         return "unknown_modem_event";
    }
}

static const char* action_to_str(ModemAction a) {
    switch (a) {
        case ModemAction::none:                           return "none";
        // recovering actions
        case ModemAction::reboot:                         return "reboot";
        case ModemAction::factory_reset:                  return "factory_reset";
        case ModemAction::check_responsiveness:           return "check_responsiveness";
        // power events
        case ModemAction::power_on:                       return "power_on";
        case ModemAction::power_off:                      return "power_off";
        case ModemAction::turn_on_radio:                  return "turn_on_radio";
        case ModemAction::switch_off_radio:               return "switch_off_radio";
        case ModemAction::enter_sleep:                    return "enter_sleep";
        case ModemAction::wake_up:                        return "wake_up";
        case ModemAction::setup_radio:                    return "setup_radio";
        // internal actions to get current state
        case ModemAction::query_network_status:           return "query_network_status";
        case ModemAction::query_pdp_context:              return "query_pdp_context";
        // internal actions to drive state machine forwared
        case ModemAction::attach_network:                 return "attach_network";
        case ModemAction::open_pdp_context:               return "open_pdp_context";
        // data actions
        case ModemAction::send_data:                      return "send_data";
        case ModemAction::read_data:                      return "read_data";
        case ModemAction::data_complete:                  return "data_complete";
        // sepecial modes
        case ModemAction::enter_transparent_mode:         return "enter_transparent_mode";
        case ModemAction::leave_transparent_mode:         return "leave_transparent_mode";
        default:                                          return "unknown_action";
    }
}

void NetworkLte::log_state() const {
    MODEM_LOG_DBG("new state: %s", state_to_str(state_));
}

void NetworkLte::log_event() const {
    MODEM_LOG_DBG("new event: %s", event_to_str(event_));
}

void NetworkLte::log_action() const {
    if(modem_action_ != ModemAction::none)
        MODEM_LOG_DBG("new action: %s", action_to_str(modem_action_));
}

} // namespace modem
