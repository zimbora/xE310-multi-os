#pragma once

#include "hal/message_queue_interface.h"
#include <memory>

namespace modem {

struct MessageQueueHandleDeleter {
    bool owns = true;
    void operator()(MessageQueueInterface* ptr) const {
        if (owns) {
            std::default_delete<MessageQueueInterface>{}(ptr);
        }
    }
};
using MessageQueueHandle = std::unique_ptr<MessageQueueInterface, MessageQueueHandleDeleter>;

/// Creates the platform-appropriate message queue implementation.
MessageQueueHandle create_platform_message_queue(size_t capacity = MessageQueueInterface::DEFAULT_CAPACITY);

} // namespace modem
