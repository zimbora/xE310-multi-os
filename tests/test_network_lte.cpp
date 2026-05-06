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
    }

    /// Create a NetworkLte with default config.
    NetworkLte make_sm(const NetworkLteConfig& cfg = {}) {
        return NetworkLte(*modem_, cfg);
    }

    /// Drive the SM to a desired state using on_event() calls.
    void drive_to(NetworkLte& sm, NetworkLteState target) {
        // Power on → idle
        if (sm.state() == NetworkLteState::switched_off && target != NetworkLteState::switched_off) {
            sm.on_event(NetworkLteEvent::power_on);
        }
        if (target == NetworkLteState::idle_mode) return;
        if (target == NetworkLteState::off_mode) {
            sm.on_event(NetworkLteEvent::switch_off_radio);
            return;
        }
        if (target == NetworkLteState::sleep_mode) {
            // idle → detached → attaching → ctx → pdp → server → data → done → sleep
            // Shortcut: idle → done (network_error) → sleep
            sm.on_event(NetworkLteEvent::network_error); // idle → done
            sm.on_event(NetworkLteEvent::enter_sleep);    // done → sleep
            return;
        }
        if (target == NetworkLteState::detached) {
            sm.on_event(NetworkLteEvent::attach_started);
            return;
        }
        if (target == NetworkLteState::attaching) {
            sm.on_event(NetworkLteEvent::network_attached);
            //sm.on_event(NetworkLteEvent::attach_started);
            return;
        }
        if (target == NetworkLteState::context_deactivated) {
            sm.on_event(NetworkLteEvent::query_network_attached);
            return;
        }
        if (target == NetworkLteState::opening_pdp_context) {
            sm.on_event(NetworkLteEvent::query_network_attached);
            sm.on_event(NetworkLteEvent::pdp_opening);
            return;
        }
        if (target == NetworkLteState::server_disconnected) {
            sm.on_event(NetworkLteEvent::server_connect);
            return;
        }
        if (target == NetworkLteState::server_registering) {
            return;
        }
        if (target == NetworkLteState::server_bootstrap) {
            sm.on_event(NetworkLteEvent::rejected);
            return;
        }
        if (target == NetworkLteState::data_ready) {
            sm.on_event(NetworkLteEvent::authenticated);
            return;
        }
        if (target == NetworkLteState::done) {
            sm.on_event(NetworkLteEvent::network_error);
            return;
        }
    }

    /// Set up a mock AT command exchange (write ok, read specific response).
    void expect_command_ok(const std::string& response_body) {
        EXPECT_CALL(*mock_uart_, write(_, _))
            .WillOnce(Return(UartError::ok));
        EXPECT_CALL(*mock_uart_, read(_, _, _, _))
            .WillOnce(Invoke([response_body](uint8_t* buffer, size_t, size_t& bytes_read, uint32_t) {
                std::string resp = response_body.empty()
                    ? "\r\nOK\r\n"
                    : "\r\n" + response_body + "\r\n\r\nOK\r\n";
                std::memcpy(buffer, resp.c_str(), resp.size());
                bytes_read = resp.size();
                return UartError::ok;
            }));
    }

    MockUart* mock_uart_ = nullptr;
    std::unique_ptr<ModemController> controller_;
    std::unique_ptr<xE310> modem_;
};

// ===========================================================================
// on_event() tests — pure state transition logic
// ===========================================================================

// --- Power states ---

TEST_F(NetworkLteTest, SwitchedOff_PowerOn) {
    auto sm = make_sm();
    EXPECT_EQ(sm.state(), NetworkLteState::switched_off);
    sm.on_event(NetworkLteEvent::power_on);
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
    EXPECT_EQ(sm.try_count(), 0);
}

TEST_F(NetworkLteTest, SwitchedOff_IgnoresOtherEvents) {
    auto sm = make_sm();
    sm.on_event(NetworkLteEvent::wake_up);
    EXPECT_EQ(sm.state(), NetworkLteState::switched_off);
    sm.on_event(NetworkLteEvent::send_data);
    EXPECT_EQ(sm.state(), NetworkLteState::switched_off);
}

TEST_F(NetworkLteTest, OffMode_TurnOnRadio) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::off_mode);
    EXPECT_EQ(sm.state(), NetworkLteState::off_mode);
    sm.on_event(NetworkLteEvent::turn_on_radio);
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}

TEST_F(NetworkLteTest, SleepMode_WakeUp) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::sleep_mode);
    EXPECT_EQ(sm.state(), NetworkLteState::sleep_mode);
    sm.on_event(NetworkLteEvent::wake_up);
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}

