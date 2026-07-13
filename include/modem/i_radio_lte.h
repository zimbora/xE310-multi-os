#pragma once

#include "modem/event_flags_factory.h"
#include "modem/fixed_string.h"
#include "modem/message_channel_factory.h"
#include "modem/network_lte_config.h"
#include "modem/static_vector.h"
#include "modem/xe310.h"

#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace modem {

enum class NetworkLteState : uint8_t;
enum class NetworkLteEvent : uint8_t;
struct ServerInfo;

/// Event bits posted on modem_evt to synchronize modem requests, responses and notifications.
enum ModemEvtBits : uint32_t {
    MODEM_EVT_REQUEST = (1U << 0),
    MODEM_EVT_RESPONSE = (1U << 1),
    MODEM_EVT_STATE = (1U << 2),
    MODEM_EVT_LOG = (1U << 3),
};

/// Request message type for cross-thread radio LTE state queries/actions.
enum class RadioLteRequestType : uint8_t {
    get_registration_info,
    get_signal_quality,
    get_iccid,
    get_imsi,
    get_modem_info,
    get_sim_status,
    get_radio_tech,
    get_reg_status,
    get_network_info,
    get_psm_mode,
    get_cpsms_config,
    get_telit_cpsms_config,
    get_telit_cpsms_status,
    get_network_survey_result,
    get_available_operators,
    get_csurv_result,
    scan_networks,
    get_server_info_array,
    get_config,
    set_config,
    network_connect,
    network_disconnect,
    server_disconnect,
    force_psm,
};

/// Fixed-size command message submitted to modem_tx_q.
struct ModemTxMsg {
    RadioLteRequestType type = RadioLteRequestType::get_registration_info;
    uint32_t arg0 = 0;
    uint32_t arg1 = 0;
};

struct ModemSetConfigMsg {
    RadioLteRequestType type = RadioLteRequestType::set_config;
    NetworkLteConfig config{};
};

static_assert(sizeof(ModemSetConfigMsg) <= MESSAGE_CHANNEL_MAX_DATA,
              "ModemSetConfigMsg exceeds MESSAGE_CHANNEL_MAX_DATA");

using RadioLteRequestMsg = ModemTxMsg;

template<typename ValueType>
struct ModemTypedResponseMsg {
    bool ok = false;
    ValueType value{};
};

static_assert(sizeof(ModemTypedResponseMsg<NetworkLteConfig>) <= MESSAGE_CHANNEL_MAX_DATA,
              "ModemTypedResponseMsg<NetworkLteConfig> exceeds MESSAGE_CHANNEL_MAX_DATA");

/// Event payload describing the current LTE state machine snapshot.
struct ModemStateMsg {
    NetworkLteState state = static_cast<NetworkLteState>(0);
    NetworkLteEvent event = static_cast<NetworkLteEvent>(0);
};

/// Fixed-size log line forwarded through modem_log_q.
struct ModemLogMsg {
    FixedString<MODEM_LONG_STR> text{};
};

/// Cross-platform transport objects for LTE requests, state notifications and logs.
class RadioLteChannels {
public:
    static constexpr size_t MODEM_TX_Q_DEPTH = 8;
    static constexpr size_t MODEM_RX_Q_DEPTH = 8;
    static constexpr size_t MODEM_LOG_Q_DEPTH = 16;

    RadioLteChannels()
        : modem_tx_q(create_platform_message_channel(MODEM_TX_Q_DEPTH)),
          modem_rx_q(create_platform_message_channel(MODEM_RX_Q_DEPTH)),
          modem_log_q(create_platform_message_channel(MODEM_LOG_Q_DEPTH)),
          modem_evt(create_platform_event_flags()) {}

    MessageChannelError send_request(const ModemTxMsg& msg, uint32_t timeout_ms = 0) {
        return send_request_message(msg, timeout_ms);
    }

    MessageChannelError send_request(const ModemSetConfigMsg& msg, uint32_t timeout_ms = 0) {
        return send_request_message(msg, timeout_ms);
    }

    MessageChannelError recv_request(ModemTxMsg& msg, uint32_t timeout_ms = 0) {
        return receive_message(*modem_tx_q, msg, timeout_ms);
    }

    MessageChannelError recv_request_frame(MessageFrame& frame, uint32_t timeout_ms = 0) {
        return modem_tx_q->receive(frame, timeout_ms);
    }

    template<typename ValueType>
    MessageChannelError publish_typed_response(const ValueType& value, uint32_t timeout_ms = 0) {
        ModemTypedResponseMsg<ValueType> msg{};
        msg.ok = true;
        msg.value = value;
        MessageChannelError err = send_message(*modem_rx_q, msg, timeout_ms);
        if (err == MessageChannelError::ok) {
            modem_evt->set(MODEM_EVT_RESPONSE);
        }
        return err;
    }

    template<typename ValueType>
    MessageChannelError recv_typed_response(ModemTypedResponseMsg<ValueType>& msg, uint32_t timeout_ms = 0) {
        return receive_message(*modem_rx_q, msg, timeout_ms);
    }

    void publish_state(NetworkLteState state, NetworkLteEvent event = static_cast<NetworkLteEvent>(0)) {
        state_msg.state = state;
        state_msg.event = event;
        modem_evt->set(MODEM_EVT_STATE);
    }

    const ModemStateMsg& current_state() const { return state_msg; }

