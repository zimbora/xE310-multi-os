#include "modem/network_lte.h"
#include "modem/log.h"

namespace modem {

NetworkLte::NetworkLte(xE310& modem, const NetworkLteConfig& config)
    : modem_(modem), lteConfig(config) {}

// --- Accessors ---
const RegistrationInfo& NetworkLte::registration_info() const { return regInfo; }
const SignalQuality&    NetworkLte::signal_quality()    const { return signalQuality; }
const std::string&      NetworkLte::iccid()             const { return modemInfo.iccid; }
const std::string&      NetworkLte::imsi()              const { return modemInfo.imsi; }
const NetworkLteConfig& NetworkLte::config()            const { return lteConfig; }
void NetworkLte::set_config(const NetworkLteConfig& config) { lteConfig = config; }

NetworkLteState NetworkLte::state() const { return state_; }
NetworkLteEvent NetworkLte::event() const { return event_; }

uint8_t NetworkLte::try_count() const { return try_count_; }

// ---------------------------------------------------------------------------
// loop — state-transition logic
// ---------------------------------------------------------------------------
NetworkLteState NetworkLte::st_machine() {
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
                    state_ = NetworkLteState::switched_off;
                    break;
                }
                state_ = NetworkLteState::idle_mode;
                if(fNewConfig){
                    event_ = NetworkLteEvent::setup_radio; // start setup flow to apply new config
                    fNewConfig = false;
                }else{
                    event_ = NetworkLteEvent::query_network_attached; // start attach flow
                }
                try_count_ = 0;
            }
            break;

        case NetworkLteState::off_mode:
            if (event_ == NetworkLteEvent::turn_on_radio) {
                // which command should I send to leave the off mode ?
                state_ = NetworkLteState::idle_mode;
                event_ = NetworkLteEvent::query_network_attached; // start attach flow
            }
            break;

        case NetworkLteState::sleep_mode:
            if (event_ == NetworkLteEvent::wake_up) {
                // do I need to send any command to wake up the modem ?
                state_ = NetworkLteState::data_ready; // assuming we were in sleep mode with an active connection, we can go directly to data ready state
            }
            break;
            
        case NetworkLteState::idle_mode:

            //modem_.read_iccid(modemInfo.iccid);
            //modem_.read_imsi(modemInfo.imsi);
            
            switch (event_) {
                case NetworkLteEvent::setup_radio:
                    state_ = NetworkLteState::setup_mode;
                    try_count_ = 0;
                    break;
                case NetworkLteEvent::query_network_detached:
                    state_ = NetworkLteState::detached;
                    try_count_ = 0;
                    break;
                case NetworkLteEvent::query_server_deregistered:
                    state_ = NetworkLteState::server_disconnected;
                    break;
                case NetworkLteEvent::query_network_attached: {
                    auto status = modem_.get_registration_status(regInfo);
                    if(status != ModemStatus::ok){
                        MODEM_LOG_ERR("Failed to get registration status");
                        // keep on the same state to retry later
                        break;
                    } else {
                        if(regInfo.stat == RegStatus::denied){
                            MODEM_LOG_ERR("Network registration denied");
                            try_count_++;
                            // reset timer
                        }
                        if(regInfo.stat == RegStatus::not_registered || 
                        regInfo.stat == RegStatus::denied ||
                        regInfo.stat == RegStatus::unknown){
                            if(try_count_ >= lteConfig.max_attach_retries){
                                state_ = NetworkLteState::done; // shouldn't happen !!
                                event_ = NetworkLteEvent::switch_off_radio; // power off modem
                                break;
                            } else if(try_count_ == 0){
                                // use default apn
                                modem_.set_apn(lteConfig.cid, lteConfig.default_apn);
                                //modem_.set_lte_bands(lteConfig.default_lte_bands);
                                state_ = NetworkLteState::detached; // start attach with default config
                                event_ = NetworkLteEvent::attach_started; // trigger attach flow
                                break;
                            }else if(try_count_ == 1){
                                // fallback apn + attach
                                fNewConfig = true;
                                modem_.set_apn(lteConfig.cid, lteConfig.fallback_apn);
                                //modem_.set_lte_bands(lteConfig.fallback_lte_bands);
                                state_ = NetworkLteState::detached; // start attach with fallback config
                                event_ = NetworkLteEvent::attach_started; // trigger attach flow
                                break;
                            }
                        }
                        if(regInfo.stat == RegStatus::registered_home || regInfo.stat == RegStatus::registered_roaming){
                            event_ = NetworkLteEvent::query_network_attached; // trigger attach flow
                            break;
                        }
                    }
                    break;
                }
                case NetworkLteEvent::query_network_has_context: {
                    auto status = modem_.get_ip_address(lteConfig.cid, networkInfo.ip_address);
                    if (status != ModemStatus::ok) {
                        // flag error
                        break;
                    }else{
                        if(networkInfo.ip_address.empty()){
                            MODEM_LOG_ERR("Registered but no IP address assigned");
                            state_ = NetworkLteState::context_deactivated; // try to open PDP context to get IP
                            event_ = NetworkLteEvent::pdp_opening; // trigger PDP activation flow
                            break;
                        } else {
                            MODEM_LOG_DBG("Already registered with IP: %s", networkInfo.ip_address.c_str());
                            event_ = NetworkLteEvent::query_server_connected; // query server connection to trigger data flow
                        }
                    }
                    break;
                }
                case NetworkLteEvent::query_server_connected: {
                    uint8_t conn_state = 0;
                    auto status = modem_.udp_status(lteConfig.conn_id, conn_state);
                    serverInfo.state = static_cast<ServerState>(conn_state);
                    if(status != ModemStatus::ok){
                        MODEM_LOG_ERR("Failed to query server connection status");
                        // flag error
                        break;
                    } else {
                        if(serverInfo.state == ServerState::SERVER_CONNECTED){
                            MODEM_LOG_DBG("Already connected to server");
                            state_ = NetworkLteState::data_ready; // already connected to server, ready to send data
                            event_ = NetworkLteEvent::send_data; // trigger data flow
                        } else {
                            state_ = NetworkLteState::server_disconnected; // start server registration flow
                            event_ = NetworkLteEvent::server_connect; // trigger server registration flow
                        }
                    }
                    break;
                }
                case NetworkLteEvent::network_error:
                    try_count_++;
                    state_ = NetworkLteState::done;
                    break;
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
            }
            // get modem configuration (bands, iot tech, apn) and apply it
            
            //modem_.set_lte_bands(lteConfig.default_lte_bands);
            //modem_.set_iot_technology(lteConfig.default_iot_tech);
            modem_.set_apn(1, lteConfig.default_apn);
            //modem_.set_psm(cfg);
            modem_.set_telit_psm(telitCpsmsConfig);
            
            state_ = NetworkLteState::idle_mode;
            event_ = NetworkLteEvent::query_network_attached; // start attach flow
            break;

        case NetworkLteState::detached:
            if (event_ == NetworkLteEvent::attach_started) {
                if(nAttachRetries >= lteConfig.max_attach_retries){
                    state_ = NetworkLteState::done;
                    event_ = NetworkLteEvent::attach_error; // flag attach error to trigger power off in the next step
                    break;
                }
                else if(nAttachRetries == 0){
                    modem_.set_operator_manual(lteConfig.plmn, RadioTech::lte);
                }else{
                    modem_.set_operator_auto();
                }
                nAttachRetries++;
                state_ = NetworkLteState::attaching;
            }
            break;

        case NetworkLteState::attaching:
            // wait for network to attach, triggered by +creg URC
            if(event_ == NetworkLteEvent::network_attached) {
                state_ = NetworkLteState::context_deactivated; // start PDP activation flow
                event_ = NetworkLteEvent::pdp_opening;
            } else if (event_ == NetworkLteEvent::timeout) {
                state_ = NetworkLteState::detached; // retry attach
            } else if (event_ == NetworkLteEvent::network_loss) {
                state_ = NetworkLteState::detached; // retry attach
            }
            break;

        case NetworkLteState::context_deactivated:
            if (event_ == NetworkLteEvent::pdp_opening) {
                try_count_++;
                state_ = NetworkLteState::opening_pdp_context;
            } else if (event_ == NetworkLteEvent::network_loss) {
                state_ = NetworkLteState::detached;
                try_count_ = 0;
            }
            break;

        case NetworkLteState::opening_pdp_context:
            if (event_ == NetworkLteEvent::context_activated) {
                state_ = NetworkLteState::server_disconnected;
                try_count_ = 0;
            } else if (event_ == NetworkLteEvent::context_loss ||
                    event_ == NetworkLteEvent::timeout) {
                if (try_count_ < lteConfig.max_pdp_retries) {
                    state_ = NetworkLteState::context_deactivated;
                } else {
                    state_ = NetworkLteState::idle_mode;
                    try_count_ = 0;
                }
            } else if (event_ == NetworkLteEvent::network_loss) {
                state_ = NetworkLteState::detached;
                try_count_ = 0;
            }
            break;

        case NetworkLteState::server_disconnected:
            if (event_ == NetworkLteEvent::server_connect) {
                state_ = NetworkLteState::server_registering;
            } else if (event_ == NetworkLteEvent::context_loss) {
                state_ = NetworkLteState::context_deactivated;
                try_count_ = 0;
            }
            break;

        case NetworkLteState::server_registering:
            if (event_ == NetworkLteEvent::authenticated) {
                state_ = NetworkLteState::data_ready;
            } else if (event_ == NetworkLteEvent::rejected) {
                state_ = NetworkLteState::server_bootstrap;
            } else if (event_ == NetworkLteEvent::timeout_server) {
                state_ = NetworkLteState::server_disconnected;
            }
            break;

        case NetworkLteState::server_bootstrap:
            if (event_ == NetworkLteEvent::bootstrap_complete) {
                state_ = NetworkLteState::data_ready;
            } else if (event_ == NetworkLteEvent::timeout_server) {
                state_ = NetworkLteState::server_disconnected;
            }
            break;

        case NetworkLteState::data_ready:
            if (event_ == NetworkLteEvent::data_complete) {
                state_ = NetworkLteState::done;
            } else if (event_ == NetworkLteEvent::context_loss) {
                state_ = NetworkLteState::context_deactivated;
                try_count_ = 0;
            }
            break;

        case NetworkLteState::done:
            switch (event_) {
                case NetworkLteEvent::enter_sleep: // check if is needed !!
                case NetworkLteEvent::psm:
                    state_ = NetworkLteState::sleep_mode;
                    break;
                case NetworkLteEvent::switch_off_radio: {
                    auto state = modem_.shutdown();
                    if(state != ModemStatus::ok){
                        MODEM_LOG_ERR("Failed to shutdown modem");
                        // flag error but keep the state to retry later
                        // power off modem anyway to avoid being stuck in a bad state, will retry power on later
                        // modem_.power_off();
                        state_ = NetworkLteState::switched_off;
                        break;
                    }
                    state_ = NetworkLteState::off_mode;
                    break;
                }
                case NetworkLteEvent::power_off:
                    state_ = NetworkLteState::switched_off;
                    // modem_.power_off();
                    break;
                case NetworkLteEvent::timeout_server:
                    try_count_++;
                    if(try_count_ > lteConfig.max_pdp_retries){
                        event_ = NetworkLteEvent::switch_off_radio; // power off modem to reset state and retry later
                    } else {
                        state_ = NetworkLteState::data_ready;
                    }
                    break;
                default:
                    break;
            }
    }

    return state_;
}

