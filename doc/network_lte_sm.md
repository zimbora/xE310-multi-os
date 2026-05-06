Modem Operation – State machine

States
1.	Switched off
2.	Off mode
3.	Sleep mode
4.	Idle mode
5.	Detached
6.	Attaching
7.	Context deactivated
8.	Opening PDP context
9.	Server deregistered
10.	Server registering
11.	Data ready
12.	Server Bootstrap
13.	Done

Actions
1.	Turn on Modem
2.	Setup radio
3.	Wake up modem
4.	Turn on radio
5.	Setup network
6.	Query network state
7.	Attach to network
8.	Open PDP context
9.	Open server connection
10.	Query server state
11.	Send data
12.	Enter sleep mode
13.	Enter off mode
14.	Switch off radio
15.	Retry Network Connection

Events
1.	Network attached (CGREG)
2.	Network registered (CGPADDR)
3.	Not authenticated 
4.	Authenticated
5.	PSM
6.	Timeout
7.	Server request
8.	Bootstrap complete
9.	Received data
10.	Modem event (network loss) (CGREG)
11.	Modem event (context loss) (CGPADDR)
12.	Modem event (server loss) (SS)
13.	Something else

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
