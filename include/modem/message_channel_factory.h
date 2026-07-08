#pragma once

#include "modem/message_channel_interface.h"

#include <memory>

namespace modem {

struct MessageChannelHandleDeleter {
    bool owns = true;
    void operator()(MessageChannelInterface* ptr) const {
        if (owns) {
            std::default_delete<MessageChannelInterface>{}(ptr);
        }
    }
};

using MessageChannelHandle = std::unique_ptr<MessageChannelInterface, MessageChannelHandleDeleter>;

/// Creates the platform-appropriate message channel implementation.
MessageChannelHandle create_platform_message_channel(size_t capacity = MessageChannelInterface::DEFAULT_CAPACITY);

} // namespace modem