// --- Idle mode routing ---

TEST_F(NetworkLteTest, IdleMode_QueryDetached) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::idle_mode);
    sm.on_event(NetworkLteEvent::query_network_detached);
    EXPECT_EQ(sm.state(), NetworkLteState::detached);
    EXPECT_EQ(sm.try_count(), 0);
}

TEST_F(NetworkLteTest, IdleMode_QueryAttached) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::idle_mode);
    sm.on_event(NetworkLteEvent::query_network_attached);
    EXPECT_EQ(sm.state(), NetworkLteState::context_deactivated);
    EXPECT_EQ(sm.try_count(), 0);
}

TEST_F(NetworkLteTest, IdleMode_QueryServerDeregistered) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::idle_mode);
    sm.on_event(NetworkLteEvent::query_server_deregistered);
    EXPECT_EQ(sm.state(), NetworkLteState::server_disconnected);
}

TEST_F(NetworkLteTest, IdleMode_NetworkError) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::idle_mode);
    sm.on_event(NetworkLteEvent::network_error);
    EXPECT_EQ(sm.state(), NetworkLteState::done);
}

TEST_F(NetworkLteTest, IdleMode_SwitchOffRadio) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::idle_mode);
    sm.on_event(NetworkLteEvent::switch_off_radio);
    EXPECT_EQ(sm.state(), NetworkLteState::off_mode);
}

// --- Attach flow ---

TEST_F(NetworkLteTest, Detached_AttachStarted) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::detached);
    sm.on_event(NetworkLteEvent::attach_started);
    EXPECT_EQ(sm.state(), NetworkLteState::attaching);
    EXPECT_EQ(sm.try_count(), 1);
}

TEST_F(NetworkLteTest, Attaching_NetworkAttached) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::attaching);
    sm.on_event(NetworkLteEvent::network_attached);
    EXPECT_EQ(sm.state(), NetworkLteState::context_deactivated);
    EXPECT_EQ(sm.try_count(), 0);
}

TEST_F(NetworkLteTest, Attaching_TimeoutRetry) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::attaching);
    // try_count == 1 after drive_to, max == 2
    sm.on_event(NetworkLteEvent::timeout);
    EXPECT_EQ(sm.state(), NetworkLteState::detached);
    EXPECT_EQ(sm.try_count(), 1); // unchanged, will increment on next attach_started
}

TEST_F(NetworkLteTest, Attaching_TimeoutGiveUp) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::attaching);
    // First timeout → retry
    sm.on_event(NetworkLteEvent::timeout);
    EXPECT_EQ(sm.state(), NetworkLteState::detached);
    // Second attach attempt
    sm.on_event(NetworkLteEvent::attach_started);
    EXPECT_EQ(sm.state(), NetworkLteState::attaching);
    EXPECT_EQ(sm.try_count(), 2);
    // Second timeout → give up (try_count >= max_attach_retries)
    sm.on_event(NetworkLteEvent::timeout);
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
    EXPECT_EQ(sm.try_count(), 0);
}

TEST_F(NetworkLteTest, Attaching_NetworkLossRetry) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::attaching);
    sm.on_event(NetworkLteEvent::network_loss);
    EXPECT_EQ(sm.state(), NetworkLteState::detached);
}

TEST_F(NetworkLteTest, Attaching_NetworkLossGiveUp) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::attaching);
    sm.on_event(NetworkLteEvent::network_loss); // try 1 → detached
    sm.on_event(NetworkLteEvent::attach_started); // try 2 → attaching
    sm.on_event(NetworkLteEvent::network_loss); // try 2 >= max → idle
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}

// --- PDP context flow ---

TEST_F(NetworkLteTest, ContextDeactivated_PdpOpening) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::context_deactivated);
    sm.on_event(NetworkLteEvent::pdp_opening);
    EXPECT_EQ(sm.state(), NetworkLteState::opening_pdp_context);
    EXPECT_EQ(sm.try_count(), 1);
}

TEST_F(NetworkLteTest, ContextDeactivated_NetworkLoss) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::context_deactivated);
    sm.on_event(NetworkLteEvent::network_loss);
    EXPECT_EQ(sm.state(), NetworkLteState::detached);
    EXPECT_EQ(sm.try_count(), 0);
}

TEST_F(NetworkLteTest, OpeningPdp_ContextActivated) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::opening_pdp_context);
    sm.on_event(NetworkLteEvent::context_activated);
    EXPECT_EQ(sm.state(), NetworkLteState::server_disconnected);
    EXPECT_EQ(sm.try_count(), 0);
}

