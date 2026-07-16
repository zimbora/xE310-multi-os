#include "i_radio_lte_internal.h"
#include "modem/i_radio_lte.h"
#include "modem/modem_controller.h"
#include "modem/network_lte.h"
#include "modem/uart_interface.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>

using namespace modem;
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

// ---------------------------------------------------------------------------
// Mock UART (mirrors test_network_lte.cpp)
// ---------------------------------------------------------------------------
class MockUartIrl : public UartInterface {
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
// RadioLteChannels unit tests (no NetworkLte required)
// ---------------------------------------------------------------------------

TEST(RadioLteChannelsTest, AckTimeoutConstantIs3000ms) {
    EXPECT_EQ(MODEM_ACK_TIMEOUT_MS, 3000U);
}

TEST(RadioLteChannelsTest, PublishActionCompleteSignalsActionDoneEvent) {
    RadioLteChannels channels;
    channels.publish_action_complete(RadioLteRequestType::scan_networks, true);

    uint32_t events = channels.wait(MODEM_EVT_ACTION_DONE, true, 100);
    EXPECT_NE(events & MODEM_EVT_ACTION_DONE, 0U);
}

TEST(RadioLteChannelsTest, RecvActionCompleteReadsTypeAndResult) {
    RadioLteChannels channels;
    channels.publish_action_complete(RadioLteRequestType::network_connect, false);

    // Drain the event
    channels.wait(MODEM_EVT_ACTION_DONE, true, 100);

    ModemActionCompleteMsg msg{};
    EXPECT_EQ(channels.recv_action_complete(msg, 0), MessageChannelError::ok);
    EXPECT_EQ(msg.type, RadioLteRequestType::network_connect);
    EXPECT_FALSE(msg.result);
}

TEST(RadioLteChannelsTest, PublishActionCompleteDoesNotSetResponseEvent) {
    RadioLteChannels channels;
    channels.publish_action_complete(RadioLteRequestType::force_psm, true);

    // MODEM_EVT_RESPONSE must NOT be set — only ACTION_DONE
    uint32_t response_event = channels.wait(MODEM_EVT_RESPONSE, false, 0);
    EXPECT_EQ(response_event & MODEM_EVT_RESPONSE, 0U);
}

TEST(RadioLteChannelsTest, PublishTypedResponseDoesNotSetActionDoneEvent) {
    RadioLteChannels channels;
    channels.publish_typed_response(true);

    uint32_t action_done = channels.wait(MODEM_EVT_ACTION_DONE, false, 0);
    EXPECT_EQ(action_done & MODEM_EVT_ACTION_DONE, 0U);
}

// ---------------------------------------------------------------------------
// Fixture for process_radio_requests tests
// ---------------------------------------------------------------------------
class IRadioLteTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto mock = std::make_unique<MockUartIrl>();
        mock_uart_ = mock.get();
        controller_ = std::make_unique<ModemController>(std::move(mock));
        modem_ = std::make_unique<xE310>(*controller_);

        ON_CALL(*mock_uart_, is_open()).WillByDefault(Return(true));

        // URC polls (short timeout) → no data
        ON_CALL(*mock_uart_, read(_, _, _, ::testing::Le(100u)))
            .WillByDefault(Invoke([](uint8_t*, size_t, size_t& n, uint32_t) {
                n = 0;
                return UartError::ok;
            }));

        // Capture written commands
        ON_CALL(*mock_uart_, write(_, _))
            .WillByDefault(Invoke([this](const uint8_t* data, size_t len) {
                last_cmd_.assign(reinterpret_cast<const char*>(data), len);
                return UartError::ok;
            }));

        // AT command reads → OK by default; specific overrides in each test
        ON_CALL(*mock_uart_, read(_, _, _, ::testing::Gt(100u)))
            .WillByDefault(Invoke([this](uint8_t* buf, size_t, size_t& n, uint32_t) {
                std::string resp = "\r\nOK\r\n";
                std::memcpy(buf, resp.c_str(), resp.size());
                n = resp.size();
                return UartError::ok;
            }));
    }

    NetworkLte make_radio(const NetworkLteConfig& cfg = {}) {
        return NetworkLte(*modem_, cfg);
    }

    MockUartIrl* mock_uart_ = nullptr;
    std::unique_ptr<ModemController> controller_;
    std::unique_ptr<xE310> modem_;
    std::string last_cmd_;
};

// ---------------------------------------------------------------------------
// Non-blocking request: get_registration_info
// Verifies the existing behaviour (single MODEM_EVT_RESPONSE) still works.
// ---------------------------------------------------------------------------
TEST_F(IRadioLteTest, GetRegistrationInfoReturnsImmediateResponse) {
    auto radio = make_radio();
    RadioLteChannels channels;

    ModemTxMsg req{RadioLteRequestType::get_registration_info, 0, 0};
    ASSERT_EQ(channels.send_request(req, 1000), MessageChannelError::ok);

    // Run process_radio_requests in a worker thread
    std::thread worker([&]() { process_radio_requests(channels, radio); });

    // Should receive MODEM_EVT_RESPONSE quickly (non-blocking op)
    uint32_t matched = channels.wait(MODEM_EVT_RESPONSE, true, 1000);
    EXPECT_NE(matched & MODEM_EVT_RESPONSE, 0U);

    // No ACTION_DONE should arrive for a non-blocking op
    uint32_t action_done = channels.wait(MODEM_EVT_ACTION_DONE, false, 0);
    EXPECT_EQ(action_done & MODEM_EVT_ACTION_DONE, 0U);

    ModemTypedResponseMsg<RegistrationInfo> resp{};
    EXPECT_EQ(channels.recv_typed_response(resp, 0), MessageChannelError::ok);
    EXPECT_TRUE(resp.ok);

    worker.join();
}

