#include "modem/message_queue_interface.h"

#if defined(PLATFORM_ZEPHYR) || defined(__ZEPHYR__)

#include <zephyr/kernel.h>
#include <cstring>
#include <array>

namespace modem {

/// Zephyr implementation using k_msgq for each TX/RX queue.
/// Messages are copied into a fixed-size ring buffer managed by k_msgq.
/// Each slot stores a raw byte buffer; the message length is prepended as a uint16_t header.
class ZephyrMessageQueue : public MessageQueueInterface {
public:
    static constexpr size_t MAX_MSG_SIZE = 256;                          // max payload per message
    static constexpr size_t SLOT_SIZE = MAX_MSG_SIZE + sizeof(uint16_t); // header + payload
    static constexpr size_t MAX_QUEUE_CAPACITY = DEFAULT_CAPACITY;

    explicit ZephyrMessageQueue(size_t capacity) {
        queue_capacity_ = (capacity == 0U || capacity > MAX_QUEUE_CAPACITY) ? MAX_QUEUE_CAPACITY : capacity;
        for (uint8_t i = 0; i < MAX_CONNECTIONS; ++i) {
            k_msgq_init(&tx_queues_[i], tx_bufs_[i].data(), SLOT_SIZE, queue_capacity_);
            k_msgq_init(&rx_queues_[i], rx_bufs_[i].data(), SLOT_SIZE, queue_capacity_);
        }
    }

    ~ZephyrMessageQueue() override = default;

    QueueError tx_push(uint8_t conn_id, const uint8_t* data, size_t length, uint32_t timeout_ms) override {
        return push_to(tx_queues_, conn_id, data, length, timeout_ms);
    }

    QueueError tx_pop(uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms) override {
        return pop_from(tx_queues_, conn_id, msg, timeout_ms);
    }

    size_t tx_count(uint8_t conn_id) const override {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return 0;
        return k_msgq_num_used_get(const_cast<struct k_msgq*>(&tx_queues_[conn_id - 1]));
    }

    QueueError rx_push(uint8_t conn_id, const uint8_t* data, size_t length, uint32_t timeout_ms) override {
        return push_to(rx_queues_, conn_id, data, length, timeout_ms);
    }

    QueueError rx_pop(uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms) override {
        return pop_from(rx_queues_, conn_id, msg, timeout_ms);
    }

    size_t rx_count(uint8_t conn_id) const override {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return 0;
        return k_msgq_num_used_get(const_cast<struct k_msgq*>(&rx_queues_[conn_id - 1]));
    }

private:
    static k_timeout_t to_timeout(uint32_t timeout_ms) { return (timeout_ms == 0U) ? K_NO_WAIT : K_MSEC(timeout_ms); }

    static QueueError map_put_result(int ret) {
        if (ret == 0) return QueueError::ok;
        if (ret == -EAGAIN) return QueueError::timeout;
        if (ret == -ENOMSG) return QueueError::full;
        return QueueError::full;
    }

    static QueueError map_get_result(int ret) {
        if (ret == 0) return QueueError::ok;
        if (ret == -EAGAIN) return QueueError::timeout;
        if (ret == -ENOMSG) return QueueError::empty;
        return QueueError::empty;
    }

    QueueError push_to(std::array<struct k_msgq, MAX_CONNECTIONS>& queues, uint8_t conn_id, const uint8_t* data,
                       size_t length, uint32_t timeout_ms) {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return QueueError::invalid_id;
        if (length > MAX_MSG_SIZE) return QueueError::full;

        uint8_t slot[SLOT_SIZE];
        auto len16 = static_cast<uint16_t>(length);
        std::memcpy(slot, &len16, sizeof(len16));
        std::memcpy(slot + sizeof(len16), data, length);

        int ret = k_msgq_put(&queues[conn_id - 1], slot, to_timeout(timeout_ms));
        return map_put_result(ret);
    }

    QueueError pop_from(std::array<struct k_msgq, MAX_CONNECTIONS>& queues, uint8_t conn_id, QueueMessage& msg,
                        uint32_t timeout_ms) {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return QueueError::invalid_id;

        uint8_t slot[SLOT_SIZE];
        int ret = k_msgq_get(&queues[conn_id - 1], slot, to_timeout(timeout_ms));
        QueueError qerr = map_get_result(ret);
        if (qerr != QueueError::ok) return qerr;

        uint16_t len16 = 0;
        std::memcpy(&len16, slot, sizeof(len16));
        msg.length = std::min(static_cast<size_t>(len16), MAX_MSG_DATA);
        std::memcpy(msg.data.data(), slot + sizeof(len16), msg.length);
        return QueueError::ok;
    }

    std::array<struct k_msgq, MAX_CONNECTIONS> tx_queues_{};
    std::array<struct k_msgq, MAX_CONNECTIONS> rx_queues_{};
    std::array<std::array<char, SLOT_SIZE * MAX_QUEUE_CAPACITY>, MAX_CONNECTIONS> tx_bufs_{};
    std::array<std::array<char, SLOT_SIZE * MAX_QUEUE_CAPACITY>, MAX_CONNECTIONS> rx_bufs_{};
    size_t queue_capacity_ = MAX_QUEUE_CAPACITY;
};

} // namespace modem

#include "modem/message_queue_factory.h"

namespace modem {

std::unique_ptr<MessageQueueInterface> create_platform_message_queue(size_t capacity) {
    return std::make_unique<ZephyrMessageQueue>(capacity);
}

} // namespace modem

#endif
