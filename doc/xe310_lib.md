# xE310 Modem Library — API Reference

## Overview

C++17 library to control a **Telit xE310 (ME310)** modem via serial UART using AT commands.
The library is designed to run on **Windows**, **macOS**, **Linux**, and **Zephyr OS (nRF54)**.

All platform-specific code is isolated behind a Hardware Abstraction Layer (HAL).
Core modem logic is fully platform-independent.

---

## Supported Platforms

| Platform | HAL Implementation | Build System |
|----------|-------------------|--------------|
| Windows  | `uart_win32.cpp` (Win32 serial API) | CMake |
| Linux    | `uart_posix.cpp` (POSIX termios) | CMake |
| macOS    | `uart_posix.cpp` (POSIX termios + IOKit) | CMake |
| Zephyr   | `uart_zephyr.cpp` (Zephyr UART driver) | Zephyr CMake module |

---

## Building the Library

### Desktop (Windows, Linux, macOS)

```bash
# Static library (default)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Shared library
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMODEM_BUILD_SHARED=ON
cmake --build build

# Install to system
cmake --install build --prefix /usr/local
```

#### Build without tests

```bash
cmake -B build -DMODEM_BUILD_TESTS=OFF
cmake --build build
```

### Zephyr OS

#### Zephyr v4.x (upstream) — prj.conf

```ini
CONFIG_CPP=y
CONFIG_STD_CPP17=y
CONFIG_REQUIRES_FULL_LIBCPP=y
CONFIG_MODEM_CONTROLLER=y
```

#### NCS v2.8.0 (Zephyr ~3.6.x) — prj.conf

```ini
CONFIG_CPLUSPLUS=y
CONFIG_STD_CPP17=y
CONFIG_LIB_CPLUSPLUS=y
CONFIG_MODEM_CONTROLLER=y
```

> **Note:** NCS v2.8.0 bundles Zephyr ~3.6.x. To use Zephyr v4.x, use NCS v3.0+ or plain upstream Zephyr with `west`.

---

## Including in Your Project

### CMake (FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(
    modem_xe310
    GIT_REPOSITORY <your-repo-url>
    GIT_TAG        main
)
FetchContent_MakeAvailable(modem_xe310)

target_link_libraries(your_app PRIVATE modem_xe310)
```

### CMake (add_subdirectory)

```cmake
add_subdirectory(path/to/modem)
target_link_libraries(your_app PRIVATE modem_xe310)
```

### CMake (find_package — after install)

```cmake
find_package(modem_xe310 REQUIRED)
target_link_libraries(your_app PRIVATE modem::modem_xe310)
```

### Include headers

```cpp
#include "modem/xe310.h"
```

---

## Architecture

```
┌─────────────────────────────────────┐
│          Application Code           │
├─────────────────────────────────────┤
│           xE310 (xe310.h)           │  ← Modem-specific AT commands
├─────────────────────────────────────┤
│    ModemController (modem_controller.h) │  ← AT command exchange engine
├─────────────────────────────────────┤
│      UartInterface (uart_interface.h)   │  ← HAL abstract interface
├──────────┬──────────┬───────────────┤
│ Win32    │ POSIX    │ Zephyr        │  ← Platform HAL implementations
└──────────┴──────────┴───────────────┘
```

---

## API Reference

### Enumerations

#### `SimDetMode`

SIM detection mode for `AT#SIMDET`.

| Value    | Description                    |
|----------|--------------------------------|
| `gpio`   | SIM detected via GPIO pin      |
| `always` | SIM always considered inserted |

#### `SimStatus`

SIM status from `AT#QSS`.

| Value                    | Description                      |
|--------------------------|----------------------------------|
| `not_inserted`           | SIM not inserted                 |
| `inserted`               | SIM inserted                     |
| `inserted_and_pin_unlocked` | SIM inserted, PIN unlocked    |
| `inserted_and_ready`     | SIM inserted and ready           |