// ---------------------------------------------------------------------------
// Blocking request: scan_networks
// Verifies the two-step protocol:
//   1. Immediate ACK via MODEM_EVT_RESPONSE (within MODEM_ACK_TIMEOUT_MS)
//   2. Deferred completion via MODEM_EVT_ACTION_DONE after the AT scan
// ---------------------------------------------------------------------------
TEST_F(IRadioLteTest, ScanNetworksReturnsImmediateAckThenActionDone) {
    // Make AT#CSURVF and AT#CSURV return quickly
    ON_CALL(*mock_uart_, read(_, _, _, ::testing::Gt(100u)))
        .WillByDefault(Invoke([this](uint8_t* buf, size_t, size_t& n, uint32_t) {
            std::string resp;
            if (last_cmd_.find("AT#CSURVF") != std::string::npos ||
                last_cmd_.find("AT#CSURV") != std::string::npos) {
                resp = "\r\nOK\r\n";
            } else {
                resp = "\r\nOK\r\n";
            }
            std::memcpy(buf, resp.c_str(), resp.size());
            n = resp.size();
            return UartError::ok;
        }));

    auto radio = make_radio();
    RadioLteChannels channels;

    ModemTxMsg req{RadioLteRequestType::scan_networks, 0, 0};
    ASSERT_EQ(channels.send_request(req, 1000), MessageChannelError::ok);

    std::thread worker([&]() { process_radio_requests(channels, radio); });

    // Step 1: ACK must arrive within MODEM_ACK_TIMEOUT_MS
    auto t0 = std::chrono::steady_clock::now();
    uint32_t ack_event = channels.wait(MODEM_EVT_RESPONSE, true, MODEM_ACK_TIMEOUT_MS);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();

    EXPECT_NE(ack_event & MODEM_EVT_RESPONSE, 0U) << "No ACK received within MODEM_ACK_TIMEOUT_MS";
    EXPECT_LT(elapsed_ms, static_cast<long long>(MODEM_ACK_TIMEOUT_MS))
        << "ACK took longer than MODEM_ACK_TIMEOUT_MS";

    ModemTypedResponseMsg<bool> ack{};
    ASSERT_EQ(channels.recv_typed_response(ack, 0), MessageChannelError::ok);
    EXPECT_TRUE(ack.ok);
    EXPECT_TRUE(ack.value) << "ACK should indicate request was accepted";

    // Step 2: Completion must arrive via MODEM_EVT_ACTION_DONE
    uint32_t done_event = channels.wait(MODEM_EVT_ACTION_DONE, true, 5000);
    EXPECT_NE(done_event & MODEM_EVT_ACTION_DONE, 0U) << "No ACTION_DONE received";

    ModemActionCompleteMsg complete{};
    ASSERT_EQ(channels.recv_action_complete(complete, 0), MessageChannelError::ok);
    EXPECT_EQ(complete.type, RadioLteRequestType::scan_networks);

    worker.join();
}

// ---------------------------------------------------------------------------
// Blocking request: server_disconnect
// Verifies the two-step protocol for a connect/disconnect blocking op.
// ---------------------------------------------------------------------------
TEST_F(IRadioLteTest, ServerDisconnectReturnsImmediateAckThenActionDone) {
    auto radio = make_radio();
    RadioLteChannels channels;

    // server_disconnect(conn_id=1)
    ModemTxMsg req{RadioLteRequestType::server_disconnect, 1, 0};
    ASSERT_EQ(channels.send_request(req, 1000), MessageChannelError::ok);

    std::thread worker([&]() { process_radio_requests(channels, radio); });

    // Step 1: immediate ACK
    uint32_t ack_event = channels.wait(MODEM_EVT_RESPONSE, true, MODEM_ACK_TIMEOUT_MS);
    EXPECT_NE(ack_event & MODEM_EVT_RESPONSE, 0U);

    ModemTypedResponseMsg<bool> ack{};
    ASSERT_EQ(channels.recv_typed_response(ack, 0), MessageChannelError::ok);
    EXPECT_TRUE(ack.ok);
    EXPECT_TRUE(ack.value);

    // Step 2: completion
    uint32_t done_event = channels.wait(MODEM_EVT_ACTION_DONE, true, 5000);
    EXPECT_NE(done_event & MODEM_EVT_ACTION_DONE, 0U);

    ModemActionCompleteMsg complete{};
    ASSERT_EQ(channels.recv_action_complete(complete, 0), MessageChannelError::ok);
    EXPECT_EQ(complete.type, RadioLteRequestType::server_disconnect);

    worker.join();
}
