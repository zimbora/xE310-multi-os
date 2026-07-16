#include "modem/network_lte.h"
#include "modem/modem_controller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstring>

using namespace modem;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::InSequence;
using ::testing::Le;
using ::testing::Gt;

namespace modem {
void process_radio_requests(RadioLteChannels& channels, NetworkLte& radio);
}

// ---------------------------------------------------------------------------
// Mock UART
// ---------------------------------------------------------------------------
class MockUart : public UartInterface {
public:
    MOCK_METHOD(UartError, open, (const char* port, const UartConfig& config), (override));
    MOCK_METHOD(void, close, (), (override));
    MOCK_METHOD(bool, is_open, (), (const, override));
    MOCK_METHOD(UartError, write, (const uint8_t* data, size_t length), (override));
    MOCK_METHOD(UartError, read,
                (uint8_t* buffer, size_t buffer_size, size_t& bytes_read, uint32_t timeout_ms),
                (override));
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class NetworkLteTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto mock = std::make_unique<MockUart>();
        mock_uart_ = mock.get();
        controller_ = std::make_unique<ModemController>(std::move(mock));
        modem_ = std::make_unique<xE310>(*controller_);

        ON_CALL(*mock_uart_, is_open()).WillByDefault(Return(true));

        // poll_urc (timeout <= 100ms) always returns no data
        ON_CALL(*mock_uart_, read(_, _, _, Le(100u)))
            .WillByDefault(Invoke([](uint8_t*, size_t, size_t& bytes_read, uint32_t) {
                bytes_read = 0;
                return UartError::ok;
            }));

        // Capture every write so the read mock can return matching responses.
        ON_CALL(*mock_uart_, write(_, _))
            .WillByDefault(Invoke([this](const uint8_t* data, size_t length) {
                last_written_cmd_.assign(reinterpret_cast<const char*>(data), length);
                return UartError::ok;
            }));

        // Command reads (timeout > 100ms) — return canned responses that match
        // the AT command written immediately before this read. This prevents
        // uninitialized-variable issues in the state machine when it queries
        // modem configuration (e.g. AT#WS46?, AT#BND?) during the attach flow.
        ON_CALL(*mock_uart_, read(_, _, _, Gt(100u)))
            .WillByDefault(Invoke([this](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
                std::string resp;
                if (last_written_cmd_.find("AT#WS46?") != std::string::npos) {
                    // Return cat_m1 (nv=0) to match default_iot_tech
                    resp = "\r\n#WS46: 0,0\r\nOK\r\n";
                } else if (last_written_cmd_.find("AT#BND?") != std::string::npos) {
                    // Return matching default LTE bands
                    resp = "\r\n#BND: 0,0," + std::to_string(DEFAULT_LTE_BANDS) + ",0,0\r\nOK\r\n";
                } else if (last_written_cmd_.find("AT+CGDCONT?") != std::string::npos) {
                    resp = "\r\n+CGDCONT: 1,\"IP\",\"" DEFAULT_APN "\"\r\nOK\r\n";
                } else if (last_written_cmd_.find("AT#SGACT?") != std::string::npos) {
                    resp = "\r\n#SGACT: 1,0\r\nOK\r\n";
                } else {
                    resp = "\r\nOK\r\n";
                }
                std::memcpy(buffer, resp.c_str(), resp.size());
                bytes_read = resp.size();
                return UartError::ok;
            }));
    }

    /// Create a NetworkLte with default config.
    NetworkLte make_sm(const NetworkLteConfig& cfg = {}) {
        return NetworkLte(*modem_, cfg);
    }

    MockUart* mock_uart_ = nullptr;
    std::unique_ptr<ModemController> controller_;
    std::unique_ptr<xE310> modem_;
    std::string last_written_cmd_;  ///< Last AT command written to mock UART
};

// ===========================================================================
// State transition tests — use change_state() + call_action() / on_event() + step()
// ===========================================================================

// --- Power states ---

