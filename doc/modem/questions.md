# ME310

how to select CATM1 with fallback for NB-IoT? AT+COPS=0,,,8 ?
solution: AT#WS46=[<n>[,<GSM_P>]]
However, The ME310G1-W2 only supports 0 and 1 values for <n>, not 2 and 3.

how to scan networks before registration?

URC:
    AT#PSMURC=1
        +PSMURC
    AT+CGEREP=1
        +CREG || +CEREG
    AT+CGEREP - Packet Domain Event Reporting
        +CGEV

Low Power
How to leave sleep mode?
How to go to sleep mode? PSM

How to enter off mode? AT#SHDN or AT+CFUN=5 or AT#SYSHALT ?
Notes: AT#SYSHALT - System Turn-Off To power down the module, the serial port
(ASC0) must have the control signals CTS, DTR, DCD and RING low.
How to leave off mode? AT+CFUN=1 ?

Power on
On power on which configurations need to be set again?

