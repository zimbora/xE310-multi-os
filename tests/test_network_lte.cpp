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

        // Command reads (timeout > 100ms) default to returning OK
        ON_CALL(*mock_uart_, read(_, _, _, Gt(100u)))
            .WillByDefault(Invoke([](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
                std::string resp = "\r\nOK\r\n";
                std::memcpy(buffer, resp.c_str(), resp.size());
                bytes_read = resp.size();
                return UartError::ok;
            }));

        // Writes always succeed
        ON_CALL(*mock_uart_, write(_, _)).WillByDefault(Return(UartError::ok));
    }

    /// Create a NetworkLte with default config.
    NetworkLte make_sm(const NetworkLteConfig& cfg = {}) {
        return NetworkLte(*modem_, cfg);
    }

    MockUart* mock_uart_ = nullptr;
    std::unique_ptr<ModemController> controller_;
    std::unique_ptr<xE310> modem_;
};

// ===========================================================================
// State transition tests — use change_state() to set up, on_event() + step()
// ===========================================================================

// --- Power states ---

TEST_F(NetworkLteTest, SwitchedOff_IgnoresOtherEvents) {
    auto sm = make_sm();
    EXPECT_EQ(sm.state(), NetworkLteState::switched_off);
    sm.on_event(NetworkLteEvent::wake_up);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::switched_off);
}

TEST_F(NetworkLteTest, OffMode_TurnOnRadio_GoesIdle) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::off_mode);
    sm.on_event(NetworkLteEvent::turn_on_radio);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}

TEST_F(NetworkLteTest, SleepMode_WakeUp_PrevDataReady_GoesDataReady) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);  // prev_state_ = data_ready
    sm.change_state(NetworkLteState::sleep_mode);
    sm.on_event(NetworkLteEvent::wake_up);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

TEST_F(NetworkLteTest, SleepMode_WakeUp_PrevOther_GoesIdle) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::sleep_mode);  // prev_state_ = switched_off
    sm.on_event(NetworkLteEvent::wake_up);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}

// --- Idle mode ---

TEST_F(NetworkLteTest, IdleMode_SetupRadio_GoesSetup) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);
    sm.on_event(NetworkLteEvent::setup_radio);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::setup_mode);
}

// ---------------------------------------------------------------------------
// query_network_status / query_network_context tests
// Each test overrides the AT-command ON_CALL with a canned response so the
// state machine sees a specific modem reply without EXPECT_CALL cardinality.
// ---------------------------------------------------------------------------

TEST_F(NetworkLteTest, IdleMode_QueryNetworkStatus_NotRegistered_GoesDetached) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);

    // AT+CEREG? → stat=0 (not_registered) → network_detached + attach_started
    ON_CALL(*mock_uart_, read(_, _, _, Gt(100u)))
        .WillByDefault(Invoke([](uint8_t* buf, size_t, size_t& n, uint32_t) {
            std::string resp = "\r\n+CEREG: 0,0\r\nOK\r\n";
            std::memcpy(buf, resp.c_str(), resp.size());
            n = resp.size();
            return UartError::ok;
        }));

    sm.on_event(NetworkLteEvent::query_network_status);
    sm.step();

    EXPECT_EQ(sm.state(), NetworkLteState::network_detached);
    EXPECT_GT(sm.get_network_attempts(), 0u);
}

TEST_F(NetworkLteTest, IdleMode_QueryNetworkStatus_Registered_SetsQueryContextEvent) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);

    // AT+CEREG? → stat=1 (registered_home) → stays idle, event becomes query_network_context
    ON_CALL(*mock_uart_, read(_, _, _, Gt(100u)))
        .WillByDefault(Invoke([](uint8_t* buf, size_t, size_t& n, uint32_t) {
            std::string resp = "\r\n+CEREG: 0,1\r\nOK\r\n";
            std::memcpy(buf, resp.c_str(), resp.size());
            n = resp.size();
            return UartError::ok;
        }));

    sm.on_event(NetworkLteEvent::query_network_status);
    sm.step();

    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
    EXPECT_EQ(sm.event(), NetworkLteEvent::query_network_context);
}