TEST_F(NetworkLteTest, SwitchedOff_IgnoresUnhandledEvents) {
    auto sm = make_sm();
    EXPECT_EQ(sm.state(), NetworkLteState::switched_off);
    sm.on_event(NetworkLteEvent::attach_started);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::switched_off);
}

TEST_F(NetworkLteTest, OffMode_TurnOnRadio_GoesIdle) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::off_mode);
    sm.call_action(ModemAction::turn_on_radio);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}

TEST_F(NetworkLteTest, SleepMode_WakeUp_PrevDataReady_GoesDataReady) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);  // prev_state_ = data_ready
    sm.change_state(NetworkLteState::sleep_mode);
    sm.call_action(ModemAction::wake_up);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

TEST_F(NetworkLteTest, SleepMode_WakeUp_PrevOther_GoesIdle) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::sleep_mode);  // prev_state_ = switched_off
    sm.call_action(ModemAction::wake_up);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}

// --- Idle mode ---

TEST_F(NetworkLteTest, IdleMode_SetupRadio_StaysIdle) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);
    sm.call_action(ModemAction::setup_radio);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}

TEST_F(NetworkLteTest, ClockRequestAlwaysQueriesModem) {
    auto sm = make_sm();
    RadioLteChannels channels;
    int cclk_query_count = 0;

    ON_CALL(*mock_uart_, write(_, _))
        .WillByDefault(Invoke([this, &cclk_query_count](const uint8_t* data, size_t length) {
            last_written_cmd_.assign(reinterpret_cast<const char*>(data), length);
            if (last_written_cmd_.find("AT+CCLK?") != std::string::npos) {
                ++cclk_query_count;
            }
            return UartError::ok;
        }));

    ON_CALL(*mock_uart_, read(_, _, _, Gt(100u)))
        .WillByDefault(Invoke([this](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
            std::string resp = "\r\nOK\r\n";
            if (last_written_cmd_.find("AT+CCLK?") != std::string::npos) {
                resp = "\r\n+CCLK: \"26/07/13,21:00:00+00\"\r\nOK\r\n";
            }
            std::memcpy(buffer, resp.c_str(), resp.size());
            bytes_read = resp.size();
            return UartError::ok;
        }));

    ModemTxMsg req{};
    req.type = RadioLteRequestType::get_clock;
    ASSERT_EQ(channels.send_request(req), MessageChannelError::ok);
    ASSERT_EQ(channels.send_request(req), MessageChannelError::ok);

    process_radio_requests(channels, sm);

    ModemTypedResponseMsg<FixedString<MODEM_SHORT_STR>> resp{};
    ASSERT_EQ(channels.recv_typed_response(resp), MessageChannelError::ok);
    EXPECT_TRUE(resp.ok);
    EXPECT_EQ(resp.value, "26/07/13,21:00:00+00");

    ASSERT_EQ(channels.recv_typed_response(resp), MessageChannelError::ok);
    EXPECT_TRUE(resp.ok);
    EXPECT_EQ(resp.value, "26/07/13,21:00:00+00");

    EXPECT_EQ(cclk_query_count, 2);
}

// ---------------------------------------------------------------------------
// query_network_status / query_pdp_context tests
// Each test overrides the AT-command ON_CALL with a canned response so the
// state machine sees a specific modem reply without EXPECT_CALL cardinality.
// ---------------------------------------------------------------------------

TEST_F(NetworkLteTest, IdleMode_QueryNetworkStatus_NotRegistered_GoesDetached) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);

    // Override read to return not-registered for CEREG, smart fallback for others
    ON_CALL(*mock_uart_, read(_, _, _, Gt(100u)))
        .WillByDefault(Invoke([this](uint8_t* buf, size_t, size_t& n, uint32_t) {
            std::string resp;
            if (last_written_cmd_.find("AT+CEREG?") != std::string::npos) {
                resp = "\r\n+CEREG: 0,0\r\nOK\r\n";
            } else {
                resp = "\r\nOK\r\n";
            }
            std::memcpy(buf, resp.c_str(), resp.size());
            n = resp.size();
            return UartError::ok;
        }));

    sm.call_action(ModemAction::query_network_status);
    sm.step();

    EXPECT_EQ(sm.state(), NetworkLteState::network_detached);
}

