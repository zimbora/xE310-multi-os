#include <gtest/gtest.h>
#include "modem/hal/message_queue_interface.h"
#include "modem/hal/message_queue_factory.h"

#include <thread>
#include <cstring>

using namespace modem;

namespace {
// Helper: compare QueueMessage content with expected bytes.
void expect_msg_eq(const QueueMessage& msg, uint8_t expected_cid, const uint8_t* expected, size_t expected_len) {
    EXPECT_EQ(msg.cid, expected_cid);
    EXPECT_EQ(msg.length, expected_len);
    EXPECT_EQ(std::memcmp(msg.data.data(), expected, expected_len), 0);
}
} // namespace

class MessageQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        queue = create_platform_message_queue(); // default capacity of 5 messages per direction
    }
    MessageQueueHandle queue;
};

// --- Basic TX push / pop ---

TEST_F(MessageQueueTest, TxPushPopSingleMessage) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    EXPECT_EQ(queue->tx_push(1, data, sizeof(data)), QueueError::ok);
    EXPECT_EQ(queue->tx_count(1), 1u);

    QueueMessage msg;
    EXPECT_EQ(queue->tx_pop(1, msg), QueueError::ok);
    expect_msg_eq(msg, 1, data, sizeof(data));
    EXPECT_EQ(queue->tx_count(1), 0u);
}

TEST_F(MessageQueueTest, TxPopEmptyReturnsEmpty) {
    QueueMessage msg;
    EXPECT_EQ(queue->tx_pop(1, msg), QueueError::empty);
}

TEST_F(MessageQueueTest, TxPushFullReturnsFull) {
    uint8_t data[] = {0xAA};
    for (uint8_t id = 1; id <= 5; ++id) {
        EXPECT_EQ(queue->tx_push(id, data, sizeof(data)), QueueError::ok);
    }
    EXPECT_EQ(queue->tx_push(1, data, sizeof(data)), QueueError::full);
}

// --- Basic RX push / pop ---

TEST_F(MessageQueueTest, RxPushPopSingleMessage) {
    uint8_t data[] = {0x10, 0x20};
    EXPECT_EQ(queue->rx_push(2, data, sizeof(data)), QueueError::ok);
    EXPECT_EQ(queue->rx_count(2), 1u);

    QueueMessage msg;
    EXPECT_EQ(queue->rx_pop(2, msg), QueueError::ok);
    expect_msg_eq(msg, 2, data, sizeof(data));
}

TEST_F(MessageQueueTest, RxPopEmptyReturnsEmpty) {
    QueueMessage msg;
    EXPECT_EQ(queue->rx_pop(3, msg), QueueError::empty);
}

// --- Invalid connection ID ---

TEST_F(MessageQueueTest, InvalidConnIdZero) {
    uint8_t data[] = {0x01};
    EXPECT_EQ(queue->tx_push(0, data, sizeof(data)), QueueError::invalid_id);
    EXPECT_EQ(queue->rx_push(0, data, sizeof(data)), QueueError::invalid_id);

    QueueMessage msg;
    EXPECT_EQ(queue->tx_pop(0, msg), QueueError::invalid_id);
    EXPECT_EQ(queue->rx_pop(0, msg), QueueError::invalid_id);
}

TEST_F(MessageQueueTest, InvalidConnIdTooHigh) {
    uint8_t data[] = {0x01};
    EXPECT_EQ(queue->tx_push(6, data, sizeof(data)), QueueError::invalid_id);
    EXPECT_EQ(queue->rx_push(6, data, sizeof(data)), QueueError::invalid_id);

    QueueMessage msg;
    EXPECT_EQ(queue->tx_pop(6, msg), QueueError::invalid_id);
    EXPECT_EQ(queue->rx_pop(6, msg), QueueError::invalid_id);
}

// --- FIFO ordering ---

TEST_F(MessageQueueTest, FifoOrdering) {
    uint8_t a[] = {0x01};
    uint8_t b[] = {0x02};
    uint8_t c[] = {0x03};

    queue->tx_push(1, a, sizeof(a));
    queue->tx_push(1, b, sizeof(b));
    queue->tx_push(1, c, sizeof(c));

    QueueMessage msg;
    queue->tx_pop(1, msg);
    expect_msg_eq(msg, 1, a, sizeof(a));
    queue->tx_pop(1, msg);
    expect_msg_eq(msg, 1, b, sizeof(b));
    queue->tx_pop(1, msg);
    expect_msg_eq(msg, 1, c, sizeof(c));
}

// --- Independence between connection IDs ---

TEST_F(MessageQueueTest, QueuesAreIndependentPerConnId) {
    uint8_t d1[] = {0xAA};
    uint8_t d2[] = {0xBB};

    queue->tx_push(1, d1, sizeof(d1));
    queue->tx_push(2, d2, sizeof(d2));

    EXPECT_EQ(queue->tx_count(1), 1u);
    EXPECT_EQ(queue->tx_count(2), 1u);

    QueueMessage msg;
    queue->tx_pop(1, msg);
    expect_msg_eq(msg, 1, d1, sizeof(d1));
    queue->tx_pop(2, msg);
    expect_msg_eq(msg, 2, d2, sizeof(d2));
}

// --- TX and RX are independent ---