#### `RadioTech`

Radio access technology for `AT+COPS`.

| Value    | Description        |
|----------|--------------------|
| `gsm`    | GSM (2G)           |
| `lte`    | LTE (4G)           |
| `cat_m1` | LTE Cat-M1 (IoT)  |
| `nb_iot` | NB-IoT (IoT)      |

#### `RegStatus`

Network registration status from `AT+CEREG`.

| Value               | Description                |
|---------------------|----------------------------|
| `not_registered`    | Not registered             |
| `registered_home`   | Registered, home network   |
| `searching`         | Searching for network      |
| `denied`            | Registration denied        |
| `unknown`           | Unknown status             |
| `registered_roaming`| Registered, roaming        |

### Structs

#### `RegistrationInfo`

Full registration info from `AT+CEREG?`.

| Field          | Type         | Description                              |
|----------------|--------------|------------------------------------------|
| `mode`         | `uint8_t`    | CEREG unsolicited result code mode       |
| `stat`         | `RegStatus`  | Registration status                      |
| `lac`          | `std::string`| Location Area Code (hex, if available)   |
| `ci`           | `std::string`| Cell ID (hex, if available)              |
| `act`          | `RadioTech`  | Access technology (if available)         |
| `has_location` | `bool`       | `true` if `lac`, `ci` fields are present |

#### `SignalQuality`

Signal measurements from `AT+CESQ`.

| Field  | Type  | Description                                  |
|--------|-------|----------------------------------------------|
| `rssi` | `int` | Received signal strength (0–31, 99=unknown)  |
| `ber`  | `int` | Bit error rate (0–7, 99=unknown)             |
| `rsrq` | `int` | Reference signal received quality (0–34)     |
| `rsrp` | `int` | Reference signal received power (0–97)       |

---

### Methods

#### Basic Commands

| Method | AT Command | Signature | Description |
|--------|-----------|-----------|-------------|
| `at_ok` | `AT` | `ModemStatus at_ok()` | Test modem communication |
| `set_baudrate` | `AT+IPR` | `ModemStatus set_baudrate(uint32_t baudrate)` | Set UART baud rate |
| `set_echo` | `ATE0/ATE1` | `ModemStatus set_echo(bool enable)` | Enable/disable command echo |

#### Identification

| Method | AT Command | Signature | Description |
|--------|-----------|-----------|-------------|
| `request_imei_sv` | `AT+IMEISV` | `ModemStatus request_imei_sv(std::string& imei_sv)` | Read IMEI software version |
| `request_model_id` | `AT#CGMM` | `ModemStatus request_model_id(std::string& model)` | Read modem model identifier |
| `request_telit_id` | `AT#TID` | `ModemStatus request_telit_id(std::string& tid)` | Read Telit module ID |
| `request_identification` | `ATI` | `ModemStatus request_identification(std::string& info)` | Read modem identification string |

#### SIM Card

| Method | AT Command | Signature | Description |
|--------|-----------|-----------|-------------|
| `read_iccid` | `AT+CCID` | `ModemStatus read_iccid(std::string& iccid)` | Read SIM ICCID |
| `read_imsi` | `AT+CIMI` | `ModemStatus read_imsi(std::string& imsi)` | Read SIM IMSI |
| `set_sim_detection` | `AT#SIMDET` | `ModemStatus set_sim_detection(SimDetMode mode)` | Set SIM detection mode |
| `query_sim_status` | `AT#QSS?` | `ModemStatus query_sim_status(SimStatus& status)` | Query SIM insertion status |
| `send_sim_command` | `AT+CSIM` | `ModemStatus send_sim_command(const std::string& command, std::string& sim_response)` | Send raw APDU to SIM |

#### Network Registration

