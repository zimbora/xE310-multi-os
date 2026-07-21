#include "hal/message_queue_interface.h"

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
        k_mutex_init(&mutex_);
        for (uint8_t i = 0; i < MAX_CONNECTIONS; ++i) {
            k_msgq_init(&tx_queues_[i], tx_bufs_[i].data(), SLOT_SIZE, queue_capacity_);
            k_msgq_init(&rx_queues_[i], rx_bufs_[i].data(), SLOT_SIZE, queue_capacity_);
        }
        k_msgq_init(&tx_order_queue_, tx_order_buf_.data(), sizeof(uint8_t), queue_capacity_);
        k_msgq_init(&rx_order_queue_, rx_order_buf_.data(), sizeof(uint8_t), queue_capacity_);
    }

    ~ZephyrMessageQueue() override = default;

    QueueError tx_push(uint8_t conn_id, const uint8_t* data, size_t length, uint32_t timeout_ms) override {
        return push_to(tx_queues_, tx_order_queue_, conn_id, data, length, timeout_ms);
    }

    QueueError tx_pop(uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms) override {
        return pop_from(tx_queues_, tx_order_queue_, conn_id, msg, timeout_ms);
    }

    QueueError tx_pop_next(QueueMessage& msg, uint32_t timeout_ms) override {
        return pop_next_from(tx_queues_, tx_order_queue_, msg, timeout_ms);
    }

    size_t tx_count(uint8_t conn_id) const override {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return 0;
        return k_msgq_num_used_get(const_cast<struct k_msgq*>(&tx_queues_[conn_id - 1]));
    }

    QueueError rx_push(uint8_t conn_id, const uint8_t* data, size_t length, uint32_t timeout_ms) override {
        return push_to(rx_queues_, rx_order_queue_, conn_id, data, length, timeout_ms);
    }

    QueueError rx_pop(uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms) override {
        return pop_from(rx_queues_, rx_order_queue_, conn_id, msg, timeout_ms);
    }

    size_t rx_count(uint8_t conn_id) const override {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return 0;
        return k_msgq_num_used_get(const_cast<struct k_msgq*>(&rx_queues_[conn_id - 1]));
    }