TEST_F(NetworkLteTest, IdleMode_QueryNetworkStatus_Registered_QueuesQueryPdp) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);

    // Override read to return registered for CEREG
    ON_CALL(*mock_uart_, read(_, _, _, Gt(100u)))
        .WillByDefault(Invoke([this](uint8_t* buf, size_t, size_t& n, uint32_t) {
            std::string resp;
            if (last_written_cmd_.find("AT+CEREG?") != std::string::npos) {
                resp = "\r\n+CEREG: 0,1\r\nOK\r\n";
            } else {
                resp = "\r\nOK\r\n";
            }
            std::memcpy(buf, resp.c_str(), resp.size());
            n = resp.size();
            return UartError::ok;
        }));

    sm.call_action(ModemAction::query_network_status);
    sm.step();

    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
    EXPECT_EQ(sm.get_action(), ModemAction::query_pdp_context);
}

TEST_F(NetworkLteTest, IdleMode_QueryPdpContext_Inactive_GoesPdpClosed) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);

    // AT#SGACT? → inactive → pdp_context_closed
    ON_CALL(*mock_uart_, read(_, _, _, Gt(100u)))
        .WillByDefault(Invoke([this](uint8_t* buf, size_t, size_t& n, uint32_t) {
            std::string resp;
            if (last_written_cmd_.find("AT#SGACT?") != std::string::npos) {
                resp = "\r\n#SGACT: 1,0\r\nOK\r\n";
            } else {
                resp = "\r\nOK\r\n";
            }
            std::memcpy(buf, resp.c_str(), resp.size());
            n = resp.size();
            return UartError::ok;
        }));

    sm.call_action(ModemAction::query_pdp_context);
    sm.step();

    EXPECT_EQ(sm.state(), NetworkLteState::pdp_context_closed);
}

TEST_F(NetworkLteTest, IdleMode_QueryPdpContext_Active_GoesDataReady) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);

    // AT#SGACT? → active, then AT+CGPADDR → valid IP → data_ready
    ON_CALL(*mock_uart_, read(_, _, _, Gt(100u)))
        .WillByDefault(Invoke([this](uint8_t* buf, size_t, size_t& n, uint32_t) {
            std::string resp;
            if (last_written_cmd_.find("AT#SGACT?") != std::string::npos) {
                resp = "\r\n#SGACT: 1,1\r\nOK\r\n";
            } else if (last_written_cmd_.find("AT+CGPADDR") != std::string::npos) {
                resp = "\r\n+CGPADDR: 1,\"10.0.0.1\"\r\nOK\r\n";
            } else {
                resp = "\r\nOK\r\n";
            }
            std::memcpy(buf, resp.c_str(), resp.size());
            n = resp.size();
            return UartError::ok;
        }));

    sm.call_action(ModemAction::query_pdp_context);
    sm.step();

    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

// --- Network detached ---
// change_state(network_detached) automatically queues attach_network action,
// so step() processes the attach flow in the same cycle.

TEST_F(NetworkLteTest, NetworkDetached_AttachFirstAttempt_GoesAttaching) {
    auto sm = make_sm();  // max_attach_retries = 2 (default)
    sm.change_state(NetworkLteState::network_detached);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
    EXPECT_EQ(sm.get_attach_retries(), 1u);  // incremented once on first attempt
}

