#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace modem {

enum class MessageChannelError : uint8_t {
    ok = 0,
    full,
    empty,
    timeout,
    invalid_argument,
    invalid_size,
};

static constexpr size_t MESSAGE_CHANNEL_MAX_DATA = 256;

struct MessageFrame {
    std::array<uint8_t, MESSAGE_CHANNEL_MAX_DATA> data{};
    size_t length = 0;
};

/// Abstract fixed-size message channel interface.
/// Implementations provide FIFO message delivery for arbitrary binary payloads.
class MessageChannelInterface {
public:
    static constexpr size_t DEFAULT_CAPACITY = 16;

    MessageChannelInterface() = default;
    MessageChannelInterface(const MessageChannelInterface&) = delete;
    MessageChannelInterface& operator=(const MessageChannelInterface&) = delete;
    MessageChannelInterface(MessageChannelInterface&&) = delete;
    MessageChannelInterface& operator=(MessageChannelInterface&&) = delete;
    virtual ~MessageChannelInterface() = default;

    /// Push a message into the channel.
    virtual MessageChannelError send(const uint8_t* data, size_t length, uint32_t timeout_ms = 0) = 0;

    /// Pop the next available message from the channel.
    virtual MessageChannelError receive(MessageFrame& msg, uint32_t timeout_ms = 0) = 0;

    /// Returns the number of queued messages.
    virtual size_t count() const = 0;
};

} // namespace modem