| Method | AT Command | Signature | Description |
|--------|-----------|-----------|-------------|
| `set_bands` | `AT#BND` | `ModemStatus set_bands(uint64_t gsm, uint64_t umts, uint64_t lte, uint64_t tdscdma, uint64_t lte_over_64)` | Configure frequency bands |
| `get_bands` | `AT#BND?` | `ModemStatus get_bands(std::string& bands)` | Query current band configuration |
| `get_registration_status` | `AT+CEREG?` | `ModemStatus get_registration_status(RegistrationInfo& info)` | Query EPS registration status, location, and access technology |
| `get_signal_quality` | `AT+CESQ` | `ModemStatus get_signal_quality(SignalQuality& sq)` | Read signal quality measurements |
| `set_radio_tech` | `AT+COPS` | `ModemStatus set_radio_tech(RadioTech tech)` | Set preferred radio access technology |
| `set_operator_manual` | `AT+COPS` | `ModemStatus set_operator_manual(const std::string& oper, RadioTech tech)` | Select operator manually |
| `set_operator_auto` | `AT+COPS=0` | `ModemStatus set_operator_auto()` | Select operator automatically |
| `get_operator` | `AT+COPS?` | `ModemStatus get_operator(std::string& oper)` | Query current operator |

#### Network Attach (PDP Context)

| Method | AT Command | Signature | Description |
|--------|-----------|-----------|-------------|
| `set_apn` | `AT+CGDCONT` | `ModemStatus set_apn(uint8_t cid, const std::string& apn)` | Configure APN for a PDP context |
| `get_apn` | `AT+CGDCONT?` | `ModemStatus get_apn(uint8_t cid, std::string& apn)` | Query APN for a PDP context |
| `activate_pdp` | `AT+CGACT=1` | `ModemStatus activate_pdp(uint8_t cid)` | Activate PDP context |
| `deactivate_pdp` | `AT+CGACT=0` | `ModemStatus deactivate_pdp(uint8_t cid)` | Deactivate PDP context |
| `get_pdp_state` | `AT+CGACT?` | `ModemStatus get_pdp_state(uint8_t cid, bool& active)` | Query PDP context activation state |
| `get_ip_address` | `AT+CGPADDR` | `ModemStatus get_ip_address(uint8_t cid, std::string& ip_addr)` | Get assigned IP address |
| `get_pdp_info` | `AT+CGCONTRDP` | `ModemStatus get_pdp_info(uint8_t cid, std::string& ip, std::string& gw, std::string& dns1, std::string& dns2)` | Get IP, gateway, and DNS servers |

#### UDP Connection

| Method | AT Command | Signature | Description |
|--------|-----------|-----------|-------------|
| `udp_open` | `AT#SD` | `ModemStatus udp_open(uint8_t conn_id, const std::string& host, uint16_t remote_port, uint16_t local_port = 0, uint8_t cid = 1)` | Open UDP socket to remote host |
| `udp_listen` | `AT#SL` | `ModemStatus udp_listen(uint8_t conn_id, uint16_t local_port, uint8_t cid = 1)` | Listen for incoming UDP on a port |
| `udp_send` | `AT#SSENDEXT` | `ModemStatus udp_send(uint8_t conn_id, const std::vector<uint8_t>& data)` | Send binary data over UDP socket |
| `udp_receive` | `AT#SRECV` | `ModemStatus udp_receive(uint8_t conn_id, std::vector<uint8_t>& data, uint16_t max_bytes = 1500)` | Receive data from UDP socket |
| `udp_close` | `AT#SH` | `ModemStatus udp_close(uint8_t conn_id)` | Close UDP socket |
| `udp_status` | `AT#SS` | `ModemStatus udp_status(uint8_t conn_id, uint8_t& state)` | Query socket connection state |

---

## Return Values

All methods return `ModemStatus`:

| Value            | Description                               |
|------------------|-------------------------------------------|
| `ok`             | Command executed successfully              |
| `at_error`       | Modem returned `ERROR` or `+CME ERROR`     |
| `timeout`        | No response within the configured timeout  |
| `uart_error`     | UART read/write failure                    |
| `not_connected`  | UART port is not open                      |