private:
    QueueError push_to(std::array<struct k_msgq, MAX_CONNECTIONS>& queues, struct k_msgq& order_queue, uint8_t conn_id,
                       const uint8_t* data, size_t length, uint32_t /*timeout_ms*/) {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return QueueError::invalid_id;
        if (length > MAX_MSG_SIZE) return QueueError::full;
        k_mutex_lock(&mutex_, K_FOREVER);
        if (k_msgq_num_used_get(&queues[conn_id - 1]) >= queue_capacity_ ||
            k_msgq_num_used_get(&order_queue) >= queue_capacity_) {
            k_mutex_unlock(&mutex_);
            return QueueError::full;
        }

        uint8_t slot[SLOT_SIZE];
        auto len16 = static_cast<uint16_t>(length);
        std::memcpy(slot, &len16, sizeof(len16));
        std::memcpy(slot + sizeof(len16), data, length);

        int payload_ret = k_msgq_put(&queues[conn_id - 1], slot, K_NO_WAIT);
        int order_ret = (payload_ret == 0) ? k_msgq_put(&order_queue, &conn_id, K_NO_WAIT) : payload_ret;
        k_mutex_unlock(&mutex_);
        return (payload_ret == 0 && order_ret == 0) ? QueueError::ok : QueueError::full;
    }

    QueueError pop_from(std::array<struct k_msgq, MAX_CONNECTIONS>& queues, struct k_msgq& order_queue, uint8_t conn_id,
                        QueueMessage& msg, uint32_t timeout_ms) {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return QueueError::invalid_id;
        const int64_t deadline = k_uptime_get() + static_cast<int64_t>(timeout_ms);
        while (true) {
            k_mutex_lock(&mutex_, K_FOREVER);
            uint8_t slot[SLOT_SIZE];
            int ret = k_msgq_get(&queues[conn_id - 1], slot, K_NO_WAIT);
            if (ret == 0) {
                remove_order_entry(order_queue, conn_id);
                k_mutex_unlock(&mutex_);
                decode_slot(conn_id, slot, msg);
                return QueueError::ok;
            }
            k_mutex_unlock(&mutex_);
            if (timeout_ms == 0U) return QueueError::empty;
            if (k_uptime_get() >= deadline) return QueueError::timeout;
            k_sleep(K_MSEC(1));
        }
    }

    QueueError pop_next_from(std::array<struct k_msgq, MAX_CONNECTIONS>& queues, struct k_msgq& order_queue,
                             QueueMessage& msg, uint32_t timeout_ms) {
        const int64_t deadline = k_uptime_get() + static_cast<int64_t>(timeout_ms);
        while (true) {
            k_mutex_lock(&mutex_, K_FOREVER);
            uint8_t conn_id = 0;
            int order_ret = k_msgq_get(&order_queue, &conn_id, K_NO_WAIT);
            if (order_ret == 0) {
                uint8_t slot[SLOT_SIZE];
                int payload_ret = k_msgq_get(&queues[conn_id - 1], slot, K_NO_WAIT);
                k_mutex_unlock(&mutex_);
                if (payload_ret != 0) return QueueError::empty;
                decode_slot(conn_id, slot, msg);
                return QueueError::ok;
            }
            k_mutex_unlock(&mutex_);
            if (timeout_ms == 0U) return QueueError::empty;
            if (k_uptime_get() >= deadline) return QueueError::timeout;
            k_sleep(K_MSEC(1));
        }
    }

    static void decode_slot(uint8_t conn_id, const uint8_t* slot, QueueMessage& msg) {
        uint16_t len16 = 0;
        std::memcpy(&len16, slot, sizeof(len16));
        msg.cid = conn_id;
        msg.length = std::min(static_cast<size_t>(len16), MAX_MSG_DATA);
        std::memcpy(msg.data.data(), slot + sizeof(len16), msg.length);
    }

    static void remove_order_entry(struct k_msgq& order_queue, uint8_t conn_id) {
        std::array<uint8_t, MAX_QUEUE_CAPACITY> deferred{};
        size_t deferred_count = 0;
        bool removed = false;
        uint8_t queued_conn_id = 0;
        while (k_msgq_get(&order_queue, &queued_conn_id, K_NO_WAIT) == 0) {
            if (!removed && queued_conn_id == conn_id) {
                removed = true;
                continue;
            }
            deferred[deferred_count++] = queued_conn_id;
        }

        for (size_t i = 0; i < deferred_count; ++i) {
            k_msgq_put(&order_queue, &deferred[i], K_NO_WAIT);
        }
    }

    std::array<struct k_msgq, MAX_CONNECTIONS> tx_queues_{};
    std::array<struct k_msgq, MAX_CONNECTIONS> rx_queues_{};
    std::array<std::array<char, SLOT_SIZE * MAX_QUEUE_CAPACITY>, MAX_CONNECTIONS> tx_bufs_{};
    std::array<std::array<char, SLOT_SIZE * MAX_QUEUE_CAPACITY>, MAX_CONNECTIONS> rx_bufs_{};
    struct k_msgq tx_order_queue_ {};
    struct k_msgq rx_order_queue_ {};
    std::array<char, MAX_QUEUE_CAPACITY * sizeof(uint8_t)> tx_order_buf_{};
    std::array<char, MAX_QUEUE_CAPACITY * sizeof(uint8_t)> rx_order_buf_{};
    struct k_mutex mutex_ {};
    size_t queue_capacity_ = MAX_QUEUE_CAPACITY;
};

} // namespace modem

#include "hal/message_queue_factory.h"

namespace modem {

MessageQueueHandle create_platform_message_queue(size_t capacity) {
    static ZephyrMessageQueue queue_instance(capacity);
    return MessageQueueHandle(&queue_instance, MessageQueueHandleDeleter{false});
}

} // namespace modem

#endif