void NetworkLte::on_event(NetworkLteEvent event) {
    event_ = event;
    st_machine();
}

// used for tests to trigger state transitions with specific events without relying on the full modem interaction flow
void NetworkLte::step() {
    st_machine();
}

static const char* state_to_str(NetworkLteState s) {
    switch (s) {
        case NetworkLteState::switched_off:        return "switched_off";
        case NetworkLteState::off_mode:            return "off_mode";
        case NetworkLteState::sleep_mode:          return "sleep_mode";
        case NetworkLteState::setup_mode:          return "setup_mode";
        case NetworkLteState::idle_mode:           return "idle_mode";
        case NetworkLteState::detached:            return "detached";
        case NetworkLteState::attaching:           return "attaching";
        case NetworkLteState::context_deactivated: return "context_deactivated";
        case NetworkLteState::opening_pdp_context: return "opening_pdp_context";
        case NetworkLteState::server_disconnected: return "server_disconnected";
        case NetworkLteState::server_registering:  return "server_registering";
        case NetworkLteState::server_bootstrap:    return "server_bootstrap";
        case NetworkLteState::data_ready:          return "data_ready";
        case NetworkLteState::done:                return "done";
        default:                                   return "unknown";
    }
}

static const char* event_to_str(NetworkLteEvent e) {
    switch (e) {
        case NetworkLteEvent::none:                      return "none";
        case NetworkLteEvent::power_on:                  return "power_on";
        case NetworkLteEvent::power_off:                 return "power_off";
        case NetworkLteEvent::turn_on_radio:             return "turn_on_radio";
        case NetworkLteEvent::switch_off_radio:          return "switch_off_radio";
        case NetworkLteEvent::enter_sleep:               return "enter_sleep";
        case NetworkLteEvent::setup_radio:               return "setup_radio";
        case NetworkLteEvent::wake_up:                   return "wake_up";
        case NetworkLteEvent::send_data:                 return "send_data";
        case NetworkLteEvent::psm:                       return "psm";
        case NetworkLteEvent::query_network_attached:    return "query_network_attached";
        case NetworkLteEvent::query_network_detached:    return "query_network_detached";
        case NetworkLteEvent::query_network_has_context: return "query_network_has_context";
        case NetworkLteEvent::query_server_connected:    return "query_server_connected";
        case NetworkLteEvent::query_server_deregistered: return "query_server_deregistered";
        case NetworkLteEvent::network_error:             return "network_error";
        case NetworkLteEvent::attach_started:            return "attach_started";
        case NetworkLteEvent::network_attached:          return "network_attached";
        case NetworkLteEvent::pdp_opening:               return "pdp_opening";
        case NetworkLteEvent::context_activated:         return "context_activated";
        case NetworkLteEvent::server_connect:            return "server_connect";
        case NetworkLteEvent::network_loss:              return "network_loss";
        case NetworkLteEvent::context_loss:              return "context_loss";
        case NetworkLteEvent::timeout:                   return "timeout";
        case NetworkLteEvent::authenticated:             return "authenticated";
        case NetworkLteEvent::rejected:                  return "rejected";
        case NetworkLteEvent::bootstrap_complete:        return "bootstrap_complete";
        case NetworkLteEvent::data_complete:             return "data_complete";
        case NetworkLteEvent::timeout_server:            return "timeout_server";
        case NetworkLteEvent::attach_error:              return "attach_error";
        case NetworkLteEvent::context_error:             return "context_error";
        default:                                         return "unknown";
    }
}

void NetworkLte::log_state() const {
    MODEM_LOG_DBG("state=%-24s event=%s", state_to_str(state_), event_to_str(event_));
}

} // namespace modem
