# xE310-multi-os

Cross-platform C++ library to control **Telit xE310** LTE-M / NB-IoT modules.

Supports the following platforms out of the box:

| Platform | Serial backend | Example |
|---|---|---|
| Arduino / ESP32 | `HardwareSerial` | `examples/arduino/basic/` |
| Linux / macOS | POSIX `termios` | `examples/linux/basic/` |
| Windows | Win32 `CreateFile` | `examples/windows/basic/` |

---

## Features

- **AT command engine** – send/receive with timeout, filter by prefix
- **Power management** – power-on, power-off, power-cycle, soft-reset
- **Module identification** – IMEI, ICCID, IMSI, model, firmware version
- **Network** – registration status, RSSI, operator, RAT selection (GSM / LTE-M / NB-IoT)
- **Data connection** – APN setup, PDP context activate/deactivate, IP address
- **TCP** – connect, send, receive, close, disconnect callback
- **UDP** – open, send, close
- **DNS** – hostname resolution
- **Ping** – ICMP ping with RTT
- **SMS** – send messages, incoming SMS callback
- **Clock** – read network time (AT+CCLK)
- **Event loop** – `loop()` handles all URCs and keeps state up-to-date

---

## Repository layout

```
src/
  platform.hpp             Platform detection and timing macros
  xE310.hpp                Main library header
  xE310.cpp                Main library implementation
  serial/
    ISerial.hpp            Abstract serial interface
    SerialArduino.hpp      Arduino / ESP32 implementation (header-only)
    SerialPosix.hpp/.cpp   Linux / macOS implementation
    SerialWindows.hpp/.cpp Windows implementation
examples/
  arduino/basic/basic.ino  Arduino / ESP32 example sketch
  linux/basic/main.cpp     Linux / macOS command-line example
  windows/basic/main.cpp   Windows command-line example
CMakeLists.txt             Desktop build (Linux / macOS / Windows)
library.properties         Arduino Library Manager metadata
```

---

## Arduino / ESP32 quick start

1. Copy the repository into your Arduino `libraries/` folder (or install via
   the Library Manager once published).
2. Open `examples/arduino/basic/basic.ino`.
3. Adjust the pin numbers, APN, and network settings at the top of the sketch.
4. Upload and open the Serial Monitor at 115200 baud.

```cpp
#include "xE310.hpp"
#include "SerialArduino.hpp"

SerialArduino modemSerial(&Serial2);
XE310         modem(&modemSerial, /* PWRKEY pin */ 4);

void setup() {
    modemSerial.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17
    modem.init(XE310_RAT_CATM1);
    modem.setupAPN(1, "internet");
    modem.activatePDP(1);
    modem.tcpConnect(0, "tcpbin.com", 4242);
    modem.tcpSend(0, "Hello!\r\n");
    modem.tcpClose(0);
}

void loop() {
    modem.loop();
}
```

---

## Linux / macOS quick start

```bash
git clone https://github.com/zimbora/xE310-multi-os.git
cd xE310-multi-os
mkdir build && cd build
cmake ..
make
./xE310_example /dev/ttyUSB0
```

```cpp
#include "xE310.hpp"
#include "SerialPosix.hpp"

SerialPosix serial("/dev/ttyUSB0");
XE310       modem(&serial);

int main() {
    serial.begin(115200);
    modem.init(XE310_RAT_CATM1);
    modem.setupAPN(1, "internet");
    modem.activatePDP(1);
    modem.tcpConnect(0, "tcpbin.com", 4242);
    modem.tcpSend(0, "Hello!\r\n");
    modem.tcpClose(0);
}
```

---

## Windows quick start

```bat
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build .
xE310_example.exe COM3
```

```cpp
#include "xE310.hpp"
#include "SerialWindows.hpp"

SerialWindows serial("COM3");
XE310         modem(&serial);

int main() {
    serial.begin(115200);
    modem.init(XE310_RAT_CATM1);
    modem.setupAPN(1, "internet");
    modem.activatePDP(1);
    modem.tcpConnect(0, "tcpbin.com", 4242);
    modem.tcpSend(0, "Hello!\r\n");
    modem.tcpClose(0);
}
```

---

## API reference

### Construction

```cpp
XE310(ISerial* serial, int8_t pwkey = -1);
```

### Port & initialisation

| Method | Description |
|---|---|
| `begin(baudrate)` | Open serial port |
| `end()` | Close serial port |
| `init(rat, cops)` | Full module init (power-cycle + config) |

### Power management

| Method | Description |
|---|---|
| `powerOn()` | Toggle PWRKEY to switch module on |
| `powerOff()` | Graceful shutdown (`AT#SHDN`) |
| `powerCycle()` | Power-off then power-on |
| `softReset()` | Software reboot (`AT#REBOOT`) |

### Module identification

| Method | Returns |
|---|---|
| `getIMEI()` | IMEI string |
| `getICCID()` | SIM card ICCID |
| `getIMSI()` | SIM IMSI |
| `getModel()` | Module model name |
| `getManufacturer()` | Manufacturer string |
| `getFirmwareVersion()` | Firmware version |

### Network

| Method | Description |
|---|---|
| `isRegistered()` | True if registered (home or roaming) |
| `getRegistrationStatus()` | Raw CREG/CEREG status (0-5) |
| `getRSSI()` | Signal strength in dBm (99 = unknown) |
| `getOperator()` | Current operator |

### Data connection

| Method | Description |
|---|---|
| `setupAPN(cid, apn, user, pass)` | Configure PDP context |
| `activatePDP(cid)` | Activate PDP context |
| `deactivatePDP(cid)` | Deactivate PDP context |
| `getIP(cid)` | Get assigned IP address |
| `isContextActive(cid)` | Check context state |

### TCP

| Method | Description |
|---|---|
| `tcpConnect(socket, host, port, cid, timeout)` | Open TCP connection |
| `tcpClose(socket)` | Close connection |
| `tcpSend(socket, data, len)` | Send binary data |
| `tcpSend(socket, text)` | Send string |
| `tcpRecv(socket, buf, maxLen, timeout)` | Receive data |
| `tcpIsConnected(socket)` | Check socket state |
| `tcpSetCloseCallback(cb)` | Register close callback |

### UDP

| Method | Description |
|---|---|
| `udpOpen(socket, localPort, cid)` | Open UDP socket |
| `udpClose(socket)` | Close socket |
| `udpSend(socket, host, port, data, len)` | Send datagram |

### Other

| Method | Description |
|---|---|
| `dnsResolve(hostname)` | DNS lookup → IP string |
| `ping(host, retries)` | ICMP ping → RTT in ms |
| `sendSMS(number, message)` | Send SMS |
| `setSMSHandler(cb)` | Register incoming SMS callback |
| `getNetworkTime(tm*)` | Read network clock |
| `loop()` | Process URCs (call frequently) |
| `logStatus()` | Print status summary |

---

## Radio access technology constants

| Constant | Description |
|---|---|
| `XE310_RAT_AUTO` | Automatic selection |
| `XE310_RAT_GSM` | GSM only |
| `XE310_RAT_CATM1` | LTE Cat-M1 only |
| `XE310_RAT_NBIOT` | NB-IoT only |

---

## License

MIT – see [LICENSE](LICENSE).
