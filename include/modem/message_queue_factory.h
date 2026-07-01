#pragma once

#include "modem/message_queue_interface.h"
#include <memory>

namespace modem {

/// Creates the platform-appropriate message queue implementation.
std::unique_ptr<MessageQueueInterface> create_platform_message_queue(size_t capacity = MessageQueueInterface::DEFAULT_CAPACITY);

} // namespace modem
