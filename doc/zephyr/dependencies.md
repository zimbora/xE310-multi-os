# Zephyr (nRF54) Dependencies

## Toolchain

- nRF Connect SDK (NCS) v2.8.0 or later
- Zephyr SDK (arm-zephyr-eabi toolchain)
- west (Zephyr meta-tool)
- CMake >= 3.20
- Ninja
- Python >= 3.10

## Python Packages

- jsonschema (required by Zephyr board detection)
- Full list: `zephyr/scripts/requirements.txt`

## Zephyr Kconfig

- CONFIG_SERIAL=y
- CONFIG_UART_ASYNC_API=y
- CONFIG_CPP=y
- CONFIG_STD_CPP17=y
- CONFIG_REQUIRES_FULL_LIBCPP=y

## Zephyr Modules / APIs

- `<zephyr/device.h>` — device binding
- `<zephyr/drivers/uart.h>` — UART driver (uart_poll_in, uart_poll_out, uart_configure)
- `<zephyr/kernel.h>` — kernel timing (k_uptime_get, k_sleep)

## C++ Standard Library

- `<string>`
- `<memory>` (std::unique_ptr, std::make_unique)
- `<cstdint>`
- `<cstring>`
- `<algorithm>`

## Build Command

```
west build -b nrf54l15dk/nrf54l15/cpuapp <project_path> --pristine
```
