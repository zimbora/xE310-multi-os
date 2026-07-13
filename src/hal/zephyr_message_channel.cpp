#include "modem/message_channel_factory.h"

#if defined(PLATFORM_ZEPHYR) || defined(__ZEPHYR__)

#include <array>
#include <cstring>

#include <zephyr/kernel.h>

namespace modem {

class ZephyrMessageChannel : public MessageChannelInterface {
public:
    static constexpr size_t MAX_CAPACITY = DEFAULT_CAPACITY;
    static constexpr size_t SLOT_SIZE = MESSAGE_CHANNEL_MAX_DATA + sizeof(uint16_t);

    explicit ZephyrMessageChannel(size_t capacity)
        : capacity_(capacity == 0U || capacity > MAX_CAPACITY ? MAX_CAPACITY : capacity) {
        k_msgq_init(&queue_, buffer_.data(), SLOT_SIZE, capacity_);
    }

    MessageChannelError send(const uint8_t* data, size_t length, uint32_t timeout_ms) override {
        if (data == nullptr && length != 0U) return MessageChannelError::invalid_size;
        if (length > MESSAGE_CHANNEL_MAX_DATA) return MessageChannelError::invalid_size;

        uint8_t slot[SLOT_SIZE] = {};
        const uint16_t size = static_cast<uint16_t>(length);
        std::memcpy(slot, &size, sizeof(size));
        if (length != 0U) {
            std::memcpy(slot + sizeof(size), data, length);
        }

        int ret = k_msgq_put(&queue_, slot, to_timeout(timeout_ms));
        return map_put_result(ret);
    }

    MessageChannelError receive(MessageFrame& msg, uint32_t timeout_ms) override {
        uint8_t slot[SLOT_SIZE] = {};
        int ret = k_msgq_get(&queue_, slot, to_timeout(timeout_ms));
        MessageChannelError err = map_get_result(ret);
        if (err != MessageChannelError::ok) return err;

        uint16_t size = 0;
        std::memcpy(&size, slot, sizeof(size));
        msg.length = size;
        if (msg.length > MESSAGE_CHANNEL_MAX_DATA) return MessageChannelError::invalid_size;
        if (msg.length != 0U) {
            std::memcpy(msg.data.data(), slot + sizeof(size), msg.length);
        }
        return MessageChannelError::ok;
    }

    size_t count() const override { return k_msgq_num_used_get(const_cast<struct k_msgq*>(&queue_)); }

private:
    static k_timeout_t to_timeout(uint32_t timeout_ms) { return timeout_ms == 0U ? K_NO_WAIT : K_MSEC(timeout_ms); }

    static MessageChannelError map_put_result(int ret) {
        if (ret == 0) return MessageChannelError::ok;
        if (ret == -EAGAIN) return MessageChannelError::timeout;
        if (ret == -ENOMSG) return MessageChannelError::full;
        return MessageChannelError::full;
    }

    static MessageChannelError map_get_result(int ret) {
        if (ret == 0) return MessageChannelError::ok;
        if (ret == -EAGAIN) return MessageChannelError::timeout;
        if (ret == -ENOMSG) return MessageChannelError::empty;
        return MessageChannelError::empty;
    }

    size_t capacity_ = MAX_CAPACITY;
    struct k_msgq queue_{};
    std::array<char, SLOT_SIZE * MAX_CAPACITY> buffer_{};
};

MessageChannelHandle create_platform_message_channel(size_t capacity) {
    auto* channel =
        new ZephyrMessageChannel(capacity); // dynamic-memory-allow: factory boundary returns owning HAL handle
    return MessageChannelHandle(channel, MessageChannelHandleDeleter{});
}

} // namespace modem

#endif