TEST_F(NetworkLteTest, OpeningPdp_TimeoutRetry) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::opening_pdp_context);
    // try_count == 1, max == 2
    sm.on_event(NetworkLteEvent::timeout);
    EXPECT_EQ(sm.state(), NetworkLteState::context_deactivated);
}

TEST_F(NetworkLteTest, OpeningPdp_TimeoutGiveUp) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::opening_pdp_context);
    sm.on_event(NetworkLteEvent::timeout); // retry
    sm.on_event(NetworkLteEvent::pdp_opening); // try 2
    sm.on_event(NetworkLteEvent::timeout); // give up
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
    EXPECT_EQ(sm.try_count(), 0);
}

TEST_F(NetworkLteTest, OpeningPdp_ContextLossRetry) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::opening_pdp_context);
    sm.on_event(NetworkLteEvent::context_loss);
    EXPECT_EQ(sm.state(), NetworkLteState::context_deactivated);
}

TEST_F(NetworkLteTest, OpeningPdp_NetworkLoss) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::opening_pdp_context);
    sm.on_event(NetworkLteEvent::network_loss);
    EXPECT_EQ(sm.state(), NetworkLteState::detached);
    EXPECT_EQ(sm.try_count(), 0);
}

// --- Server flow ---

TEST_F(NetworkLteTest, ServerDeregistered_Connecting) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::server_disconnected);
    sm.on_event(NetworkLteEvent::server_connect);
    EXPECT_EQ(sm.state(), NetworkLteState::server_registering);
}

TEST_F(NetworkLteTest, ServerDeregistered_ContextLoss) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::server_disconnected);
    sm.on_event(NetworkLteEvent::context_loss);
    EXPECT_EQ(sm.state(), NetworkLteState::context_deactivated);
}

TEST_F(NetworkLteTest, ServerRegistering_Authenticated) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::server_registering);
    sm.on_event(NetworkLteEvent::authenticated);
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

TEST_F(NetworkLteTest, ServerRegistering_Rejected) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::server_registering);
    sm.on_event(NetworkLteEvent::rejected);
    EXPECT_EQ(sm.state(), NetworkLteState::server_bootstrap);
}

TEST_F(NetworkLteTest, ServerRegistering_Timeout) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::server_registering);
    sm.on_event(NetworkLteEvent::timeout_server);
    EXPECT_EQ(sm.state(), NetworkLteState::server_disconnected);
}

TEST_F(NetworkLteTest, ServerBootstrap_Complete) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::server_bootstrap);
    sm.on_event(NetworkLteEvent::bootstrap_complete);
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

TEST_F(NetworkLteTest, ServerBootstrap_Timeout) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::server_bootstrap);
    sm.on_event(NetworkLteEvent::timeout_server);
    EXPECT_EQ(sm.state(), NetworkLteState::server_disconnected);
}

// --- Data ready / Done ---

TEST_F(NetworkLteTest, DataReady_Complete) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::data_complete);
    EXPECT_EQ(sm.state(), NetworkLteState::done);
}

TEST_F(NetworkLteTest, DataReady_ContextLoss) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::data_ready);
    sm.on_event(NetworkLteEvent::context_loss);
    EXPECT_EQ(sm.state(), NetworkLteState::context_deactivated);
}

TEST_F(NetworkLteTest, Done_EnterSleep) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::done);
    sm.on_event(NetworkLteEvent::enter_sleep);
    EXPECT_EQ(sm.state(), NetworkLteState::sleep_mode);
}

TEST_F(NetworkLteTest, Done_Psm) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::done);
    sm.on_event(NetworkLteEvent::psm);
    EXPECT_EQ(sm.state(), NetworkLteState::sleep_mode);
}

TEST_F(NetworkLteTest, Done_SwitchOff) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::done);
    sm.on_event(NetworkLteEvent::switch_off_radio);
    EXPECT_EQ(sm.state(), NetworkLteState::off_mode);
}

TEST_F(NetworkLteTest, Done_SendData) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::done);
    sm.on_event(NetworkLteEvent::send_data);
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

TEST_F(NetworkLteTest, Done_TimeoutServer) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::done);
    sm.on_event(NetworkLteEvent::timeout_server);
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);
}

// ===========================================================================
// step() tests — modem integration via mock UART
// ===========================================================================

TEST_F(NetworkLteTest, Step_SwitchedOff_GoesIdle) {
    auto sm = make_sm();
    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}

TEST_F(NetworkLteTest, Step_IdleMode_NotRegistered_GoesDetached) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::idle_mode);

    // AT+CEREG? → not registered (searching)
    InSequence seq;
    expect_command_ok("+CEREG: 0,2");

    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::detached);
}