TEST_F(NetworkLteTest, IdleMode_QueryNetworkContext_NoIp_GoesPdpClosed) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);

    // AT+CGPADDR=1 → empty quoted IP → pdp_context_closed + pdp_opening
    ON_CALL(*mock_uart_, read(_, _, _, Gt(100u)))
        .WillByDefault(Invoke([](uint8_t* buf, size_t, size_t& n, uint32_t) {
            std::string resp = "\r\n+CGPADDR: 1,\"\"\r\nOK\r\n";
            std::memcpy(buf, resp.c_str(), resp.size());
            n = resp.size();
            return UartError::ok;
        }));

    sm.on_event(NetworkLteEvent::query_network_context);
    sm.step();

    EXPECT_EQ(sm.state(), NetworkLteState::pdp_context_closed);
    EXPECT_EQ(sm.event(), NetworkLteEvent::pdp_opening);
}

TEST_F(NetworkLteTest, IdleMode_QueryNetworkContext_WithIp_GoesDataReady) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);

    // AT+CGPADDR=1 → valid IP → data_ready
    ON_CALL(*mock_uart_, read(_, _, _, Gt(100u)))
        .WillByDefault(Invoke([](uint8_t* buf, size_t, size_t& n, uint32_t) {
            std::string resp = "\r\n+CGPADDR: 1,\"10.0.0.1\"\r\nOK\r\n";
            std::memcpy(buf, resp.c_str(), resp.size());
            n = resp.size();
            return UartError::ok;
        }));

    sm.on_event(NetworkLteEvent::query_network_context);
    sm.step();

    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

// --- Network detached ---

TEST_F(NetworkLteTest, NetworkDetached_AttachStarted_GoesAttaching) {
    auto sm = make_sm();  // max_attach_retries = 2 (default)
    sm.change_state(NetworkLteState::network_detached);
    sm.on_event(NetworkLteEvent::attach_started);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
    EXPECT_EQ(sm.get_attach_retries(), 1u);  // incremented once on first attempt
}

TEST_F(NetworkLteTest, NetworkDetached_AttachStarted_SecondAttempt_GoesAttaching) {
    auto sm = make_sm();  // max_attach_retries = 2 (default)
    sm.change_state(NetworkLteState::network_detached);

    // First attempt
    sm.on_event(NetworkLteEvent::attach_started);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
    EXPECT_EQ(sm.get_attach_retries(), 1u);

    // Second attempt
    sm.change_state(NetworkLteState::network_detached);
    sm.on_event(NetworkLteEvent::attach_started);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_attaching);
    EXPECT_EQ(sm.get_attach_retries(), 2u);
}

TEST_F(NetworkLteTest, NetworkDetached_AttachStarted_MaxRetries_GoesDone) {
    auto sm = make_sm();  // max_attach_retries = 2 (default)
    sm.set_attach_retries(sm.config().max_attach_retries);  // pre-set retries to max
    sm.change_state(NetworkLteState::network_detached);
    sm.on_event(NetworkLteEvent::attach_started);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::done);
    EXPECT_EQ(sm.get_attach_retries(), sm.config().max_attach_retries);  // unchanged — exhausted before attempt
}

// --- Network attaching ---

TEST_F(NetworkLteTest, NetworkAttaching_Attached_GoesPdpContextClosedWithPdpEvent) {
    auto sm = make_sm();
    sm.set_attach_retries(1u);  // pre-set retries to 1    
    sm.change_state(NetworkLteState::network_attaching);
    sm.on_event(NetworkLteEvent::network_attached);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::pdp_context_closed);
    EXPECT_EQ(sm.event(), NetworkLteEvent::pdp_opening);
    EXPECT_EQ(sm.get_attach_retries(), 1u);
}

TEST_F(NetworkLteTest, NetworkAttaching_Timeout_GoesDetached) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::network_attaching);
    sm.on_event(NetworkLteEvent::timeout);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_detached);
}

TEST_F(NetworkLteTest, NetworkAttaching_NetworkDetached_GoesDetached) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::network_attaching);
    sm.on_event(NetworkLteEvent::network_detached);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_detached);
}

