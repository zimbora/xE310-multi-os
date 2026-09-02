#pragma once

#include "hal/event_flags_factory.h"
#include "hal/fixed_string.h"
#include "hal/message_channel_factory.h"
#include "hal/message_queue_interface.h"
#include "modem/network_lte_config.h"
#include "hal/static_vector.h"
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
constexpr uint32_t MODEM_EVT_REQUEST = (1U << 0);
constexpr uint32_t MODEM_EVT_RESPONSE = (1U << 1);
constexpr uint32_t MODEM_EVT_STATE = (1U << 2);
constexpr uint32_t MODEM_EVT_LOG = (1U << 3);
/// Posted after a blocking action (scan_networks, connect, disconnect, force_psm) completes.
constexpr uint32_t MODEM_EVT_ACTION_DONE = (1U << 4);

/// Maximum time in ms to wait for an initial ACK to any i_radio_lte request.
constexpr uint32_t MODEM_ACK_TIMEOUT_MS = 3000U;

/// Request message type for cross-thread radio LTE state queries/actions.
enum class RadioLteRequestType : uint8_t {
    get_registration_info,
    get_signal_quality,
    get_iccid,
    get_imsi,
    get_clock,
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
    server_connect,
    server_disconnect,
    force_psm,
    get_timers,
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

struct ModemServerConnectMsg {
    RadioLteRequestType type = RadioLteRequestType::server_connect;
    uint8_t conn_id = 1;
    uint16_t port = 0;
    FixedString<MODEM_SHORT_STR> protocol;
    FixedString<MODEM_MEDIUM_STR> ip;
};

static_assert(sizeof(ModemServerConnectMsg) <= MESSAGE_CHANNEL_MAX_DATA,
              "ModemServerConnectMsg exceeds MESSAGE_CHANNEL_MAX_DATA");

/// Completion notification sent after a blocking action finishes.
/// Consumed via recv_action_complete() after waiting on MODEM_EVT_ACTION_DONE.
struct ModemActionCompleteMsg {
    RadioLteRequestType type = RadioLteRequestType::get_registration_info;
    bool result = false;
};

static_assert(sizeof(ModemActionCompleteMsg) <= MESSAGE_CHANNEL_MAX_DATA,
              "ModemActionCompleteMsg exceeds MESSAGE_CHANNEL_MAX_DATA");

using RadioLteRequestMsg = ModemTxMsg;

template<typename ValueType> struct ModemTypedResponseMsg {
    bool ok = false;
    ValueType value{};
};

static_assert(sizeof(ModemTypedResponseMsg<NetworkLteConfig>) <= MESSAGE_CHANNEL_MAX_DATA,
              "ModemTypedResponseMsg<NetworkLteConfig> exceeds MESSAGE_CHANNEL_MAX_DATA");

/// Accumulated time (in milliseconds) spent in each timed state since the NetworkLte instance was created.
/// Counters are incremented each time the state machine exits a timed state, by the number of milliseconds
/// spent in that state during that visit.
struct StateTimers {
    uint32_t network_attaching_ms = 0;   ///< Total ms spent in network_attaching across all visits.
    uint32_t pdp_context_opening_ms = 0; ///< Total ms spent in pdp_context_opening across all visits.
    uint32_t data_ready_ms = 0;          ///< Total ms spent in data_ready across all visits.
    uint32_t transparent_mode_ms = 0;    ///< Total ms spent in transparent_mode across all visits.
    uint32_t gnss_fix_mode_ms = 0;       ///< Total ms spent in gnss_fix_mode across all visits.
    uint32_t sleep_mode_ms = 0;          ///< Total ms spent in sleep_mode across all visits.
    uint32_t off_mode_ms = 0;            ///< Total ms spent in off_mode across all visits.
};

static_assert(sizeof(ModemTypedResponseMsg<StateTimers>) <= MESSAGE_CHANNEL_MAX_DATA,
              "ModemTypedResponseMsg<StateTimers> exceeds MESSAGE_CHANNEL_MAX_DATA");

/// Event payload describing the current LTE state machine snapshot.
struct ModemStateMsg {
    NetworkLteState state = static_cast<NetworkLteState>(0);
    NetworkLteEvent event = static_cast<NetworkLteEvent>(0);
};

/// Fixed-size log line forwarded through modem_log_q.
struct ModemLogMsg {
    FixedString<MODEM_LONG_STR> text;
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

    MessageChannelError send_request(const ModemServerConnectMsg& msg, uint32_t timeout_ms = 0) {
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

    /// Send a blocking-action completion notification and post MODEM_EVT_ACTION_DONE.
    void publish_action_complete(RadioLteRequestType type, bool result, uint32_t timeout_ms = 0) {
        ModemActionCompleteMsg msg{type, result};
        MessageChannelError err = send_message(*modem_rx_q, msg, timeout_ms);
        if (err == MessageChannelError::ok) {
            modem_evt->set(MODEM_EVT_ACTION_DONE);
        }
    }

    /// Read a completion notification from the rx channel (call after MODEM_EVT_ACTION_DONE fires).
    MessageChannelError recv_action_complete(ModemActionCompleteMsg& msg, uint32_t timeout_ms = 0) {
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
        static_assert(std::is_trivially_copyable_v<MessageType>, "MessageType must be trivially copyable");
        return channel.send(reinterpret_cast<const uint8_t*>(&msg), sizeof(MessageType), timeout_ms);
    }

    template<typename MessageType>
    static MessageChannelError receive_message(MessageChannelInterface& channel, MessageType& msg,
                                               uint32_t timeout_ms) {
        static_assert(std::is_trivially_copyable_v<MessageType>, "MessageType must be trivially copyable");
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

/// Interface for thread-safe payload queue access outside the radio request path.
class IRadioDataQueue {
public:
    IRadioDataQueue() = default;
    IRadioDataQueue(const IRadioDataQueue&) = delete;
    IRadioDataQueue& operator=(const IRadioDataQueue&) = delete;
    IRadioDataQueue(IRadioDataQueue&&) = delete;
    IRadioDataQueue& operator=(IRadioDataQueue&&) = delete;
    virtual ~IRadioDataQueue() = default;

    /// Queue payload data for the given connection ID (1-based).
    virtual QueueError tx_write(uint8_t conn_id, const uint8_t* data, size_t length) = 0;

    /// Read queued RX payload data for the given connection ID (1-based).
    virtual QueueError rx_read(uint8_t conn_id, QueueMessage& msg) = 0;
};

} // namespace modem
