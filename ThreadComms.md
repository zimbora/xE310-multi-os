# Thread Communications

This document explains how communication primitives are used between threads in this project.

## Purpose Split

- `message_channel`: request/response communication between threads.
- `message_queue`: data path used for network socket payloads.

## message_channel Usage (Thread Requests)

Use `message_channel` when one thread asks another thread for state or control information.

Examples:

- Requesting modem state snapshots (registration, signal quality, config).
- Sending control-oriented thread requests and waiting for a typed response.
- Exchanging internal command/response payloads where the data is not socket traffic.

Relevant interfaces:

- `include/modem/message_channel_interface.h`
- `include/modem/message_channel_factory.h`
- `include/modem/i_radio_lte.h` (`RadioLteChannels` wrapper)

## message_queue Usage (Socket Data)

Use `message_queue` for payload data that goes to or comes from network sockets.

Queue messages carry:

- `cid`: 1-based socket/server context identifier.
- `payload`: copied socket bytes.
- `length`: payload length stored with the message.

Behavior:

- TX messages are processed by the server/network thread in the same FIFO order they were queued, even when different connection IDs are interleaved.
- TX and RX queues each allow up to 5 in-flight messages at a time.
- New messages are dropped when a queue is full.

Examples:

- TX data queued by app code before `udp_send`.
- RX data queued from modem URCs before app consumption.
- Per-connection buffering behavior tied to socket connection IDs.

Relevant interfaces:

- `include/modem/message_queue_interface.h`
- `include/modem/message_queue_factory.h`
- `include/modem/i_radio_lte.h` (`IRadioLte::tx_write` / `IRadioLte::rx_read`)
- `include/modem/network_lte.h` / `src/network_lte.cpp`

## Event Flags Usage

Events are used to signal that a request, response, state update, or log is available.

Current LTE event bits:

- `MODEM_EVT_REQUEST`
- `MODEM_EVT_RESPONSE`
- `MODEM_EVT_STATE`
- `MODEM_EVT_LOG`

Guidelines:

- Set events when a new item is published.
- Wait on events in consumer threads before attempting to consume channels/queues.
- Clear matched events when the consumer has handled the corresponding data.

Relevant interfaces:

- `include/modem/event_flags_interface.h`
- `include/modem/event_flags_factory.h`
- `include/modem/i_radio_lte.h`

## Selection Rule

When choosing a primitive:

- If the communication is thread request/response or control-plane metadata, use `message_channel` + event flags.
- If the communication is socket payload data (network TX/RX), use `message_queue`.
