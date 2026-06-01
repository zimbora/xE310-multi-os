#include <gtest/gtest.h>
#include "modem/message_queue_interface.h"
#include "modem/message_queue_factory.h"

#include <thread>
#include <vector>
#include <string>

using namespace modem;

class MessageQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        queue = create_platform_message_queue(4); // capacity of 4 messages per queue
    }
    std::unique_ptr<MessageQueueInterface> queue;
};

// --- Basic TX push / pop ---

TEST_F(MessageQueueTest, TxPushPopSingleMessage) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    EXPECT_EQ(queue->tx_push(1, data.data(), data.size()), QueueError::ok);
    EXPECT_EQ(queue->tx_count(1), 1u);

    QueueMessage msg;
    EXPECT_EQ(queue->tx_pop(1, msg), QueueError::ok);
    EXPECT_EQ(msg.data, data);
    EXPECT_EQ(queue->tx_count(1), 0u);
}

TEST_F(MessageQueueTest, TxPopEmptyReturnsEmpty) {
    QueueMessage msg;
    EXPECT_EQ(queue->tx_pop(1, msg), QueueError::empty);
}

TEST_F(MessageQueueTest, TxPushFullReturnsFull) {
    std::vector<uint8_t> data = {0xAA};
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(queue->tx_push(1, data.data(), data.size()), QueueError::ok);
    }
    EXPECT_EQ(queue->tx_push(1, data.data(), data.size()), QueueError::full);
}

// --- Basic RX push / pop ---

TEST_F(MessageQueueTest, RxPushPopSingleMessage) {
    std::vector<uint8_t> data = {0x10, 0x20};
    EXPECT_EQ(queue->rx_push(2, data.data(), data.size()), QueueError::ok);
    EXPECT_EQ(queue->rx_count(2), 1u);

    QueueMessage msg;
    EXPECT_EQ(queue->rx_pop(2, msg), QueueError::ok);
    EXPECT_EQ(msg.data, data);
}

TEST_F(MessageQueueTest, RxPopEmptyReturnsEmpty) {
    QueueMessage msg;
    EXPECT_EQ(queue->rx_pop(3, msg), QueueError::empty);
}

// --- Invalid connection ID ---

TEST_F(MessageQueueTest, InvalidConnIdZero) {
    std::vector<uint8_t> data = {0x01};
    EXPECT_EQ(queue->tx_push(0, data.data(), data.size()), QueueError::invalid_id);
    EXPECT_EQ(queue->rx_push(0, data.data(), data.size()), QueueError::invalid_id);

    QueueMessage msg;
    EXPECT_EQ(queue->tx_pop(0, msg), QueueError::invalid_id);
    EXPECT_EQ(queue->rx_pop(0, msg), QueueError::invalid_id);
}

TEST_F(MessageQueueTest, InvalidConnIdTooHigh) {
    std::vector<uint8_t> data = {0x01};
    EXPECT_EQ(queue->tx_push(6, data.data(), data.size()), QueueError::invalid_id);
    EXPECT_EQ(queue->rx_push(6, data.data(), data.size()), QueueError::invalid_id);

    QueueMessage msg;
    EXPECT_EQ(queue->tx_pop(6, msg), QueueError::invalid_id);
    EXPECT_EQ(queue->rx_pop(6, msg), QueueError::invalid_id);
}

// --- FIFO ordering ---

TEST_F(MessageQueueTest, FifoOrdering) {
    std::vector<uint8_t> a = {0x01};
    std::vector<uint8_t> b = {0x02};
    std::vector<uint8_t> c = {0x03};

    queue->tx_push(1, a.data(), a.size());
    queue->tx_push(1, b.data(), b.size());
    queue->tx_push(1, c.data(), c.size());

    QueueMessage msg;
    queue->tx_pop(1, msg);
    EXPECT_EQ(msg.data, a);
    queue->tx_pop(1, msg);
    EXPECT_EQ(msg.data, b);
    queue->tx_pop(1, msg);
    EXPECT_EQ(msg.data, c);
}

// --- Independence between connection IDs ---

TEST_F(MessageQueueTest, QueuesAreIndependentPerConnId) {
    std::vector<uint8_t> d1 = {0xAA};
    std::vector<uint8_t> d2 = {0xBB};

    queue->tx_push(1, d1.data(), d1.size());
    queue->tx_push(2, d2.data(), d2.size());

    EXPECT_EQ(queue->tx_count(1), 1u);
    EXPECT_EQ(queue->tx_count(2), 1u);

    QueueMessage msg;
    queue->tx_pop(1, msg);
    EXPECT_EQ(msg.data, d1);
    queue->tx_pop(2, msg);
    EXPECT_EQ(msg.data, d2);
}

// --- TX and RX are independent ---

TEST_F(MessageQueueTest, TxAndRxAreIndependent) {
    std::vector<uint8_t> tx_data = {0x01};
    std::vector<uint8_t> rx_data = {0x02};

    queue->tx_push(1, tx_data.data(), tx_data.size());
    queue->rx_push(1, rx_data.data(), rx_data.size());

    EXPECT_EQ(queue->tx_count(1), 1u);
    EXPECT_EQ(queue->rx_count(1), 1u);

    QueueMessage msg;
    queue->tx_pop(1, msg);
    EXPECT_EQ(msg.data, tx_data);
    queue->rx_pop(1, msg);
    EXPECT_EQ(msg.data, rx_data);
}

// --- All 5 connection IDs work ---

TEST_F(MessageQueueTest, AllFiveConnectionIds) {
    for (uint8_t id = 1; id <= 5; ++id) {
        std::vector<uint8_t> data = {id};
        EXPECT_EQ(queue->tx_push(id, data.data(), data.size()), QueueError::ok);
        EXPECT_EQ(queue->rx_push(id, data.data(), data.size()), QueueError::ok);
    }

    for (uint8_t id = 1; id <= 5; ++id) {
        QueueMessage msg;
        EXPECT_EQ(queue->tx_pop(id, msg), QueueError::ok);
        EXPECT_EQ(msg.data, std::vector<uint8_t>{id});
        EXPECT_EQ(queue->rx_pop(id, msg), QueueError::ok);
        EXPECT_EQ(msg.data, std::vector<uint8_t>{id});
    }
}

// --- Timeout on pop waits for push from another thread ---

TEST_F(MessageQueueTest, PopWithTimeoutWaitsForPush) {
    QueueMessage msg;

    // Pop with 500ms timeout — a producer thread pushes after 50ms
    std::thread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::vector<uint8_t> data = {0xDE, 0xAD};
        queue->rx_push(1, data.data(), data.size());
    });

    auto err = queue->rx_pop(1, msg, 500);
    producer.join();

    EXPECT_EQ(err, QueueError::ok);
    EXPECT_EQ(msg.data, (std::vector<uint8_t>{0xDE, 0xAD}));
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
    std::vector<uint8_t> data(200, 0x42);
    EXPECT_EQ(queue->tx_push(1, data.data(), data.size()), QueueError::ok);

    QueueMessage msg;
    EXPECT_EQ(queue->tx_pop(1, msg), QueueError::ok);
    EXPECT_EQ(msg.data.size(), 200u);
    EXPECT_EQ(msg.data, data);
}
