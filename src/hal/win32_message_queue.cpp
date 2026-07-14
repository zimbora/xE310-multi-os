#include "modem/message_queue_interface.h"

#ifdef MODEM_PLATFORM_WINDOWS

#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace modem {

class Win32MessageQueue : public MessageQueueInterface {
public:
    explicit Win32MessageQueue(size_t capacity)
        : capacity_(capacity == 0U ? DEFAULT_CAPACITY : capacity) {}
    ~Win32MessageQueue() override = default;

    Win32MessageQueue(const Win32MessageQueue&) = delete;
    Win32MessageQueue& operator=(const Win32MessageQueue&) = delete;

    QueueError tx_push(uint8_t conn_id, const uint8_t* data, size_t length, uint32_t timeout_ms) override {
        return push_to(tx_, tx_order_, conn_id, data, length, timeout_ms);
    }

    QueueError tx_pop(uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms) override {
        return pop_from(tx_, tx_order_, conn_id, msg, timeout_ms);
    }

    QueueError tx_pop_next(QueueMessage& msg, uint32_t timeout_ms) override {
        return pop_next_from(tx_, tx_order_, msg, timeout_ms);
    }

    size_t tx_count(uint8_t conn_id) const override {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return 0;
        std::lock_guard<std::mutex> lk(mtx_);
        return tx_[conn_id - 1].size();
    }

    QueueError rx_push(uint8_t conn_id, const uint8_t* data, size_t length, uint32_t timeout_ms) override {
        return push_to(rx_, rx_order_, conn_id, data, length, timeout_ms);
    }

    QueueError rx_pop(uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms) override {
        return pop_from(rx_, rx_order_, conn_id, msg, timeout_ms);
    }

    size_t rx_count(uint8_t conn_id) const override {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return 0;
        std::lock_guard<std::mutex> lk(mtx_);
        return rx_[conn_id - 1].size();
    }

private:
    QueueError push_to(std::array<std::deque<QueueMessage>, MAX_CONNECTIONS>& queues, std::deque<uint8_t>& order,
                       uint8_t conn_id, const uint8_t* data, size_t length, uint32_t /*timeout_ms*/) {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return QueueError::invalid_id;
        std::unique_lock<std::mutex> lk(mtx_);

        auto& q = queues[conn_id - 1];
        if (q.size() >= capacity_ || order.size() >= capacity_) {
            return QueueError::full;
        }

        q.push_back(QueueMessage{});
        auto& msg = q.back();
        msg.cid = conn_id;
        msg.length = std::min(length, MAX_MSG_DATA);
        std::memcpy(msg.data.data(), data, msg.length);
        order.push_back(conn_id);
        cv_push_.notify_one();
        return QueueError::ok;
    }

    QueueError pop_from(std::array<std::deque<QueueMessage>, MAX_CONNECTIONS>& queues, std::deque<uint8_t>& order,
                        uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms) {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return QueueError::invalid_id;
        std::unique_lock<std::mutex> lk(mtx_);

        auto& q = queues[conn_id - 1];
        if (q.empty()) {
            if (timeout_ms == 0) return QueueError::empty;
            auto ok = cv_push_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return !q.empty(); });
            if (!ok) return QueueError::timeout;
        }

        msg = std::move(q.front());
        q.pop_front();
        remove_order_entry(order, conn_id);
        cv_pop_.notify_one();
        return QueueError::ok;
    }

    QueueError pop_next_from(std::array<std::deque<QueueMessage>, MAX_CONNECTIONS>& queues, std::deque<uint8_t>& order,
                             QueueMessage& msg, uint32_t timeout_ms) {
        std::unique_lock<std::mutex> lk(mtx_);
        if (order.empty()) {
            if (timeout_ms == 0) return QueueError::empty;
            auto ok = cv_push_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return !order.empty(); });
            if (!ok) return QueueError::timeout;
        }

        const uint8_t conn_id = order.front();
        auto& q = queues[conn_id - 1];
        if (q.empty()) return QueueError::empty;

        msg = std::move(q.front());
        q.pop_front();
        order.pop_front();
        cv_pop_.notify_one();
        return QueueError::ok;
    }

    static void remove_order_entry(std::deque<uint8_t>& order, uint8_t conn_id) {
        for (auto it = order.begin(); it != order.end(); ++it) {
            if (*it == conn_id) {
                order.erase(it);
                break;
            }
        }
    }

    size_t capacity_;
    mutable std::mutex mtx_;
    std::condition_variable cv_push_; // signalled after a push (for pop waiters)
    std::condition_variable cv_pop_;  // signalled after a pop  (for push waiters)
    std::array<std::deque<QueueMessage>, MAX_CONNECTIONS> tx_{};
    std::array<std::deque<QueueMessage>, MAX_CONNECTIONS> rx_{};
    std::deque<uint8_t> tx_order_{};
    std::deque<uint8_t> rx_order_{};
};

} // namespace modem

#include "modem/message_queue_factory.h"

namespace modem {

MessageQueueHandle create_platform_message_queue(size_t capacity) {
    return MessageQueueHandle(new Win32MessageQueue(capacity), MessageQueueHandleDeleter{});
}

} // namespace modem

#endif
