#include <gtest/gtest.h>

#include "hal/message_channel_factory.h"

#include <cstring>
#include <thread>

using namespace modem;

namespace {

void expect_frame_eq(const MessageFrame& msg, const uint8_t* expected, size_t expected_len) {
    EXPECT_EQ(msg.length, expected_len);
    EXPECT_EQ(std::memcmp(msg.data.data(), expected, expected_len), 0);
}

} // namespace

class MessageChannelTest : public ::testing::Test {
protected:
    void SetUp() override { channel = create_platform_message_channel(4); }

    MessageChannelHandle channel;
};

TEST_F(MessageChannelTest, SendReceiveSingleMessage) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    EXPECT_EQ(channel->send(data, sizeof(data)), MessageChannelError::ok);
    EXPECT_EQ(channel->count(), 1u);

    MessageFrame msg;
    EXPECT_EQ(channel->receive(msg), MessageChannelError::ok);
    expect_frame_eq(msg, data, sizeof(data));
    EXPECT_EQ(channel->count(), 0u);
}

TEST_F(MessageChannelTest, ReceiveEmptyReturnsEmpty) {
    MessageFrame msg;
    EXPECT_EQ(channel->receive(msg), MessageChannelError::empty);
}

TEST_F(MessageChannelTest, SendFullReturnsFull) {
    uint8_t data[] = {0xAA};
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(channel->send(data, sizeof(data)), MessageChannelError::ok);
    }
    EXPECT_EQ(channel->send(data, sizeof(data)), MessageChannelError::full);
}

TEST_F(MessageChannelTest, PreservesFifoOrdering) {
    uint8_t a[] = {0x01};
    uint8_t b[] = {0x02};
    uint8_t c[] = {0x03};

    EXPECT_EQ(channel->send(a, sizeof(a)), MessageChannelError::ok);
    EXPECT_EQ(channel->send(b, sizeof(b)), MessageChannelError::ok);
    EXPECT_EQ(channel->send(c, sizeof(c)), MessageChannelError::ok);

    MessageFrame msg;
    EXPECT_EQ(channel->receive(msg), MessageChannelError::ok);
    expect_frame_eq(msg, a, sizeof(a));
    EXPECT_EQ(channel->receive(msg), MessageChannelError::ok);
    expect_frame_eq(msg, b, sizeof(b));
    EXPECT_EQ(channel->receive(msg), MessageChannelError::ok);
    expect_frame_eq(msg, c, sizeof(c));
}

TEST_F(MessageChannelTest, ReceiveWithTimeoutWaitsForProducer) {
    MessageFrame msg;

    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        uint8_t data[] = {0xDE, 0xAD};
        channel->send(data, sizeof(data));
    });

    auto err = channel->receive(msg, 500);
    producer.join();

    uint8_t expected[] = {0xDE, 0xAD};
    EXPECT_EQ(err, MessageChannelError::ok);
    expect_frame_eq(msg, expected, sizeof(expected));
}

TEST_F(MessageChannelTest, ReceiveTimeoutExpires) {
    MessageFrame msg;
    EXPECT_EQ(channel->receive(msg, 50), MessageChannelError::timeout);
}

TEST_F(MessageChannelTest, RejectsOversizedMessages) {
    uint8_t data[MESSAGE_CHANNEL_MAX_DATA + 1] = {};
    EXPECT_EQ(channel->send(data, sizeof(data)), MessageChannelError::invalid_size);
}