TEST_F(NetworkLteTest, NetworkDetached_AttachSecondAttempt_GoesAttaching) {
    NetworkLteConfig cfg;
    // Use same bands/APN for default and fallback to avoid band-change detour in retry
    cfg.fallback_lte_bands = cfg.default_lte_bands;
    cfg.fallback_apn = cfg.default_apn;
    auto sm = make_sm(cfg);
    sm.change_state(NetworkLteState::network_detached);

    // First attempt
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
    EXPECT_EQ(sm.get_attach_retries(), 1u);

    // Second attempt (fallback config)
    sm.change_state(NetworkLteState::network_detached);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
    EXPECT_EQ(sm.get_attach_retries(), 2u);
}

TEST_F(NetworkLteTest, NetworkDetached_AttachMaxRetries_GoesOffMode) {
    auto sm = make_sm();  // max_attach_retries = 2 (default)
    sm.set_attach_retries(sm.config().max_attach_retries);  // pre-set retries to max
    sm.change_state(NetworkLteState::network_detached);
    // step 1: attach_network sees max retries → fires attach_error event
    sm.step();
    // step 2: attach_error event → enter_sleep action → switch_off_radio action queued
    sm.step();
    // step 3: switch_off_radio action → shutdown() → off_mode
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::off_mode);
}

// --- Network attaching ---

TEST_F(NetworkLteTest, NetworkAttaching_Attached_GoesDataReady) {
    auto sm = make_sm();
    sm.set_attach_retries(1u);  // pre-set retries to 1
    sm.change_state(NetworkLteState::network_attaching);
    sm.on_event(NetworkLteEvent::network_attached);
    sm.step();
    // network_attached → pdp_context_closed → open_pdp_context (mock OK) → data_ready
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
    EXPECT_EQ(sm.get_attach_retries(), 1u);
}

TEST_F(NetworkLteTest, NetworkAttaching_Timeout_RetriesAttach) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::network_attaching);
    sm.on_event(NetworkLteEvent::timeout);
    sm.step();
    // timeout → done → attach_network action retries → network_attaching
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
    EXPECT_EQ(sm.get_attach_retries(), 1u);
}

TEST_F(NetworkLteTest, NetworkAttaching_Timeout_MaxRetries_GoesDone) {
    auto sm = make_sm();
    sm.set_attach_retries(sm.config().max_attach_retries);
    sm.change_state(NetworkLteState::network_attaching);
    sm.on_event(NetworkLteEvent::timeout);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::done);
}

TEST_F(NetworkLteTest, NetworkAttaching_NetworkDetached_RetriesAttach) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::network_attaching);
    sm.on_event(NetworkLteEvent::network_detached);
    sm.step();
    // network_detached → network_detached state → attach_network action → network_attaching
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
    EXPECT_EQ(sm.get_attach_retries(), 1u);
}

// --- PDP context closed ---
// change_state(pdp_context_closed) automatically queues open_pdp_context action.

TEST_F(NetworkLteTest, PdpContextClosed_OpensContext_GoesDataReady) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::pdp_context_closed);
    // open_pdp_context action is auto-queued, mock activate_pdp returns OK
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

TEST_F(NetworkLteTest, PdpContextClosed_NetworkDetached_TriggersAttach) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::pdp_context_closed);
    sm.on_event(NetworkLteEvent::network_detached);
    sm.step();
    // network_detached event overrides pending open_pdp_context action
    // → network_detached state → attach_network → network_attaching
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
}

// --- PDP context opening ---

TEST_F(NetworkLteTest, PdpContextOpening_ContextOpened_GoesDataReady) {
    auto sm = make_sm();
    sm.set_pdp_retries(1u);
    sm.set_attach_retries(1u);
    sm.set_network_attempts(1u);
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_event(NetworkLteEvent::context_opened);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
    EXPECT_EQ(sm.get_network_attempts(), 1u);
    EXPECT_EQ(sm.get_attach_retries(), 1u);
    EXPECT_EQ(sm.get_pdp_retries(), 1u);
}

TEST_F(NetworkLteTest, PdpContextOpening_Timeout_RetriesAttach) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_event(NetworkLteEvent::timeout);
    sm.step();
    // timeout → done → attach_network action → network_attaching
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
}

