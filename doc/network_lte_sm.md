Modem Operation – State machine

> States/Actions/Events below reflect the current `NetworkLteState`/`ModemAction`/`NetworkLteEvent` enums in
> `include/modem/network_lte.h`. The "Moving from States" and "Description of Actions" sections further down are the
> original design notes and predate `transparent_mode`, `modem_fota` and `gnss_fix_mode` — see `src/network_lte.cpp`
> (`execute_actions()`, `go_to_state()`) for the authoritative transition logic for those states.

States (`NetworkLteState`)
0.	none — sentinel value, never an active operating state
1.	rebooting — AT#REBOOT sent, waiting for the modem to become responsive again
2.	switched_off — modem powered down (entry point)
3.	off_mode — powered but radio off (CFUN=0)
4.	sleep_mode — PSM (Power Saving Mode)
5.	setup_mode — reserved (radio/band/tech setup is currently handled within idle_mode via the setup_radio action)
6.	idle_mode — modem responsive, no attach/PDP flow in progress
7.	network_detached — not registered, attach flow will be (re)started
8.	network_attaching — attach requested, waiting for registration
9.	pdp_context_closed — registered, no active PDP context
10.	pdp_context_opening — PDP activation requested, waiting for IP
11.	data_ready — PDP context active, ready to send/receive data
12.	transparent_mode — UART handed over for raw AT passthrough (e.g. AT IPC, port 9002)
13.	modem_fota — firmware update in progress (`update_modem()`)
14.	gnss_fix_mode — GNSS session active, waiting for a fix (`acquire_gnss_position()`)
15.	done — terminal state (max retries reached / unrecoverable error); requires manual reset

Actions (`ModemAction`)
1.	reboot — AT#REBOOT, then power_on
2.	factory_reset — reserved, not yet implemented
3.	check_responsiveness — poll AT until the modem responds, then query_network_status
4.	power_on — set baudrate/echo, verify AT, then setup_radio
5.	power_off
6.	turn_on_radio
7.	switch_off_radio — CFUN=4 + shutdown (or reset button on devkit)
8.	enter_sleep — triggers switch_off_radio if PSM entry isn't otherwise confirmed
9.	wake_up — GPIO0 + CFUN=1 (warm boot)
10.	setup_radio — apply bands/IoT tech/PSM config (reboot if changed), then query_network_status
11.	query_network_status — AT+CEREG?, decide attach vs PDP flow
12.	query_pdp_context — AT+CGACT?, decide PDP activation vs data_ready
13.	attach_network — apply default/fallback APN, bands, tech; power the radio back on to (re)attach
14.	open_pdp_context — AT#SGACT=1
15.	send_data — drain the TX queue over the active UDP socket
16.	read_data — read one UDP payload (debug/manual read)
17.	data_complete — reserved, signal end of a data session
18.	enter_transparent_mode — hand off the UART, disable URCs
19.	leave_transparent_mode — re-enable URCs, return to idle_mode
20.	enter_gnss_fix — power the radio off, enable GNSS + NMEA URCs, → gnss_fix_mode
21.	leave_gnss_fix — disable GNSS + NMEA URCs, → idle_mode

Events (`NetworkLteEvent`)
1.	psm_enter — #PSMURC URC
2.	psm_exit — wake from PSM (GPIO/CTS)
3.	network_detached — +CEREG deregistration URC
4.	network_attached — +CEREG registration URC
5.	context_opened — +CGEV activation URC
6.	context_closed — +CGEV deactivation URC
7.	context_rejected — +CGEV activation-rejected URC
8.	data_available — SRING URC
9.	gps_available — GNSS NMEA fix URC (e.g. $GPGSA)
10.	attach_started — internal, raised after the attach command is sent
11.	pdp_opening — internal, raised after the PDP activation command is sent
12.	timeout — internal timer expiry (attach/PDP/data_ready/transparent/GNSS state timers)
13.	network_error — generic network failure, can trigger retries/fallback
14.	attach_error — max attach retries reached
15.	context_error — max PDP activation retries reached
16.	at_command_no_response — modem unresponsive to an AT command expected to always succeed

Moving from States
1.	“Switched Off” state goes to state: (Entry point – 1st time)
a.	“Idle mode” with action “Turn on Modem” followed by action “Setup radio”
2.	“Off mode” state goes to state:
a.	“Idle mode” with action “Turn on radio”
3.	“Sleep mode” state goes to state:
a.	“Idle mode” with action “Wake up modem”
4.	“Idle mode” state goes to state:
a.	“Detached” with action “Query network state”
b.	“Context deactivated” with action “Query network state”
c.	“Server deregistered” with action “Query Server state”
d.	“Data ready” witch action “Query Server state”
5.	“Detached” state goes to state:
a.	“Attaching” with action “Setup network” followed by “Attach to network” (1st attempt default cell config bands, cops and tech. 2nd attempt fallback cell config: all bands, auto cops, tech with fallback)
6.	“Attaching” state goes to state:
a.	“Detached” with timeout and tries == 1
b.	“Done” with timeout and tries == 2
c.	“Context deactivated” with event “network attached”
7.	“Context deactivated” state goes to state:
a.	“Opening PDP context” with action “Open PDP Context”
8.	“Opening PDP context” state goes to state:
a.	“Context deactivated” with timeout and tries == 1
b.	“Done” with timeout and tries == 2
c.	“Server deregistered” with event “network registered” and server state “deauthed”
d.	“Data ready” with event “network registered” and server state “authed”
9.	“Server deregistered” state goes to state:
a.	“Server registering” with action “Open Server Connection”
10.	 “Server registering” state goes to state:
a.	“Server Bootstrap” with event “rejected”
b.	“Data ready” with event “authenticated”
c.	“Done” with something else
11.	“Data ready” state goes to state:
a.	“Data ready” with action “Send data”
b.	“Data ready” with event “Received data”
c.	“Sleep mode” with event PSM
d.	“Done” with event timeout or server request
e.	“Detached” with modem event
f.	“Context deactivated” with modem event
12.	 “Server Bootstrap” state goes to state:
a.	“Server deregistered” with event “Bootstrap Complete”
b.	“Sleep mode” with event PSM
c.	“Done” with event timeout or server request
d.	“Detached” with modem event
e.	“Context deactivated” with modem event
13.	“Done” state goes to state:
a.	“Sleep mode” with action “Enter sleep mode” if supported
b.	“Off mode” with action “Enter off mode” if supported
c.	“Switched off” with action “Switch off radio” if supported
d.	“Idle Mode” with action Retry Network Connection if network error or network connection timeout occurs