---

## Usage Examples

### Minimal — Check modem connectivity

```cpp
#include "modem/xe310.h"
#include "modem/modem_controller.h"

// Platform-specific UART (e.g., Win32)
#include "modem/hal/win32_uart.h"

int main() {
    auto uart = std::make_unique<modem::UartWin32>();
    modem::ModemController controller(std::move(uart));

    if (controller.connect("COM3") != modem::ModemStatus::ok) {
        return 1;
    }

    modem::xE310 modem(controller);

    if (modem.at_ok() != modem::ModemStatus::ok) {
        return 1;
    }

    std::string model;
    modem.request_model_id(model);
    // model = "ME310G1-W1"

    return 0;
}
```

### Full flow — Register, attach, and send UDP data

```cpp
#include "modem/xe310.h"
#include "modem/modem_controller.h"
#include "modem/hal/posix_uart.h"

int main() {
    auto uart = std::make_unique<modem::UartPosix>();
    modem::ModemController controller(std::move(uart));
    controller.connect("/dev/ttyUSB0");

    modem::xE310 modem(controller);

    // 1. Basic setup
    modem.set_echo(false);
    modem.set_baudrate(115200);

    // 2. Check SIM
    modem::SimStatus sim_status;
    modem.query_sim_status(sim_status);
    if (sim_status != modem::SimStatus::inserted_and_ready) {
        return 1;
    }

    // 3. Configure and register
    modem.set_radio_tech(modem::RadioTech::cat_m1);
    modem.set_operator_auto();

    modem::RegistrationInfo reg;
    modem.get_registration_status(reg);
    if (reg.stat != modem::RegStatus::registered_home &&
        reg.stat != modem::RegStatus::registered_roaming) {
        return 1;
    }

    // 4. Attach to network
    modem.set_apn(1, "internet.operator.com");
    modem.activate_pdp(1);

    std::string ip;
    modem.get_ip_address(1, ip);
    // ip = "10.0.0.1"

    // 5. Send UDP data
    modem.udp_open(1, "192.168.1.100", 5000);

    std::vector<uint8_t> payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
    modem.udp_send(1, payload);

    // 6. Receive UDP data
    std::vector<uint8_t> rx_data;
    modem.udp_receive(1, rx_data);

    // 7. Cleanup
    modem.udp_close(1);
    modem.deactivate_pdp(1);

    return 0;
}
```

### Zephyr application

```cpp
#include "modem/xe310.h"
#include "modem/modem_controller.h"
#include "modem/hal/zephyr_uart.h"

void main(void) {
    auto uart = std::make_unique<modem::UartZephyr>();
    modem::ModemController controller(std::move(uart));
    controller.connect("UART_1");

    modem::xE310 modem(controller);
    modem.at_ok();

    // ... same API as desktop
}
```

---

## Directory Structure

```
modem/
├── include/
│   └── modem/
│       ├── xe310.h                 # xE310 modem class
│       ├── modem_controller.h      # AT command exchange engine
│       ├── at_command.h            # AT command builder / parser
│       └── uart_interface.h        # HAL abstract interface
├── src/
│   ├── xe310.cpp
│   ├── modem_controller.cpp
│   ├── at_command.cpp
│   └── hal/
│       ├── uart_win32.cpp          # Windows serial
│       ├── uart_posix.cpp          # Linux / macOS serial
│       └── uart_zephyr.cpp         # Zephyr UART driver
├── tests/
│   ├── test_xe310.cpp
│   ├── test_modem_controller.cpp
│   ├── test_at_command.cpp
│   └── CMakeLists.txt
├── zephyr/
│   ├── CMakeLists.txt
│   ├── Kconfig
│   └── module.yml
├── doc/
│   └── xe310_lib.md                # This file
└── CMakeLists.txt
```