TEST_F(NetworkLteTest, Step_IdleMode_RegisteredNoPdp_GoesContextDeactivated) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::idle_mode);

    InSequence seq;
    // AT+CEREG? → registered home
    expect_command_ok("+CEREG: 0,1");
    // AT+CGACT? → not active
    expect_command_ok("+CGACT: 1,0");

    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::context_deactivated);
}

TEST_F(NetworkLteTest, Step_IdleMode_RegisteredPdpActive_GoesServerDeregistered) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::idle_mode);

    InSequence seq;
    // AT+CEREG? → registered home
    expect_command_ok("+CEREG: 0,1");
    // AT+CGACT? → active
    expect_command_ok("+CGACT: 1,1");

    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::server_disconnected);
}

TEST_F(NetworkLteTest, Step_Detached_ConfiguresAndGoesAttaching) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::detached);

    InSequence seq;
    // AT#BND=... (set bands)
    expect_command_ok("");
    // AT#WS46=... (set iot tech)
    expect_command_ok("");

    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::attaching);
    EXPECT_EQ(sm.try_count(), 1);
}

TEST_F(NetworkLteTest, Step_Attaching_Registered_GoesContextDeactivated) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::attaching);

    InSequence seq;
    // AT+CEREG? → registered roaming
    expect_command_ok("+CEREG: 0,5");

    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::context_deactivated);
    EXPECT_EQ(sm.try_count(), 0);
}

TEST_F(NetworkLteTest, Step_Attaching_StillSearching_StaysAttaching) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::attaching);

    InSequence seq;
    // AT+CEREG? → searching
    expect_command_ok("+CEREG: 0,2");

    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::attaching);
}

TEST_F(NetworkLteTest, Step_ContextDeactivated_ActivatesPdp) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::context_deactivated);

    InSequence seq;
    // AT+CGDCONT=1,"IP","internet"
    expect_command_ok("");
    // AT+CGACT=1,1
    expect_command_ok("");

    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::opening_pdp_context);
    EXPECT_EQ(sm.try_count(), 1);
}

TEST_F(NetworkLteTest, Step_OpeningPdp_Active_GoesServerDeregistered) {
    auto sm = make_sm();
    drive_to(sm, NetworkLteState::opening_pdp_context);

    InSequence seq;
    // AT+CGACT? → active
    expect_command_ok("+CGACT: 1,1");

    sm.step();
    EXPECT_EQ(sm.state(), NetworkLteState::server_disconnected);
    EXPECT_EQ(sm.try_count(), 0);
}

// ===========================================================================
// Full flow integration test
// ===========================================================================

TEST_F(NetworkLteTest, FullHappyPath) {
    auto sm = make_sm();
    EXPECT_EQ(sm.state(), NetworkLteState::switched_off);

    // Power on
    sm.on_event(NetworkLteEvent::power_on);
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);

    // Not registered → detached
    sm.on_event(NetworkLteEvent::query_network_attached);
    EXPECT_EQ(sm.state(), NetworkLteState::detached);

    // Configure & start attaching
    sm.on_event(NetworkLteEvent::attach_started);
    EXPECT_EQ(sm.state(), NetworkLteState::attaching);

    // Registered
    sm.on_event(NetworkLteEvent::network_attached);
    EXPECT_EQ(sm.state(), NetworkLteState::context_deactivated);

    // Open PDP
    sm.on_event(NetworkLteEvent::pdp_opening);
    EXPECT_EQ(sm.state(), NetworkLteState::opening_pdp_context);

    // PDP activated
    sm.on_event(NetworkLteEvent::context_activated);
    EXPECT_EQ(sm.state(), NetworkLteState::server_disconnected);

    // Server connect
    sm.on_event(NetworkLteEvent::query_server_connected);
    EXPECT_EQ(sm.state(), NetworkLteState::server_registering);

    // Auth success
    sm.on_event(NetworkLteEvent::authenticated);
    EXPECT_EQ(sm.state(), NetworkLteState::data_ready);

    // Data done
    sm.on_event(NetworkLteEvent::data_complete);
    EXPECT_EQ(sm.state(), NetworkLteState::done);

    // Enter sleep
    sm.on_event(NetworkLteEvent::enter_sleep);
    EXPECT_EQ(sm.state(), NetworkLteState::sleep_mode);

    // Wake up
    sm.on_event(NetworkLteEvent::wake_up);
    EXPECT_EQ(sm.state(), NetworkLteState::idle_mode);
}