// --- PDP context closed ---

TEST_F(NetworkLteTest, PdpContextClosed_PdpOpening_FirstAttempt_GoesOpening) {
    auto sm = make_sm();  // max_pdp_retries = 2 (default)
    sm.change_state(NetworkLteState::pdp_context_closed);
    sm.on_event(NetworkLteEvent::pdp_opening);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::pdp_context_opening);
    EXPECT_EQ(sm.get_pdp_retries(), 1u);  // incremented once on first attempt
}

TEST_F(NetworkLteTest, PdpContextClosed_PdpOpening_SecondAttempt_GoesOpening) {
    auto sm = make_sm();  // max_pdp_retries = 2 (default)
    sm.change_state(NetworkLteState::pdp_context_closed);

    // First attempt
    sm.on_event(NetworkLteEvent::pdp_opening);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::pdp_context_opening);
    EXPECT_EQ(sm.get_pdp_retries(), 1u);

    // Second attempt (fallback APN path)
    sm.change_state(NetworkLteState::pdp_context_closed);
    sm.on_event(NetworkLteEvent::pdp_opening);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::pdp_context_opening);
    EXPECT_EQ(sm.get_pdp_retries(), 2u);
}

TEST_F(NetworkLteTest, PdpContextClosed_PdpOpening_MaxRetries_GoesDone) {
    auto sm = make_sm();  // max_pdp_retries = 2 (default)
    sm.set_pdp_retries(sm.config().max_pdp_retries);  // pre-set retries to max
    sm.change_state(NetworkLteState::pdp_context_closed);
    sm.on_event(NetworkLteEvent::pdp_opening);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::done);
    EXPECT_EQ(sm.get_pdp_retries(), sm.config().max_pdp_retries);  // unchanged — exhausted before attempt
}

TEST_F(NetworkLteTest, PdpContextClosed_NetworkDetached) {
    auto sm = make_sm();
    sm.set_attach_retries(1u); // pre-set attach retries to 1 to verify it doesn't get reset on network loss
    sm.change_state(NetworkLteState::pdp_context_closed);
    sm.on_event(NetworkLteEvent::network_detached);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_detached);
    EXPECT_EQ(sm.get_attach_retries(), 1u); // attach retries shouldn't be reset when network is lost, even if we were in PDP closed
}

// --- PDP context opening ---

TEST_F(NetworkLteTest, PdpContextOpening_ContextOpened_GoesDataReady) {
    auto sm = make_sm();
    sm.set_pdp_retries(1u); // pre-set PDP retries to 1 to verify it doesn't get reset on timeout
    sm.set_attach_retries(1u); // pre-set attach retries to 1 to verify it doesn't get reset on timeout
    sm.set_network_attempts(1u); // pre-set network attempts to 1 to verify it doesn't get reset on timeout
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_event(NetworkLteEvent::context_opened);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
    EXPECT_EQ(sm.get_network_attempts(), 1u); // no reset on success, avoid endless loop
    EXPECT_EQ(sm.get_attach_retries(), 1u);  // no reset on success, avoid endless loop
    EXPECT_EQ(sm.get_pdp_retries(), 1u);  // no reset on success, avoid endless loop
}

TEST_F(NetworkLteTest, PdpContextOpening_Timeout_GoesPdpClosed) {
    auto sm = make_sm();
    sm.set_pdp_retries(1u); // pre-set PDP retries to 1 to verify it doesn't get reset on timeout
    sm.set_attach_retries(1u); // pre-set attach retries to 1 to verify it doesn't get reset on timeout
    sm.set_network_attempts(1u); // pre-set network attempts to 1 to verify it doesn't get reset on timeout
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_event(NetworkLteEvent::timeout);
    sm.step();
    // nNetworkAttempts(0) < max_pdp_retries(2)
    EXPECT_EQ(sm.state(), NetworkLteState::pdp_context_closed);
    EXPECT_EQ(sm.get_network_attempts(), 1u);      // expect not be reset
    EXPECT_EQ(sm.get_attach_retries(), 1u);  // expect not be reset
    EXPECT_EQ(sm.get_pdp_retries(), 1u);  // expect not be reset
}