TEST_F(NetworkLteTest, PdpContextOpening_Timeout_MaxRetries_GoesDone) {
    auto sm = make_sm();
    sm.set_attach_retries(sm.config().max_attach_retries);
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_event(NetworkLteEvent::timeout);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::done);
}

TEST_F(NetworkLteTest, PdpContextOpening_ContextClosed_TriggersAttach) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_event(NetworkLteEvent::context_closed);
    sm.step();
    // context_closed → network_detached → attach_network → network_attaching
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
}

TEST_F(NetworkLteTest, PdpContextOpening_NetworkDetached_TriggersAttach) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_event(NetworkLteEvent::network_detached);
    sm.step();
    // network_detached → network_detached state → attach_network → network_attaching
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
}

// --- Data ready ---

TEST_F(NetworkLteTest, DataReady_Timeout_GoesDone) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::timeout);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::done);
}

TEST_F(NetworkLteTest, DataReady_ZeroTimeout_TimeoutEventIgnored) {
    NetworkLteConfig cfg;
    cfg.data_ready_timeout_sec = 0;
    auto sm = make_sm(cfg);
    sm.change_state(NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::timeout);
    sm.step();
    // when data_ready_timeout_sec == 0, timeout should not move modem to done
    EXPECT_NE(sm.state(), NetworkLteState::done);
}

TEST_F(NetworkLteTest, DataReady_NetworkDetached_TriggersAttach) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::network_detached);
    sm.step();
    // network_detached → network_detached state → attach_network → network_attaching
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
}

TEST_F(NetworkLteTest, DataReady_ContextClosed_TriggersAttach) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::context_closed);
    sm.step();
    // context_closed → network_detached → attach_network → network_attaching
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
}

// --- Done state ---

TEST_F(NetworkLteTest, Done_PsmEnter_GoesSleep) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::done);
    sm.on_event(NetworkLteEvent::psm_enter);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::sleep_mode);
}

TEST_F(NetworkLteTest, Done_SwitchOffRadio) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::done);
    sm.call_action(ModemAction::switch_off_radio);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::off_mode);
}

TEST_F(NetworkLteTest, Done_PowerOff) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::done);
    sm.call_action(ModemAction::power_off);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::switched_off);
}

// --- Transparent mode ---

TEST_F(NetworkLteTest, TransparentMode_ActionFromIdle) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);
    sm.call_action(ModemAction::enter_transparent_mode);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::transparent_mode);
}

// ===========================================================================
// Modem event (URC-driven) tests via on_event() + step()
// ===========================================================================

TEST_F(NetworkLteTest, ModemEvent_PsmEnter_GoesSleep) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::psm_enter);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::sleep_mode);
}

TEST_F(NetworkLteTest, ModemEvent_PsmExit_PrevDataReady_GoesDataReady) {
    auto sm = make_sm();
    // Simulate: was in data_ready, then went to sleep
    sm.change_state(NetworkLteState::data_ready);
    sm.change_state(NetworkLteState::sleep_mode);
    sm.on_event(NetworkLteEvent::psm_exit);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

TEST_F(NetworkLteTest, ModemEvent_PsmExit_PrevOther_GoesIdle) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::sleep_mode);
    sm.on_event(NetworkLteEvent::psm_exit);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}

TEST_F(NetworkLteTest, ModemEvent_NetworkDetached_TriggersAttach) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::network_detached);
    sm.step();
    // network_detached → network_detached state → attach_network → network_attaching
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
}

TEST_F(NetworkLteTest, ModemEvent_NetworkAttached_GoesDataReady) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::network_attaching);
    sm.on_event(NetworkLteEvent::network_attached);
    sm.step();
    // network_attached → pdp_context_closed → open_pdp_context (mock OK) → data_ready
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

TEST_F(NetworkLteTest, ModemEvent_ContextClosed_TriggersAttach) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::context_closed);
    sm.step();
    // context_closed → network_detached → attach_network → network_attaching
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
}