    MessageChannelError publish_log(std::string_view line, uint32_t timeout_ms = 0) {
        ModemLogMsg msg{};
        msg.text = line;
        MessageChannelError err = send_message(*modem_log_q, msg, timeout_ms);
        if (err == MessageChannelError::ok) {
            modem_evt->set(MODEM_EVT_LOG);
        }
        return err;
    }

    MessageChannelError recv_log(ModemLogMsg& msg, uint32_t timeout_ms = 0) {
        return receive_message(*modem_log_q, msg, timeout_ms);
    }

    uint32_t wait(uint32_t events, bool reset, uint32_t timeout_ms = 0) {
        return modem_evt->wait(events, reset, timeout_ms);
    }

    void clear(uint32_t events) { modem_evt->clear(events); }

private:
    template<typename RequestType>
    MessageChannelError send_request_message(const RequestType& msg, uint32_t timeout_ms) {
        MessageChannelError err = send_message(*modem_tx_q, msg, timeout_ms);
        if (err == MessageChannelError::ok) {
            modem_evt->set(MODEM_EVT_REQUEST);
        }
        return err;
    }
    template<typename MessageType>
    static MessageChannelError send_message(MessageChannelInterface& channel, const MessageType& msg,
                                            uint32_t timeout_ms) {
        static_assert(std::is_trivially_copyable<MessageType>::value, "MessageType must be trivially copyable");
        return channel.send(reinterpret_cast<const uint8_t*>(&msg), sizeof(MessageType), timeout_ms);
    }

    template<typename MessageType>
    static MessageChannelError receive_message(MessageChannelInterface& channel, MessageType& msg,
                                               uint32_t timeout_ms) {
        static_assert(std::is_trivially_copyable<MessageType>::value, "MessageType must be trivially copyable");
        MessageFrame frame{};
        MessageChannelError err = channel.receive(frame, timeout_ms);
        if (err != MessageChannelError::ok) return err;
        if (frame.length != sizeof(MessageType)) return MessageChannelError::invalid_size;
        std::memcpy(&msg, frame.data.data(), sizeof(MessageType));
        return MessageChannelError::ok;
    }

    MessageChannelHandle modem_tx_q;
    MessageChannelHandle modem_rx_q;
    MessageChannelHandle modem_log_q;
    EventFlagsHandle modem_evt;
    ModemStateMsg state_msg{};
};

/// Interface that exposes the last known LTE radio/modem state.
class IRadioLte {
public:
    virtual ~IRadioLte() = default;

    /// Attach to network and establish default PDP connectivity.
    virtual bool network_connect() = 0;

    /// Disconnect from network and deactivate connectivity.
    virtual bool network_disconnect() = 0;

    /// Disconnect server socket for the given connection id.
    virtual bool server_disconnect(uint8_t conn_id) = 0;

    /// Force modem into PSM flow according to current configuration.
    virtual bool force_psm() = 0;

    /// Last registration info read from the modem.
    virtual const RegistrationInfo& registration_info() const = 0;

    /// Last signal quality read from the modem.
    virtual const SignalQuality& signal_quality() const = 0;

    /// SIM ICCID read at power-on.
    virtual const FixedString<MODEM_SHORT_STR>& iccid() const = 0;

    /// SIM IMSI read at power-on.
    virtual const FixedString<MODEM_SHORT_STR>& imsi() const = 0;

    /// Full modem identification info read at power-on.
    virtual const ModemInfo& modem_info() const = 0;

    /// Last known SIM status.
    virtual SimStatus sim_status() const = 0;

    /// Last known radio access technology.
    virtual RadioTech radio_tech() const = 0;

    /// Last known registration status (from URC or query).
    virtual RegStatus reg_status() const = 0;

    /// Last known network/PDP context info.
    virtual const NetworkInfo& network_info() const = 0;

    /// Last known PSM mode.
    virtual PsmMode psm_mode() const = 0;

    /// Last known 3GPP PSM configuration.
    virtual const CpsmsConfig& cpsms_config() const = 0;

    /// Last known Telit PSM configuration.
    virtual const TelitCpsmsConfig& telit_cpsms_config() const = 0;

    /// Last known Telit PSM network status.
    virtual const TelitCpsmsStatus& telit_cpsms_status() const = 0;

    /// Last network survey result (populated after a survey action).
    virtual const NetworkSurveyResult& network_survey_result() const = 0;

    /// List of operators found by the last AT+COPS=? scan.
    virtual const StaticVector<Operator, xE310::MAX_OPERATORS>& available_operators() const = 0;

    /// Result of the last AT#CSURV scan (populated by scan_networks()).
    virtual const CsurvResult& csurv_result() const = 0;

    /// Run AT#CSURVF=2 + AT#CSURV and store results internally.
    /// Optionally restrict to channels [start_ch, end_ch]; pass 0 for both to scan full band.
    virtual bool scan_networks(uint32_t start_ch = 0, uint32_t end_ch = 0) = 0;

    /// Pointer to the internal server info array (MAX_SERVER_CONNECTIONS entries, 0-based).
    virtual const ServerInfo* server_info_array() const = 0;

    /// Active configuration.
    virtual const NetworkLteConfig& config() const = 0;

    /// Replace the active configuration (takes effect on the next step cycle).
    virtual void set_config(const NetworkLteConfig& config) = 0;
};

/// Process pending radio LTE requests from channels and dispatch them to network.
/// Call this from the network thread on each iteration to service cross-thread requests.
void process_radio_requests(RadioLteChannels& channels, IRadioLte& radio);

} // namespace modem
