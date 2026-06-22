# ME310

how to select CATM1 with fallback for NB-IoT? AT+COPS=0,,,8 ?
solution: AT#WS46=[<n>[,<GSM_P>]]
However, The ME310M1 only supports 0 and 1 values for <n>, not 2 and 3.

how to scan networks before registration?

How to avoid COPS long response? >> reduce bands

--- New Questions 06/09---

What means NO CARRIER? Why I receive it? Socket closed? which one?

How to wake up modem on dev kit from DH0 (PSM mode and off mode)

max time for AT+COPS=? Sometimes it takes more than 3 minutes!!!

// --- New Questions 22/09

[18:02:10.686][INF] Starting network attach with default configuration
[18:02:10.686][DBG] >>: AT+COPS=1,2,"26801",7
[18:03:37.314][DBG] <<: 
OK

+CEREG: 2

+CEREG: 2,,,,,,"00100011",

+CGEV: ME PDN ACT 1

+CEREG: 5,"0010","0824800B",7,,,"00100011",

[18:03:37.314][DBG] new state: network_attaching
[18:03:48.158][DBG] << (URC poll raw): 
+CEREG: 4,,,,,,"00100011",
 [30 bytes]

 What's the point to receive all CEREG after completed block function COPS ?
 After 10s I am receiving CEREG: 4 ??

 