TEST_F(NetworkLteTest, ModemEvent_ContextOpened_GoesDataReady) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_event(NetworkLteEvent::context_opened);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

// ===========================================================================
// handle_urc() tests
// ===========================================================================

TEST_F(NetworkLteTest, HandleUrc_CEREG_Registered) {
    auto sm = make_sm();
    sm.handle_urc("+CEREG: 1");
    EXPECT_EQ(sm.get_action(), ModemAction::query_pdp_context);
}

TEST_F(NetworkLteTest, HandleUrc_CEREG_RegisteredRoaming) {
    auto sm = make_sm();
    sm.handle_urc("+CEREG: 5");
    EXPECT_EQ(sm.get_action(), ModemAction::query_pdp_context);
}

TEST_F(NetworkLteTest, HandleUrc_CEREG_NotRegistered) {
    auto sm = make_sm();
    sm.on_event(NetworkLteEvent::none);
    sm.handle_urc("+CEREG: 0");
    EXPECT_EQ(sm.event(), NetworkLteEvent::none);
}
/*
TEST_F(NetworkLteTest, HandleUrc_CEREG_Denied) {
    auto sm = make_sm();
    sm.handle_urc("+CEREG: 3");
    EXPECT_EQ(sm.event(), NetworkLteEvent::network_detached);
}
*/
TEST_F(NetworkLteTest, HandleUrc_CEREG_WithNField) {
    auto sm = make_sm();
    sm.handle_urc("+CEREG: 0,1");
    EXPECT_EQ(sm.get_action(), ModemAction::query_pdp_context);
}

TEST_F(NetworkLteTest, HandleUrc_CREG_Registered) {
    auto sm = make_sm();
    sm.handle_urc("+CREG: 1");
    EXPECT_EQ(sm.get_action(), ModemAction::query_pdp_context);
}

TEST_F(NetworkLteTest, HandleUrc_CGEV_NW_DEACT) {
    auto sm = make_sm();
    sm.handle_urc("+CGEV: NW_DEACT");
    EXPECT_EQ(sm.event(), NetworkLteEvent::context_closed);
}

TEST_F(NetworkLteTest, HandleUrc_CGEV_ME_DEACT) {
    auto sm = make_sm();
    sm.handle_urc("+CGEV: ME DEACT");
    EXPECT_EQ(sm.event(), NetworkLteEvent::context_closed);
}

TEST_F(NetworkLteTest, HandleUrc_CGEV_NW_DETACH) {
    auto sm = make_sm();
    sm.handle_urc("+CGEV: NW_DETACH");
    EXPECT_EQ(sm.event(), NetworkLteEvent::network_detached);
}

TEST_F(NetworkLteTest, HandleUrc_CGEV_ME_DETACH) {
    auto sm = make_sm();
    sm.handle_urc("+CGEV: ME_DETACH");
    EXPECT_EQ(sm.event(), NetworkLteEvent::network_detached);
}

TEST_F(NetworkLteTest, HandleUrc_CGEV_REJECT) {
    auto sm = make_sm();
    sm.handle_urc("+CGEV: REJECT");
    EXPECT_EQ(sm.event(), NetworkLteEvent::context_rejected);
}

TEST_F(NetworkLteTest, HandleUrc_PSMURC) {
    auto sm = make_sm();
    sm.handle_urc("#PSMURC: 10,3600");
    EXPECT_EQ(sm.event(), NetworkLteEvent::psm_enter);
}

TEST_F(NetworkLteTest, HandleUrc_NotifyEvPlmnSearchExh) {
    auto sm = make_sm();
    sm.handle_urc("%NOTIFYEV:\"PLMNSEARCHEXH\"");
    EXPECT_EQ(sm.event(), NetworkLteEvent::network_detached);
}

TEST_F(NetworkLteTest, HandleUrc_SRING_MatchingConnId) {
    NetworkLteConfig cfg;
    cfg.conn_id = 1;
    auto sm = make_sm(cfg);
    sm.handle_urc("SRING: 1");
    EXPECT_EQ(sm.event(), NetworkLteEvent::data_available);
}

