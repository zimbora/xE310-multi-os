# RPC Server (Port 9003)

A line-mode TCP server that exposes a simple text RPC interface for inspecting and configuring the modem at runtime. Compatible with `nc` and any tool that can open a plain TCP connection.

---

## Connection

```bash
nc <host> 9003
```

From WSL2, use the Windows host IP (the vEthernet WSL adapter address):

```bash
nc $(cat /etc/resolv.conf | grep nameserver | awk '{print $2}') 9003
```

Each line sent to the server is one request. Each response is one JSON line followed by `\r\n`. The connection stays open; send as many requests as needed.

---

## Command syntax

```
GET <RESOURCE>
SET CONFIG <key>=<value> [<key>=<value> ...]
```

Commands are case-insensitive. Values in `SET CONFIG` preserve their original case (important for strings like `default_apn`).

---

## GET resources

| Resource | Type | Description |
|---|---|---|
| `GET STATE` | string | Current `NetworkLteState` value |
| `GET CONFIG` | object | Full `NetworkLteConfig` |
| `GET MODEMINFO` | object | `ModemInfo` (IMEI, ICCID, model, firmware, …) |
| `GET SIMSTATUS` | string | `SimStatus` enum |
| `GET RADIOTECH` | string | `RadioTech` enum |
| `GET REGSTATUS` | string | `RegStatus` enum |
| `GET REGINFO` | object | `RegistrationInfo` (TAC, CI, AcT, PSM timers, reject cause) |
| `GET NETWORKINFO` | object | `NetworkInfo` (context state, IP address) |
| `GET SIGNALQUALITY` | object | `SignalQuality` (RSSI, BER, RSRQ, RSRP, RSRP dBm) |
| `GET PSMMODE` | string | `PsmMode` enum |
| `GET CPSMSCONFIG` | object | 3GPP `CpsmsConfig` (timer octet strings) |
| `GET TELITCPSMSCONFIG` | object | Telit `TelitCpsmsConfig` (timer values in seconds) |
| `GET TELITCPSMSSTATUS` | object | Telit `TelitCpsmsStatus` (network-granted timers) |
| `GET SURVEYRESULT` | object | Last `NetworkSurveyResult` (cells array) |
| `GET SERVERINFO` | array | All 5 `ServerInfo` entries |
| `GET SERVERINFO <n>` | object | Single entry for connection ID `n` (1–5) |
| `GET ALL` | object | Single response containing all of the above |

### Examples

```
GET STATE
"data_ready"

GET REGINFO
{"mode":4,"stat":"registered_home","lac":"3A2B","ci":"0A1B2C3D","act":"cat_m1","has_location":true,"cause_type":0,"reject_cause":0,"has_reject":false,"active_time":"00100000","periodic_tau":"10000110","has_psm":true}

GET SIGNALQUALITY
{"rssi":20,"ber":0,"rsrq":14,"rsrp":45,"rsrp_dbm":-95}

GET SERVERINFO 1
{"conn_id":1,"state":"connected","protocol":"UDP","address":"185.205.209.91","port":10000,"has_data":false}

GET ALL
{ ... full snapshot ... }
```

---

## SET CONFIG

Updates one or more fields of `NetworkLteConfig`. Returns the full updated config as JSON.

```
SET CONFIG <key>=<value> [<key>=<value> ...]
```

Changes take effect on the next `network.loop()` iteration. They do not automatically trigger a reconnect or reboot — the new values are picked up when the state machine next executes the relevant action (e.g. attach, PDP activation).

### Settable fields

| Key | Type | Default | Description |
|---|---|---|---|
| `cid` | uint8 | `1` | PDP context ID |
| `conn_id` | uint8 | `1` | Default connection ID for send/receive |
| `attach_timeout_sec` | uint8 | `120` | Network attach timeout |
| `pdp_timeout_sec` | uint8 | `15` | PDP context activation timeout |
| `data_ready_timeout_sec` | uint8 | `30` | Time to wait for data-ready state |
| `transparent_timeout_sec` | uint16 | `300` | Timeout while in transparent mode |
| `max_network_attempts` | uint8 | `2` | Max full connect attempts |
| `max_attach_retries` | uint8 | `2` | Max attach retries before error |
| `max_pdp_retries` | uint8 | `2` | Max PDP activation retries |
| `default_lte_bands` | uint64 | `524416` | LTE band bitmask (primary) |
| `default_iot_tech` | string | `cat_m1` | IoT tech: `gsm`, `lte`, `cat_m1`, `nb_iot` |
| `default_apn` | string | `connect.cxn` | APN for primary attach |
| `fallback_lte_bands` | uint64 | `524420` | LTE band bitmask (fallback) |
| `fallback_iot_tech` | string | `cat_m1` | IoT tech for fallback |
| `fallback_apn` | string | `anova.apn` | APN for fallback |
| `plmn` | string | `26801` | PLMN for manual operator selection (empty = auto) |
| `psm_t3412` | uint32 | `3600` | T3412 sleep timer (seconds) |
| `psm_t3324` | uint32 | `300` | T3324 active timer (seconds) |

### Examples

```
SET CONFIG max_attach_retries=5 psm_t3412=7200
{"cid":1,"attach_timeout_sec":120,...,"max_attach_retries":5,...,"psm_t3412":7200,...}

SET CONFIG default_apn=internet plmn=26806
{"cid":1,...,"default_apn":"internet",...,"plmn":"26806",...}
```

---

## Error responses

All errors are returned as a plain string (not JSON):

| Response | Cause |
|---|---|
| `ERROR: unknown GET resource` | Unrecognised resource name |
| `ERROR: unknown SET resource` | Only `CONFIG` is settable |
| `ERROR: no valid fields provided (use key=value pairs)` | Malformed or unrecognised keys |
| `ERROR: invalid conn_id` | Non-numeric argument to `GET SERVERINFO <n>` |
| `ERROR: conn_id out of range (1-5)` | Index outside `[1, MAX_SERVER_CONNECTIONS]` |
| `ERROR: unknown command — use GET <resource> or SET CONFIG <key>=<value>` | First token is neither `GET` nor `SET` |

---

## Notes

- The server runs on its own thread (separate from the main loop and the AT passthrough thread on port 9002). Requests are handled synchronously within the RPC thread — `GET` reads shared state, `SET` writes `lteConfig` — no locking is applied beyond what the `NetworkLte` methods already provide.
- The `GET SURVEYRESULT` resource returns the last cached survey. To trigger a fresh network scan, run `AT#CSURVC` via the AT passthrough server on port 9002.
- `GET MODEMINFO` contains data gathered at startup (IMEI, ICCID, model). Fields will be empty strings if the modem was not fully initialised.
