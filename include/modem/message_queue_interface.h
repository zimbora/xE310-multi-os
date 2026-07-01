#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace modem {

enum class QueueError {
    ok = 0,
    full,
    empty,
    timeout,
    invalid_id,
};

struct QueueMessage {
    std::vector<uint8_t> data;
};

/// Abstract message queue interface — implemented per platform.
/// Each instance manages TX and RX queues for a fixed number of connection IDs.
class MessageQueueInterface {
public:
    static constexpr uint8_t MAX_CONNECTIONS = 5;
    static constexpr size_t  DEFAULT_CAPACITY = 16;

    virtual ~MessageQueueInterface() = default;

    /// Push a message onto the TX queue for the given connection ID (1-based).
    virtual QueueError tx_push(uint8_t conn_id, const uint8_t* data, size_t length, uint32_t timeout_ms = 0) = 0;

    /// Pop a message from the TX queue for the given connection ID (1-based).
    virtual QueueError tx_pop(uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms = 0) = 0;

    /// Returns the number of messages in the TX queue for the given connection ID.
    virtual size_t tx_count(uint8_t conn_id) const = 0;

    /// Push a message onto the RX queue for the given connection ID (1-based).
    virtual QueueError rx_push(uint8_t conn_id, const uint8_t* data, size_t length, uint32_t timeout_ms = 0) = 0;

    /// Pop a message from the RX queue for the given connection ID (1-based).
    virtual QueueError rx_pop(uint8_t conn_id, QueueMessage& msg, uint32_t timeout_ms = 0) = 0;

    /// Returns the number of messages in the RX queue for the given connection ID.
    virtual size_t rx_count(uint8_t conn_id) const = 0;
};

} // namespace modem
