# Tests — Modem Controller

## Overview

Unit tests for the modem controller library using [Google Test](https://github.com/google/googletest) and [Google Mock](https://github.com/google/googletest/tree/main/googlemock). Tests run on **desktop platforms only** (Windows, macOS, Linux).

All tests mock the HAL (`UartInterface`) so no real hardware is required.

## Test Structure

| File                        | Description                                      |
|-----------------------------|--------------------------------------------------|
| `test_at_command.cpp`       | AT command building and response parsing          |
| `test_modem_controller.cpp` | UART connection, send/receive, error handling     |
| `test_xe310.cpp`            | xE310 modem commands (SIM, network, PDP, UDP)     |

## Prerequisites

- C++17 compatible compiler
- CMake 3.20+
- Google Test is fetched automatically via `FetchContent` (no manual install needed)

## Build

From the project root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Run All Tests

```bash
cd build
ctest --output-on-failure
```

Or run the test binary directly:

```bash
.\build\tests\Debug\modem_tests.exe        # Windows
./build/tests/modem_tests                   # Linux / macOS
```

## Run with Verbose Output

```bash
ctest --output-on-failure --verbose
```

## List Available Tests

```bash
.\build\tests\Debug\modem_tests.exe --gtest_list_tests
```

## Run Specific Tests

Use `--gtest_filter` to run a subset of tests:

```bash
# All xE310 tests
.\build\tests\Debug\modem_tests.exe --gtest_filter="Xe310Test.*"

# Only SIM-related tests
.\build\tests\Debug\modem_tests.exe --gtest_filter="Xe310Test.*Sim*"

# Only UDP tests
.\build\tests\Debug\modem_tests.exe --gtest_filter="Xe310Test.Udp*"

# Only modem controller tests
.\build\tests\Debug\modem_tests.exe --gtest_filter="ModemControllerTest.*"

# A single test
.\build\tests\Debug\modem_tests.exe --gtest_filter="Xe310Test.AtOkSuccess"
```

## Run Tests from VS Code

1. Install the **CMake Tools** or **C++ TestMate** extension
2. Open the **Testing** panel (beaker icon in the sidebar)
3. Tests are auto-discovered — run or debug them individually

## Test Categories

### AT Command Tests (`test_at_command.cpp`)

- Command string construction with parameters
- Response parsing (`OK`, `ERROR`, `+CME ERROR`)
- Timeout configuration

### Modem Controller Tests (`test_modem_controller.cpp`)

- Connect / disconnect lifecycle
- Send command when not connected
- UART write and read errors
- Timeout handling

### xE310 Modem Tests (`test_xe310.cpp`)

| Category              | What is tested                                              |
|-----------------------|-------------------------------------------------------------|
| Basic commands        | `at_ok`, `set_baudrate`, `set_echo`                         |
| Identification        | `request_imei_sv`, `request_model_id`, `request_telit_id`   |
| SIM card              | `read_iccid`, `read_imsi`, `set_sim_detection`, `query_sim_status` |
| Network registration  | `set_bands`, `get_registration_status`, `get_signal_quality`, `set_radio_tech`, `set_operator_*` |
| Network attach        | `set_apn`, `activate_pdp`, `deactivate_pdp`, `get_pdp_state`, `get_ip_address`, `get_pdp_info` |
| UDP connection        | `udp_open`, `udp_listen`, `udp_send`, `udp_receive`, `udp_close`, `udp_status` |
| Error handling        | Timeout, AT error, not connected                            |

## Writing New Tests

1. Add test cases to an existing file or create a new `.cpp` file
2. If creating a new file, register it in `tests/CMakeLists.txt`:

    ```cmake
    add_executable(modem_tests
        test_at_command.cpp
        test_modem_controller.cpp
        test_xe310.cpp
        test_new_feature.cpp    # <-- add here
    )
    ```

3. Mock the HAL interface — never depend on real hardware:

    ```cpp
    class MockUart : public UartInterface {
    public:
        MOCK_METHOD(UartError, open, (const char* port, const UartConfig& config), (override));
        MOCK_METHOD(void, close, (), (override));
        MOCK_METHOD(bool, is_open, (), (const, override));
        MOCK_METHOD(UartError, write, (const uint8_t* data, size_t length), (override));
        MOCK_METHOD(UartError, read,
                    (uint8_t* buffer, size_t buffer_size, size_t& bytes_read, uint32_t timeout_ms),
                    (override));
    };
    ```

4. Follow naming conventions: `snake_case` for variables/functions, `PascalCase` for test fixtures
5. Rebuild and run:

    ```bash
    cmake --build build
    cd build && ctest --output-on-failure
    ```