TEST_F(MessageQueueTest, TxAndRxAreIndependent) {
    uint8_t tx_data[] = {0x01};
    uint8_t rx_data[] = {0x02};

    queue->tx_push(1, tx_data, sizeof(tx_data));
    queue->rx_push(1, rx_data, sizeof(rx_data));

    EXPECT_EQ(queue->tx_count(1), 1u);
    EXPECT_EQ(queue->rx_count(1), 1u);

    QueueMessage msg;
    queue->tx_pop(1, msg);
    expect_msg_eq(msg, 1, tx_data, sizeof(tx_data));
    queue->rx_pop(1, msg);
    expect_msg_eq(msg, 1, rx_data, sizeof(rx_data));
}

// --- All 5 connection IDs work ---

TEST_F(MessageQueueTest, AllFiveConnectionIds) {
    for (uint8_t id = 1; id <= 5; ++id) {
        EXPECT_EQ(queue->tx_push(id, &id, 1), QueueError::ok);
        EXPECT_EQ(queue->rx_push(id, &id, 1), QueueError::ok);
    }

    for (uint8_t id = 1; id <= 5; ++id) {
        QueueMessage msg;
        EXPECT_EQ(queue->tx_pop(id, msg), QueueError::ok);
        expect_msg_eq(msg, id, &id, 1);
        EXPECT_EQ(queue->rx_pop(id, msg), QueueError::ok);
        expect_msg_eq(msg, id, &id, 1);
    }
}

// --- Timeout on pop waits for push from another thread ---

TEST_F(MessageQueueTest, PopWithTimeoutWaitsForPush) {
    QueueMessage msg;

    // Pop with 500ms timeout — a producer thread pushes after 50ms
    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        uint8_t data[] = {0xDE, 0xAD};
        queue->rx_push(1, data, sizeof(data));
    });

    auto err = queue->rx_pop(1, msg, 500);
    producer.join();

    uint8_t expected[] = {0xDE, 0xAD};
    EXPECT_EQ(err, QueueError::ok);
    expect_msg_eq(msg, 1, expected, sizeof(expected));
}

// --- Timeout expires when no data arrives ---

TEST_F(MessageQueueTest, PopTimeoutExpires) {
    QueueMessage msg;
    auto err = queue->tx_pop(1, msg, 50);
    EXPECT_EQ(err, QueueError::timeout);
}

// --- Count returns 0 for invalid IDs ---

TEST_F(MessageQueueTest, CountReturnsZeroForInvalidId) {
    EXPECT_EQ(queue->tx_count(0), 0u);
    EXPECT_EQ(queue->tx_count(6), 0u);
    EXPECT_EQ(queue->rx_count(0), 0u);
    EXPECT_EQ(queue->rx_count(6), 0u);
}

// --- Large message ---

TEST_F(MessageQueueTest, LargeMessage) {
    uint8_t data[200];
    std::memset(data, 0x42, sizeof(data));
    EXPECT_EQ(queue->tx_push(1, data, sizeof(data)), QueueError::ok);

    QueueMessage msg;
    EXPECT_EQ(queue->tx_pop(1, msg), QueueError::ok);
    EXPECT_EQ(msg.length, 200u);
    expect_msg_eq(msg, 1, data, sizeof(data));
}

TEST_F(MessageQueueTest, TxPopNextPreservesGlobalFifoAcrossConnIds) {
    uint8_t first[] = {0x01};
    uint8_t second[] = {0x02};
    uint8_t third[] = {0x03};

    ASSERT_EQ(queue->tx_push(2, first, sizeof(first)), QueueError::ok);
    ASSERT_EQ(queue->tx_push(1, second, sizeof(second)), QueueError::ok);
    ASSERT_EQ(queue->tx_push(2, third, sizeof(third)), QueueError::ok);

    QueueMessage msg;
    ASSERT_EQ(queue->tx_pop_next(msg), QueueError::ok);
    expect_msg_eq(msg, 2, first, sizeof(first));
    ASSERT_EQ(queue->tx_pop_next(msg), QueueError::ok);
    expect_msg_eq(msg, 1, second, sizeof(second));
    ASSERT_EQ(queue->tx_pop_next(msg), QueueError::ok);
    expect_msg_eq(msg, 2, third, sizeof(third));
}

TEST_F(MessageQueueTest, TxPopRemovesOrderEntryForMatchingConnId) {
    uint8_t first[] = {0x0A};
    uint8_t second[] = {0x0B};
    uint8_t third[] = {0x0C};

    ASSERT_EQ(queue->tx_push(1, first, sizeof(first)), QueueError::ok);
    ASSERT_EQ(queue->tx_push(2, second, sizeof(second)), QueueError::ok);
    ASSERT_EQ(queue->tx_push(1, third, sizeof(third)), QueueError::ok);

    QueueMessage msg;
    ASSERT_EQ(queue->tx_pop(1, msg), QueueError::ok);
    expect_msg_eq(msg, 1, first, sizeof(first));

    ASSERT_EQ(queue->tx_pop_next(msg), QueueError::ok);
    expect_msg_eq(msg, 2, second, sizeof(second));
    ASSERT_EQ(queue->tx_pop_next(msg), QueueError::ok);
    expect_msg_eq(msg, 1, third, sizeof(third));
}
