#include "modem/message_queue_interface.h"

#ifdef MODEM_PLATFORM_POSIX

#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace modem {

class PosixMessageQueue : public MessageQueueInterface {
public:
    explicit PosixMessageQueue(size_t capacity)
        : capacity_(capacity) {}
    ~PosixMessageQueue() override = default;

    PosixMessageQueue(const PosixMessageQueue&) = delete;
    PosixMessageQueue& operator=(const PosixMessageQueue&) = delete;

    QueueError tx_push(uint8_t conn_id, const uint8_t* data, size_t length, uint32_t timeout_ms) override {
        return push_to(tx_, conn_id, data, length, timeout_ms);
    }

    QueueError tx_pop(uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms) override {
        return pop_from(tx_, conn_id, msg, timeout_ms);
    }

    size_t tx_count(uint8_t conn_id) const override {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return 0;
        std::lock_guard<std::mutex> lk(mtx_);
        return tx_[conn_id - 1].size();
    }

    QueueError rx_push(uint8_t conn_id, const uint8_t* data, size_t length, uint32_t timeout_ms) override {
        return push_to(rx_, conn_id, data, length, timeout_ms);
    }

    QueueError rx_pop(uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms) override {
        return pop_from(rx_, conn_id, msg, timeout_ms);
    }

    size_t rx_count(uint8_t conn_id) const override {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return 0;
        std::lock_guard<std::mutex> lk(mtx_);
        return rx_[conn_id - 1].size();
    }

private:
    QueueError push_to(std::array<std::deque<QueueMessage>, MAX_CONNECTIONS>& queues, uint8_t conn_id,
                       const uint8_t* data, size_t length, uint32_t timeout_ms) {
        if (conn_id < 1 || conn_id > MAX_CONNECTIONS) return QueueError::invalid_id;
        std::unique_lock<std::mutex> lk(mtx_);

        auto& q = queues[conn_id - 1];
        if (q.size() >= capacity_) {
            if (timeout_ms == 0) return QueueError::full;
            auto ok = cv_pop_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return q.size() < capacity_; });
            if (!ok) return QueueError::timeout;
        }

        q.push_back(QueueMessage{});
        auto& msg = q.back();
        msg.length = std::min(length, MAX_MSG_DATA);
        std::memcpy(msg.data.data(), data, msg.length);
        cv_push_.notify_one();
        return QueueError::ok;
    }

    QueueError pop_from(std::array<std::deque<QueueMessage>, MAX_CONNECTIONS>& queues, uint8_t conn_id,
                        QueueMessage& msg, uint32_t timeout_ms) {
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
        cv_pop_.notify_one();
        return QueueError::ok;
    }

    size_t capacity_;
    mutable std::mutex mtx_;
    std::condition_variable cv_push_;
    std::condition_variable cv_pop_;
    std::array<std::deque<QueueMessage>, MAX_CONNECTIONS> tx_{};
    std::array<std::deque<QueueMessage>, MAX_CONNECTIONS> rx_{};
};

} // namespace modem

#include "modem/message_queue_factory.h"

namespace modem {

MessageQueueHandle create_platform_message_queue(size_t capacity) {
    return MessageQueueHandle(new PosixMessageQueue(capacity), MessageQueueHandleDeleter{});
}

} // namespace modem

#endif
