#include "modem/modem_controller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace modem;

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

TEST(ModemControllerTest, ConnectSuccess) {
    auto mock = std::make_unique<MockUart>();
    auto* raw = mock.get();

    EXPECT_CALL(*raw, open(testing::_, testing::_))
        .WillOnce(testing::Return(UartError::ok));

    ModemController ctrl(std::move(mock));
    EXPECT_EQ(ctrl.connect("COM1"), ModemStatus::ok);
}

TEST(ModemControllerTest, ConnectFails) {
    auto mock = std::make_unique<MockUart>();
    auto* raw = mock.get();

    EXPECT_CALL(*raw, open(testing::_, testing::_))
        .WillOnce(testing::Return(UartError::port_not_open));

    ModemController ctrl(std::move(mock));
    EXPECT_EQ(ctrl.connect("COM1"), ModemStatus::uart_error);
}

TEST(ModemControllerTest, SendCommandWhenNotConnected) {
    auto mock = std::make_unique<MockUart>();
    auto* raw = mock.get();

    EXPECT_CALL(*raw, is_open()).WillOnce(testing::Return(false));

    ModemController ctrl(std::move(mock));
    AtCommand cmd("AT");
    AtResponse resp;
    EXPECT_EQ(ctrl.send_command(cmd, resp), ModemStatus::not_connected);
}