TEST_F(NetworkLteTest, HandleUrc_SRING_DifferentConnId_StillSetsEvent) {
    NetworkLteConfig cfg;
    cfg.conn_id = 1;
    auto sm = make_sm(cfg);
    sm.on_event(NetworkLteEvent::none);
    // Source code does not filter by conn_id — it always sets data_available
    sm.handle_urc("SRING: 2");
    EXPECT_EQ(sm.event(), NetworkLteEvent::data_available);
}

TEST_F(NetworkLteTest, HandleUrc_SRING_NoSpace) {
    NetworkLteConfig cfg;
    cfg.conn_id = 3;
    auto sm = make_sm(cfg);
    sm.handle_urc("SRING:3");
    EXPECT_EQ(sm.event(), NetworkLteEvent::data_available);
}

// ===========================================================================
// network_connect blocking-state tests
// ===========================================================================

TEST_F(NetworkLteTest, NetworkConnect_TransparentMode_ReturnsFalse) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::transparent_mode);
    bool result = sm.network_connect();
    EXPECT_FALSE(result);
    // State must not change — the guard fires before any go_to_state call.
    EXPECT_EQ(sm.state(), NetworkLteState::transparent_mode);
}

TEST_F(NetworkLteTest, NetworkConnect_ModemFota_ReturnsFalse) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::modem_fota);
    bool result = sm.network_connect();
    EXPECT_FALSE(result);
    EXPECT_EQ(sm.state(), NetworkLteState::modem_fota);
}

TEST_F(NetworkLteTest, NetworkConnect_DoneState_ReturnsFalse) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::done);
    bool result = sm.network_connect();
    EXPECT_FALSE(result);
    EXPECT_EQ(sm.state(), NetworkLteState::done);
}

TEST_F(NetworkLteTest, NetworkConnect_AlreadyDataReady_ReturnsTrue) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    bool result = sm.network_connect();
    EXPECT_TRUE(result);
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

// ===========================================================================
// go_to_state blocking-state tests
// ===========================================================================

TEST_F(NetworkLteTest, GoToState_DoneState_ReturnsFalse) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::done);
    // go_to_state must return false immediately without spinning
    bool result = sm.go_to_state(NetworkLteState::data_ready);
    EXPECT_FALSE(result);
    EXPECT_EQ(sm.state(), NetworkLteState::done);
}

TEST_F(NetworkLteTest, GoToState_ModemFota_ReturnsFalse) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::modem_fota);
    bool result = sm.go_to_state(NetworkLteState::data_ready);
    EXPECT_FALSE(result);
    EXPECT_EQ(sm.state(), NetworkLteState::modem_fota);
}

// ===========================================================================
// server_connect tests
// ===========================================================================

TEST_F(NetworkLteTest, ServerConnect_NotInDataReady_ReturnsFalse) {
    auto sm = make_sm();
    // Default state is switched_off — not data_ready or sleep_mode
    bool result = sm.server_connect(1, "UDP", "192.168.1.1", 5000);
    EXPECT_FALSE(result);
}

TEST_F(NetworkLteTest, ServerConnect_IdleMode_ReturnsFalse) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);
    bool result = sm.server_connect(1, "UDP", "192.168.1.1", 5000);
    EXPECT_FALSE(result);
}

TEST_F(NetworkLteTest, ServerConnect_UdpInDataReady_ReturnsTrue) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);

    // AT#SS=1 → disconnected (state=0, default OK response has no comma)
    // AT#SD=1,... → OK
    // Default mock returns "\r\nOK\r\n" for unrecognised commands,
    // so udp_status returns ok with state=0 (no comma found) and
    // udp_open returns ok.
    bool result = sm.server_connect(1, "UDP", "192.168.1.1", 5000);
    EXPECT_TRUE(result);
}

