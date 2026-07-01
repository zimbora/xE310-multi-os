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
    static constexpr size_t max_msg_size = MAX_MSG_DATA; // max payload per message
    static constexpr size_t slot_size = max_msg_size + sizeof(uint16_t); // header + payload
    static constexpr size_t max_queue_capacity = default_capacity;

    explicit ZephyrMessageQueue(size_t capacity) : capacity_(capacity > max_queue_capacity ? max_queue_capacity : capacity) {
        for (uint8_t i = 0; i < max_connections; ++i) {
            k_msgq_init(&tx_queues_[i], reinterpret_cast<char*>(tx_bufs_[i].data()), slot_size, capacity_);
            k_msgq_init(&rx_queues_[i], reinterpret_cast<char*>(rx_bufs_[i].data()), slot_size, capacity_);
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
    QueueError push_to(std::array<struct k_msgq, MAX_CONNECTIONS>& queues,
                       uint8_t conn_id, const uint8_t* data, size_t length, uint32_t timeout_ms) {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return QueueError::invalid_id;
        if (length > MAX_MSG_SIZE) return QueueError::full;

        uint8_t slot[SLOT_SIZE];
        auto len16 = static_cast<uint16_t>(length);
        std::memcpy(slot, &len16, sizeof(len16));
        std::memcpy(slot + sizeof(len16), data, length);

        k_timeout_t tout = (timeout_ms == 0) ? K_NO_WAIT : K_MSEC(timeout_ms);
        int ret = k_msgq_put(&queues[conn_id - 1], slot, tout);
        if (ret == -ENOMSG || ret == -EAGAIN) return QueueError::full;
        if (ret == -EAGAIN) return QueueError::timeout;
        return QueueError::ok;
    }

    QueueError pop_from(std::array<struct k_msgq, MAX_CONNECTIONS>& queues,
                        uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms) {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return QueueError::invalid_id;

        uint8_t slot[SLOT_SIZE];
        k_timeout_t tout = (timeout_ms == 0) ? K_NO_WAIT : K_MSEC(timeout_ms);
        int ret = k_msgq_get(&queues[conn_id - 1], slot, tout);
        if (ret == -ENOMSG || ret == -EAGAIN) return QueueError::empty;

        uint16_t len16 = 0;
        std::memcpy(&len16, slot, sizeof(len16));
        msg.length = std::min(static_cast<size_t>(len16), MAX_MSG_DATA);
        std::memcpy(msg.data.data(), slot + sizeof(len16), msg.length);
        return QueueError::ok;
    }

    size_t capacity_;
    std::array<struct k_msgq, max_connections> tx_queues_{};
    std::array<struct k_msgq, max_connections> rx_queues_{};
    std::array<std::array<uint8_t, slot_size * max_queue_capacity>, max_connections> tx_bufs_{};
    std::array<std::array<uint8_t, slot_size * max_queue_capacity>, max_connections> rx_bufs_{};
};

} // namespace modem

#include "modem/message_queue_factory.h"

namespace modem {

std::unique_ptr<MessageQueueInterface> create_platform_message_queue(size_t capacity) {
    return std::make_unique<ZephyrMessageQueue>(capacity);
}

} // namespace modem

#endif
