---
name: isolatedEnv
description: design a logic to isolate functions from these thread. Use events and messages to perform actions or return data
argument-hint: "new feature"
# tools: ['vscode', 'execute', 'read', 'agent', 'edit', 'search', 'web', 'todo'] # specify the tools this agent can use. If not set, all enabled tools are allowed.
---

<!-- Tip: Use /create-agent in chat to generate content with agent assistance -->

Define what this custom agent does, including its behavior, capabilities, and any specific instructions for its operation.
This custom agent is designed to isolate functions from the main thread of execution, allowing for concurrent processing and improved performance. It utilizes events and messages to communicate between isolated functions and the main thread, enabling actions to be performed or data to be returned without blocking the main execution flow.

Use the following guidelines for its operation:
2. IPC Primitives Overview
The firmware uses two Zephyr IPC primitives for all cross-thread communication. Understanding
their semantics is essential before reading the interface specifications.
2.1 Message Queues (k_msgq)
A k_msgq is a fixed-size, FIFO ring buffer for passing structured data between threads. Each
queue holds messages of a single, fixed size. The producer thread calls k_msgq_put() to
enqueue a message; the consumer thread calls k_msgq_get() to dequeue. Both operations can
block with a timeout or return immediately with K_NO_WAIT.
Key properties: thread-safe by design (no external mutex needed), zero-copy within the queue
buffer, fixed memory footprint declared at compile time, and backpressure via K_FOREVER or
bounded timeout on put.
2.2 Events (k_event)
A k_event is a 32-bit bitmask used for lightweight state signaling. A producer thread calls
k_event_post() to set one or more bits; a consumer thread calls k_event_wait() to block until the
desired bits are set. Events do not carry data—they signal that something has happened, and
the consumer reads the associated state from a shared-but-thread-safe location (typically the
most recent message in a queue, or a thread-local snapshot).
Key properties: multiple bits can be set and waited on simultaneously, wait can require all bits
(K_EVENT_WAIT_ALL) or any bit (K_EVENT_WAIT_ANY), and posting is non-blocking and
safe from ISR context.
2.3 Naming Convention
All IPC objects follow a consistent naming scheme to make cross-references unambiguous
across code and documentation:
Object Type Naming Pattern Example
Message queue <module>_<direction>_q modem_tx_q, sensor_data_q
Event <module>_evt conn_state_evt, alarm_evt
Event bit <MODULE>_EVT_<NAME> MODEM_EVT_CONNECTED,
SENSOR_EVT_ALARM
Message struct <Module><Purpose>Msg ModemTxMsg, SensorDataMsg
2.4 Sending Structs Between Threads
When a thread needs data owned by another thread, it cannot call methods across the thread
boundary. Instead, the requesting thread sends a command message through a k_msgq, and
the owning thread serializes the result into one or more response messages on the return
Anova | Abstract Interface Specification | Proprietary and Confidential
queue. Both sides compile from the same header file, so the struct layout is the encoding—no
external serialization library is needed.
Two approaches are available. Both use zero heap allocation, are safe under Zephyr’s -fnoexceptions / -fno-rtti constraints, and rely entirely on compile-time-sized types.
Option A — Typed Message Struct (Recommended)
Define a dedicated POD struct for each message type. The struct itself is the wire format—the
consumer reads named fields directly with no decoding step. Each struct gets its own k_msgq
sized to that struct.
Struct definition (shared header):
struct OperatorEntryMsg {
 uint8_t index; // 0-based position in list
 uint8_t totalCount; // total entries in response
 uint8_t status; // 0=unknown, 1=available, 2=current, 3=forbidden
 uint8_t act; // access technology
 char shortName[16]; // operator short name, null-terminated
 char longName[32]; // operator long name, null-terminated
 uint32_t numericId; // PLMN numeric (MCC+MNC)
};
Producer (Modem thread) — field-by-field copy from internal object:
const auto& ops = modem.available_operators();
for (uint8_t i = 0; i < ops.size(); i++) {
 OperatorEntryMsg entry{};
 entry.index = i;
 entry.totalCount = static_cast<uint8_t>(ops.size());
 entry.status = static_cast<uint8_t>(ops[i].status);
 entry.act = static_cast<uint8_t>(ops[i].act);
 entry.numericId = ops[i].numericId;
 strncpy(entry.shortName, ops[i].shortName.data(), sizeof(entry.shortName) - 1);
 strncpy(entry.longName, ops[i].longName.data(), sizeof(entry.longName) - 1);
 k_msgq_put(&operator_q, &entry, K_FOREVER);
}
k_event_post(&conn_state_evt, MODEM_EVT_OPERATORS_READY);
Consumer (Protocol thread) — reads fields directly, no decoding:
k_event_wait(&conn_state_evt, MODEM_EVT_OPERATORS_READY, true, K_FOREVER);
OperatorEntryMsg entry;
while (k_msgq_get(&operator_q, &entry, K_NO_WAIT) == 0) {
 // Fields are directly accessible — no parsing needed
 log_operator(entry.longName, entry.numericId, entry.status);
}
Pros: Compile-time type safety—the compiler catches field name typos and type mismatches. No
memcpy/cast gymnastics. Each message type has a dedicated queue with the correct element
size. This is the recommended approach for all interfaces.