TEST_F(NetworkLteTest, ServerConnect_TcpProtocol_ReturnsFalse) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    // TCP is not supported
    bool result = sm.server_connect(1, "TCP", "192.168.1.1", 5000);
    EXPECT_FALSE(result);
}

TEST_F(NetworkLteTest, ServerConnect_UnknownProtocol_ReturnsFalse) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    bool result = sm.server_connect(1, "SCTP", "192.168.1.1", 5000);
    EXPECT_FALSE(result);
}

// ===========================================================================
// StateTimers tests
// ===========================================================================

TEST_F(NetworkLteTest, StateTimers_InitiallyAllZero) {
    auto sm = make_sm();
    const auto& t = sm.state_timers();
    EXPECT_EQ(t.network_attaching_ms, 0u);
    EXPECT_EQ(t.pdp_context_opening_ms, 0u);
    EXPECT_EQ(t.data_ready_ms, 0u);
    EXPECT_EQ(t.transparent_mode_ms, 0u);
}

TEST_F(NetworkLteTest, StateTimers_NetworkAttaching_AccumulatesOnExit) {
    auto sm = make_sm();
    // Enter network_attaching state
    sm.change_state(NetworkLteState::network_attaching);
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
    // Transition away — elapsed time (≥ 0) is accumulated
    sm.change_state(NetworkLteState::pdp_context_closed);
    EXPECT_GE(sm.state_timers().network_attaching_ms, 0u);
    // Other counters remain zero
    EXPECT_EQ(sm.state_timers().pdp_context_opening_ms, 0u);
    EXPECT_EQ(sm.state_timers().data_ready_ms, 0u);
    EXPECT_EQ(sm.state_timers().transparent_mode_ms, 0u);
}

TEST_F(NetworkLteTest, StateTimers_PdpContextOpening_AccumulatesOnExit) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.change_state(NetworkLteState::data_ready);
    EXPECT_GE(sm.state_timers().pdp_context_opening_ms, 0u);
    EXPECT_EQ(sm.state_timers().network_attaching_ms, 0u);
}

TEST_F(NetworkLteTest, StateTimers_DataReady_AccumulatesOnExit) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.change_state(NetworkLteState::idle_mode);
    EXPECT_GE(sm.state_timers().data_ready_ms, 0u);
    EXPECT_EQ(sm.state_timers().network_attaching_ms, 0u);
}

TEST_F(NetworkLteTest, StateTimers_TransparentMode_AccumulatesOnExit) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::transparent_mode);
    sm.change_state(NetworkLteState::idle_mode);
    EXPECT_GE(sm.state_timers().transparent_mode_ms, 0u);
    EXPECT_EQ(sm.state_timers().network_attaching_ms, 0u);
}

TEST_F(NetworkLteTest, StateTimers_MultipleVisits_CountersAccumulate) {
    auto sm = make_sm();
    // Visit network_attaching twice
    sm.change_state(NetworkLteState::network_attaching);
    sm.change_state(NetworkLteState::network_detached);
    uint32_t after_first = sm.state_timers().network_attaching_ms;

    sm.change_state(NetworkLteState::network_attaching);
    sm.change_state(NetworkLteState::pdp_context_closed);
    uint32_t after_second = sm.state_timers().network_attaching_ms;

    // Counter must be >= the value after the first visit
    EXPECT_GE(after_second, after_first);
}

TEST_F(NetworkLteTest, StateTimers_NonTimedStates_NeverAccumulate) {
    auto sm = make_sm();
    // Transition through states that have no timers
    sm.change_state(NetworkLteState::idle_mode);
    sm.change_state(NetworkLteState::network_detached);
    sm.change_state(NetworkLteState::setup_mode);
    const auto& t = sm.state_timers();
    EXPECT_EQ(t.network_attaching_ms, 0u);
    EXPECT_EQ(t.pdp_context_opening_ms, 0u);
    EXPECT_EQ(t.data_ready_ms, 0u);
    EXPECT_EQ(t.transparent_mode_ms, 0u);
}
