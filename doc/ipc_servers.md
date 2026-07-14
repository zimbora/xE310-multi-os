# IPC Servers

`main.cpp` exposes four TCP IPC servers so external processes can interact with the modem without linking the library directly. All servers bind to `INADDR_ANY` (port accessible from WSL2 and other local interfaces). Each server runs its accept/client loop on a dedicated background thread.

---

## Overview

| Port | Name | Wire format | Purpose |
|------|------|-------------|---------|
| 9000 | Data IPC | Line (newline-delimited) | Queue outbound data for TX; receive inbound data from the modem |
| 9001 | CoAP IPC | Framed (`[uint16_t len LE][payload]`) | Send/receive CoAP binary frames |
| 9002 | AT passthrough | Line (newline-delimited) | Forward raw AT commands to the modem and receive responses |
| 9003 | RPC IPC | Line (newline-delimited) | Query runtime modem/network state and update config fields |

---

## Port 9000 — Data IPC (line mode)

**Usage:** `nc <host> 9000`

**Inbound (client → modem):**  
Each newline-terminated line received from the client is pushed to the `NetworkLte` TX message queue for `conn_id` and the `send_data` action is scheduled. On the next `network.loop()` iteration the queue is drained and the data is sent over the active UDP socket.

**Outbound (modem → client):**  
When the modem delivers data (SRING URC → `udp_receive`), `NetworkLte` places the payload in the RX message queue. The main loop drains the RX queue and forwards each message to the connected IPC client via `ipc.send()`.

---

## Port 9001 — CoAP IPC (framed mode)

**Usage:** CoAP agent or test tool that speaks the framed binary protocol.

**Wire format:** every message is prefixed with a 2-byte little-endian length, followed by the raw payload bytes.

**Inbound:** same as port 9000 — payload pushed to the TX queue for `conn_id` and `send_data` is triggered.

**Outbound:** not yet wired (RX data is only forwarded to port 9000 in the current implementation).

---

## Port 9002 — AT Command Passthrough (line mode)

**Usage:** `nc <host> 9002`

This server puts the modem into *transparent mode* for the duration of the connection, giving the client exclusive access to the UART for raw AT command exchange.

### Connection lifecycle

```
Client connects
    └─▶ enter_transparent_mode()
            ├─ state → transparent_mode          ← FIRST (stops main-loop poll_urc race)
            ├─ AT#PSMURC=0   (disable PSM URCs)
            ├─ AT+CEREG=0    (disable registration URCs)
            └─ AT+CGEREP=0   (disable PDP URCs)

Client sends "ATI\n"
    └─▶ network.send_at_command("ATI", response, 5000ms)
            ├─ forwards command over UART
            ├─ waits up to 5 s for modem reply
            └─▶ at_ipc.send("<body>\r\nOK\r\n")   or   "ERROR\r\n"

Client disconnects
    └─▶ exit_transparent_mode()
            ├─ AT#PSMURC=1   (re-enable PSM URCs)
            ├─ AT+CEREG=4    (re-enable registration URCs)
            ├─ AT+CGEREP=1   (re-enable PDP URCs)
            └─ state → idle_mode  +  query_network_status action queued
```

### UART ownership during transparent mode

`NetworkLte::loop()` skips `poll_urc()` entirely while `state == transparent_mode`. This prevents the main thread from reading bytes off the UART concurrently with `send_at_command()` on the IPC thread — which would otherwise steal the AT response and deliver it as a spurious URC.

### Response format

`AtCommand::parse_response()` splits the raw modem reply and places the status token (`OK` / `ERROR`) in `AtResponse::status`, not in `AtResponse::body`. The IPC callback therefore reconstructs the full reply before sending it to the client:

```
<body lines joined by \r\n>          (may be empty for simple commands)
OK\r\n                                (or ERROR\r\n on failure)
```

---

## Port 9003 — RPC IPC (line mode)

**Usage:** `nc <host> 9003`

This server provides a small text RPC protocol over newline-delimited TCP messages.

### Command format

```
GET <RESOURCE>
SET CONFIG <key>=<value>[,<key>=<value>...]
SET NETWORKDISCONNECT [conn_id]
SET SERVERCONNECT <conn_id> <protocol> <ip> <port>
```

### Supported GET resources

`CONFIG`, `MODEMINFO`, `SIMSTATUS`, `RADIOTECH`, `REGSTATUS`, `REGINFO`,
`NETWORKINFO`, `SIGNALQUALITY`, `PSMMODE`, `CPSMSCONFIG`, `TELITCPSMSCONFIG`,
`TELITCPSMSSTATUS`, `SURVEYRESULT`, `OPERATORLIST`, `SCANSURVEY`, `STATE`,
`SERVERINFO [n]`, `ALL`

Notes:
- `GET SCANSURVEY` actively triggers a scan (`network.scan_networks()`) before returning JSON.
- `GET SERVERINFO` returns all server slots; `GET SERVERINFO <n>` returns one slot (`1..MAX_SERVER_CONNECTIONS`).

### Supported SET operations

- `SET CONFIG <key>=<value>[,...]`
  - Applies provided config fields via `rpc::apply_config_fields(...)`.
  - Returns the updated full config JSON.
- `SET NETWORKDISCONNECT [conn_id]`
  - Calls `server_disconnect(conn_id)` and `network_disconnect()`.
  - Returns JSON summary:
    `{"resource":"NETWORKDISCONNECT","conn_id":<n>,"server_disconnect":<bool>,"network_disconnect":<bool>}`
- `SET SERVERCONNECT <conn_id> <protocol> <ip> <port>`
  - Calls `server_connect(conn_id, protocol, ip, port)` on the network thread.
  - `conn_id`: integer in range `1..MAX_SERVER_CONNECTIONS`.
  - `protocol`: `UDP` or `TCP`.
  - `ip`: IP address or DNS hostname of the remote server.
  - `port`: remote port number (1–65535).
  - Returns JSON summary:
    `{"resource":"SERVERCONNECT","conn_id":<n>,"protocol":"<proto>","ip":"<addr>","port":<port>,"server_connect":<bool>}`

### Example session

```
GET STATE
"idle_mode"

GET CONFIG
{"cid":1,"default_lte_bands":524416,...}

SET CONFIG default_apn=my.apn
{"cid":1,"default_lte_bands":524416,...,"default_apn":"my.apn",...}

SET NETWORKDISCONNECT 1
{"resource":"NETWORKDISCONNECT","conn_id":1,"server_disconnect":true,"network_disconnect":true}

SET SERVERCONNECT 1 UDP 185.205.209.91 10000
{"resource":"SERVERCONNECT","conn_id":1,"protocol":"UDP","ip":"185.205.209.91","port":10000,"server_connect":true}
```

Errors are returned as plain text starting with `ERROR:`.

---

## Threading model

```
main thread
  └─ network.loop()   — poll_urc, state machine, TX/RX queue drain

IPC thread (one per server, spawned by IpcServer::start())
  └─ accept loop
       └─ client loop  — recv / send on the accepted socket
            ├─ port 9000/9001: calls network.tx_write() + call_action()
    ├─ port 9002:      calls network.send_at_command() (blocks until reply)
    └─ port 9003:      calls RPC handler (GET/SET, returns text/JSON)
```

`call_action()` is thread-safe (it only appends to the pending action queue). `send_at_command()` performs a synchronous UART exchange on the IPC thread; during this time the main thread must not read from UART — guaranteed by the `transparent_mode` state guard in `loop()`.