TEST_F(NetworkLteTest, PdpContextOpening_ContextRejected_GoesPdpClosed) {
    auto sm = make_sm();
    sm.set_pdp_retries(1u); // pre-set PDP retries to 1 to verify it doesn't get reset on context_rejected
    sm.set_attach_retries(1u); // pre-set attach retries to 1 to verify it doesn't get reset on context_rejected
    sm.set_network_attempts(1u); // pre-set network attempts to 1 to verify it doesn't get reset on context_rejected
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_event(NetworkLteEvent::context_rejected);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::pdp_context_closed);
    EXPECT_EQ(sm.get_network_attempts(), 1u);      // expect not be reset
    EXPECT_EQ(sm.get_attach_retries(), 1u);  // expect not be reset
    EXPECT_EQ(sm.get_pdp_retries(), 1u);  // expect not be reset
}

TEST_F(NetworkLteTest, PdpContextOpening_ContextClosed_GoesPdpClosed) {
    auto sm = make_sm();
    sm.set_pdp_retries(1u); // pre-set PDP retries to 1 to verify it doesn't get reset on context_closed
    sm.set_attach_retries(1u); // pre-set attach retries to 1 to verify it doesn't get reset on context_closed
    sm.set_network_attempts(1u); // pre-set network attempts to 1 to verify it doesn't get reset on context_closed
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_event(NetworkLteEvent::context_closed);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::pdp_context_closed);
    EXPECT_EQ(sm.get_network_attempts(), 1u);      // expect not be reset
    EXPECT_EQ(sm.get_attach_retries(), 1u);  // expect not be reset
    EXPECT_EQ(sm.get_pdp_retries(), 1u);  // expect not be reset
}

TEST_F(NetworkLteTest, PdpContextOpening_NetworkDetached) {
    auto sm = make_sm();
    sm.set_pdp_retries(1u); // pre-set PDP retries to 1 to verify it doesn't get reset on network_detached
    sm.set_attach_retries(1u); // pre-set attach retries to 1 to verify it doesn't get reset on network_detached
    sm.set_network_attempts(1u); // pre-set network attempts to 1 to verify it doesn't get reset on network_detached
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_event(NetworkLteEvent::network_detached);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_detached);
    EXPECT_EQ(sm.get_network_attempts(), 1u);      // expect not be reset
    EXPECT_EQ(sm.get_attach_retries(), 1u);  // expect not be reset
    EXPECT_EQ(sm.get_pdp_retries(), 1u);  // expect not be reset

}

// --- Data ready ---

TEST_F(NetworkLteTest, DataReady_DataComplete_GoesDone) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::data_complete);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::done);
    EXPECT_EQ(sm.event(), NetworkLteEvent::psm_enter);
}

TEST_F(NetworkLteTest, DataReady_NetworkDetached) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::network_detached);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_detached);
}

TEST_F(NetworkLteTest, DataReady_ContextClosed_GoesPdpClosed) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::context_closed);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::pdp_context_closed);
}

// --- Done state ---

TEST_F(NetworkLteTest, Done_EnterSleep) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::done);
    sm.on_event(NetworkLteEvent::enter_sleep);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::sleep_mode);
}

TEST_F(NetworkLteTest, Done_SwitchOffRadio) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::done);
    sm.on_event(NetworkLteEvent::switch_off_radio);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::off_mode);
}

TEST_F(NetworkLteTest, Done_PowerOff) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::done);
    sm.on_event(NetworkLteEvent::power_off);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::switched_off);
}

// --- Transparent mode ---

TEST_F(NetworkLteTest, TransparentMode_EventFromIdle) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::idle_mode);
    sm.on_event(NetworkLteEvent::transparent_mode);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::transparent_mode);
}

// ===========================================================================
// Modem event (URC-driven) tests via on_modem_event()
// ===========================================================================

TEST_F(NetworkLteTest, ModemEvent_PsmEnter_GoesSleep) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_modem_event(NetworkLteEvent::psm_enter);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::sleep_mode);
}

