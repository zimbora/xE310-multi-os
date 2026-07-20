#include "modem/hal/message_channel_factory.h"

#ifdef MODEM_PLATFORM_POSIX

#include <array>
#include <condition_variable>
#include <cstring>
#include <mutex>

namespace modem {

class PosixMessageChannel : public MessageChannelInterface {
public:
    static constexpr size_t MAX_CAPACITY = DEFAULT_CAPACITY;

    explicit PosixMessageChannel(size_t capacity)
        : capacity_(capacity == 0U || capacity > MAX_CAPACITY ? MAX_CAPACITY : capacity) {}

    MessageChannelError send(const uint8_t* data, size_t length, uint32_t timeout_ms) override {
        if (data == nullptr && length != 0U) return MessageChannelError::invalid_size;
        if (length > MESSAGE_CHANNEL_MAX_DATA) return MessageChannelError::invalid_size;

        std::unique_lock<std::mutex> lock(mutex_);
        if (count_ >= capacity_) {
            if (timeout_ms == 0U) return MessageChannelError::full;
            bool fReady = cvReceive_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                              [this]() { return count_ < capacity_; });
            if (!fReady) return MessageChannelError::timeout;
        }

        MessageFrame& slot = buffer_[tail_];
        slot.length = length;
        if (length != 0U) {
            std::memcpy(slot.data.data(), data, length);
        }
        tail_ = (tail_ + 1U) % MAX_CAPACITY;
        ++count_;
        cvSend_.notify_one();
        return MessageChannelError::ok;
    }

    MessageChannelError receive(MessageFrame& msg, uint32_t timeout_ms) override {
        std::unique_lock<std::mutex> lock(mutex_);
        if (count_ == 0U) {
            if (timeout_ms == 0U) return MessageChannelError::empty;
            bool fReady =
                cvSend_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() { return count_ != 0U; });
            if (!fReady) return MessageChannelError::timeout;
        }

        msg = buffer_[head_];
        head_ = (head_ + 1U) % MAX_CAPACITY;
        --count_;
        cvReceive_.notify_one();
        return MessageChannelError::ok;
    }

    size_t count() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

private:
    size_t capacity_ = MAX_CAPACITY;
    mutable std::mutex mutex_;
    std::condition_variable cvSend_;
    std::condition_variable cvReceive_;
    std::array<MessageFrame, MAX_CAPACITY> buffer_{};
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
};

MessageChannelHandle create_platform_message_channel(size_t capacity) {
    // dynamic-memory-allow: factory boundary returns owning HAL handle
    return MessageChannelHandle(new PosixMessageChannel(capacity), MessageChannelHandleDeleter{});
}

} // namespace modem

#endif