Description of Actions: Work in progress
1.	Turn on Modem (timeout 15s)
1.1	Turn on power
1.2	Set baudrate
1.3	Disable echo (ok -> Wake up modem, fail once -> Turn on radio, fail twice -> Switch off radio)
2.	Setup radio
2.1	Configure modem options
2.2	Configure PSM
3.	Wakeup Modem (timeout 5s)
3.1	Check AT command (ok-> Setup network, fail once-> Turn on radio, fail twice Switch off radio)
4.	Turn on radio
4.1	AT+CFUN=1
5.	Setup Network (timeout 30s)
5.1	Set bands
5.2	Set apn
5.3	Set cops (move to Query Network)
5.4	Network attached? (ok -> Attach to network, fail once -> Setup network again with fallback, fail twice -> Enter off mode
6.	Query Network 
6.1	Is Registered? (ok proceed, fail move to Attach to network)
6.2	Has Context? (yes move to Query Server, no move to Open PDP Context)
7.	Attach to network (timeout 15/30s)
8.	Open PDP context (timeout 15s)
9.	Query Server
9.1	Is Registered? (Yes proceed to next point,No move to Open Server Connection 
9.2	Is Connected? (ok -> move to data ready, No move to Open Server Connection
10.	Open Server Connection
10.1	Set server endpoint
10.2	Connect
10.3	Authenticated? (yes move to data ready, no move to bootstrap server)
11.	Connect to Bootstrap server
11.1	Set server endpoint
11.2	Connect (yes proceed, no switch off)
11.3	Wait for setup
11.4	Go to Open Server Connection
12.	Send data 
12.1	Check queue messages
12.2	Wait for ack or requests
13.	Enter Sleep mode
13.1	This action should be automatically triggered after a timeout
14.	Enter off mode
14.1	Set modem in off mode
15.	Switch off modem
15.1	Cut off power energy

Public Blocking Calls (`NetworkLte` API)

These are the public `NetworkLte` methods that block the calling thread until the requested state is reached, a
terminal condition occurs, or a configured timeout elapses. They are the primitives the IPC/RPC servers call
(see `doc/ipc_servers.md`); all of them internally drive the state machine via `go_to_state()` / `loop()`.

1.	`bool network_connect()` — drives the state machine to data_ready. Returns false immediately (no state change)
	from transparent_mode, modem_fota or done; returns true immediately if already in data_ready.
2.	`bool network_disconnect()` — sends AT+CGATT=0 and moves to network_detached.
3.	`bool server_connect(conn_id, protocol, ip, port)` — requires data_ready or sleep_mode; opens a UDP socket
	(AT#SD) for the given connection.
4.	`bool server_disconnect(conn_id)` — closes the UDP socket (AT#SH) for the given connection.
5.	`bool force_psm()` — requires data_ready; iterates available operators to force PSM entry (test/demo helper).
6.	`bool enter_sleep()` — drives the state machine to sleep_mode (or off_mode/switched_off).
7.	`bool enter_transparent_mode()` — drives the state machine to transparent_mode.
8.	`bool exit_transparent_mode()` / `bool leave_transparent_mode()` — leave transparent_mode, re-enable URCs,
	return to idle_mode (leave_transparent_mode additionally re-enables URCs before the state change completes).
9.	`bool send_at_command(command, response, timeout_ms)` — only valid while in transparent_mode; forwards a raw
	AT command and waits up to timeout_ms for the reply.
10.	`bool update_modem(firmware_url)` — drives the state machine to modem_fota (FOTA command sequence itself is
	not yet implemented).
11.	`bool acquire_gnss_position(GnssPosition& pos)` — drives the state machine to gnss_fix_mode and blocks until
	a fix is obtained (gps_available) or gps_timeout_sec elapses; caches the result (see `gnss_position()`).
12.	`bool scan_networks(start_ch, end_ch)` — runs AT#CSURVF=2 + AT#CSURV and populates `csurv_result()`.
13.	`bool go_to_state(target_state)` — the low-level primitive all of the above build on; generates the
	necessary action/event sequence and calls `loop()` until `target_state` is reached, or returns false if the
	current state cannot transition there (e.g. done, modem_fota).