TEST_F(NetworkLteTest, ModemEvent_PsmExit_PrevDataReady_GoesDataReady) {
    auto sm = make_sm();
    // Simulate: was in data_ready, then went to sleep
    sm.change_state(NetworkLteState::data_ready);
    sm.change_state(NetworkLteState::sleep_mode);
    sm.on_modem_event(NetworkLteEvent::psm_exit);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

TEST_F(NetworkLteTest, ModemEvent_PsmExit_PrevOther_GoesIdle) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::sleep_mode);
    sm.on_modem_event(NetworkLteEvent::psm_exit);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}

TEST_F(NetworkLteTest, ModemEvent_NetworkDetached) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_modem_event(NetworkLteEvent::network_detached);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_detached);
}

TEST_F(NetworkLteTest, ModemEvent_NetworkAttached_GoesPdpClosed) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::network_attaching);
    sm.on_modem_event(NetworkLteEvent::network_attached);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::pdp_context_closed);
}

TEST_F(NetworkLteTest, ModemEvent_ContextClosed_GoesDetached) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::data_ready);
    sm.on_modem_event(NetworkLteEvent::context_closed);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::network_detached);
}

TEST_F(NetworkLteTest, ModemEvent_ContextOpened_GoesDataReady) {
    auto sm = make_sm();
    sm.change_state(NetworkLteState::pdp_context_opening);
    sm.on_modem_event(NetworkLteEvent::context_opened);
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

// ===========================================================================
// handle_urc() tests
// ===========================================================================

TEST_F(NetworkLteTest, HandleUrc_CEREG_Registered) {
    auto sm = make_sm();
    sm.handle_urc("+CEREG: 1");
    EXPECT_EQ(sm.event(), NetworkLteEvent::network_attached);
}

TEST_F(NetworkLteTest, HandleUrc_CEREG_RegisteredRoaming) {
    auto sm = make_sm();
    sm.handle_urc("+CEREG: 5");
    EXPECT_EQ(sm.event(), NetworkLteEvent::network_attached);
}

TEST_F(NetworkLteTest, HandleUrc_CEREG_NotRegistered) {
    auto sm = make_sm();
    sm.handle_urc("+CEREG: 0");
    EXPECT_EQ(sm.event(), NetworkLteEvent::network_detached);
}

TEST_F(NetworkLteTest, HandleUrc_CEREG_Denied) {
    auto sm = make_sm();
    sm.handle_urc("+CEREG: 3");
    EXPECT_EQ(sm.event(), NetworkLteEvent::network_detached);
}

TEST_F(NetworkLteTest, HandleUrc_CEREG_WithNField) {
    auto sm = make_sm();
    sm.handle_urc("+CEREG: 0,1");
    EXPECT_EQ(sm.event(), NetworkLteEvent::network_attached);
}

TEST_F(NetworkLteTest, HandleUrc_CREG_Registered) {
    auto sm = make_sm();
    sm.handle_urc("+CREG: 1");
    EXPECT_EQ(sm.event(), NetworkLteEvent::network_attached);
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

TEST_F(NetworkLteTest, HandleUrc_SRING_MatchingConnId) {
    NetworkLteConfig cfg;
    cfg.conn_id = 1;
    auto sm = make_sm(cfg);
    sm.handle_urc("SRING: 1");
    EXPECT_EQ(sm.event(), NetworkLteEvent::data_available);
}

TEST_F(NetworkLteTest, HandleUrc_SRING_DifferentConnId_NoEvent) {
    NetworkLteConfig cfg;
    cfg.conn_id = 1;
    auto sm = make_sm(cfg);
    sm.on_event(NetworkLteEvent::none);
    sm.handle_urc("SRING: 2");
    EXPECT_NE(sm.event(), NetworkLteEvent::data_available);
}

TEST_F(NetworkLteTest, HandleUrc_SRING_NoSpace) {
    NetworkLteConfig cfg;
    cfg.conn_id = 3;
    auto sm = make_sm(cfg);
    sm.handle_urc("SRING:3");
    EXPECT_EQ(sm.event(), NetworkLteEvent::data_available);
}
