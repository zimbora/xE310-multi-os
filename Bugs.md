Code is stalled while in state network_attaching
Waiting for a CEREG but nothing is received.
No timeout seems to be triggered !!

[19:11:58.785][DBG][M] >>: AT
[19:11:58.860][DBG][M] <<: 
OK

[19:11:58.861][INF][M] Modem initialized successfully
[19:11:58.863][INF][M] IPC server listening on localhost:9000
[19:11:58.863][INF][M] CoAP IPC server listening on localhost:9001 (framed binary)
[19:11:58.864][INF][M] AT IPC server listening on localhost:9002 (AT command passthrough)
[19:11:58.864][INF][M] RPC IPC server listening on localhost:9003 (RPC get/set)
[19:11:58.865][DBG][N] new action: power_on
[19:11:58.877][DBG][N] Executing action: power_on
[19:11:58.877][DBG][M] >>: AT+IPR=115200
[19:11:59.013][DBG][M] <<: 
OK

[19:11:59.013][DBG][M] >>: ATE0
[19:11:59.088][DBG][M] <<: 
OK

[19:11:59.089][DBG][M] >>: AT
[19:11:59.164][DBG][M] <<: 
OK

[19:11:59.164][INF][N] Modem powered on and responsive
[19:11:59.164][DBG][N] new state: idle_mode
[19:11:59.164][DBG][N] new action: setup_radio
[19:11:59.179][DBG][N] Executing action: setup_radio
[19:11:59.179][INF][N] Re-applying modem configuration after boot
[19:11:59.180][DBG][M] >>: AT#CPSMS=1,,,3600,60
[19:11:59.344][DBG][M] <<: 
OK

[19:11:59.344][DBG][M] >>: AT#PSMURC=1
[19:11:59.464][DBG][M] <<: 
OK

[19:11:59.465][DBG][M] >>: AT+CEREG=4
[19:11:59.571][DBG][M] <<: 
OK

[19:11:59.571][DBG][M] >>: AT+CGEREP=1
[19:11:59.662][DBG][M] <<: 
OK

[19:11:59.663][DBG][N] new action: query_network_status
[19:11:59.678][DBG][N] Executing action: query_network_status
[19:11:59.678][DBG][M] >>: AT+CEREG?
[19:11:59.784][DBG][M] <<: 
+CEREG: 4,2

OK

[19:11:59.784][DBG][N] new state: network_detached
[19:11:59.784][INF][N] State changed to network_detached, resetting network info and server states
[19:11:59.785][DBG][N] new action: attach_network
[19:11:59.799][DBG][N] Executing action: attach_network
[19:11:59.800][DBG][M] >>: AT#WS46?
[19:11:59.890][DBG][M] <<: 
#WS46: 0,0

OK

[19:11:59.891][DBG][M] >>: AT+CGDCONT?
[19:12:00.377][DBG][M] <<: 
+CGDCONT: 1,"IP","connect.cxn","",0,0,0,0
+CGDCONT: 2,"IPV4V6","","0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0",0,0,0,0
+CGDCONT: 3,"IPV4V6","","0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0",0,0,0,0
+CGDCONT: 4,"IPV4V6","","0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0",0,0,0,0
+CGDCONT: 5,"IPV4V6","","0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0",0,0,0,0
+CGDCONT: 6,"IPV4V6","","0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0",0,0,0,0

OK

[19:12:00.377][DBG][M] >>: AT#BND?
[19:12:00.634][DBG][M] <<: 
#BND: 0,0,134742149,0,1048578

OK

[19:12:00.635][INF][N] Starting network attach with default configuration
[19:12:00.636][DBG][M] >>: AT+COPS=1,2,"26801",7
[19:14:15.359][DBG][M] <<: 
OK

[19:14:15.359][DBG][N] new state: network_attaching