#include "hal/uart_interface.h"

#ifdef MODEM_PLATFORM_WINDOWS

#include <windows.h>
#include <string>

namespace modem {

class Win32Uart : public UartInterface {
public:
    Win32Uart() = default;
    ~Win32Uart() override { do_close(); }

    Win32Uart(const Win32Uart&) = delete;
    Win32Uart& operator=(const Win32Uart&) = delete;

    UartError open(const char* port, const UartConfig& config) override {
        if (handle_ != INVALID_HANDLE_VALUE) {
            close();
        }

        // Win32 serial ports need the \\.\prefix for COM10+, works for all
        std::string full_path = std::string("\\\\.\\") + port;

        handle_ = CreateFileA(full_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);

        if (handle_ == INVALID_HANDLE_VALUE) {
            return UartError::invalid_config;
        }

        // Configure baud rate, data bits, stop bits, no parity
        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle_, &dcb)) {
            close();
            return UartError::invalid_config;
        }

        dcb.BaudRate = config.baud_rate;
        dcb.ByteSize = config.data_bits;
        dcb.StopBits = (config.stop_bits == 2) ? TWOSTOPBITS : ONESTOPBIT;
        dcb.Parity = NOPARITY;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;

        if (!SetCommState(handle_, &dcb)) {
            close();
            return UartError::invalid_config;
        }

        // Configure timeouts
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = config.timeout_ms;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = config.timeout_ms;
        timeouts.WriteTotalTimeoutMultiplier = 0;

        if (!SetCommTimeouts(handle_, &timeouts)) {
            close();
            return UartError::invalid_config;
        }

        // Clear any pending data
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);

        return UartError::ok;
    }

    void close() override { do_close(); }

    bool is_open() const override { return handle_ != INVALID_HANDLE_VALUE; }

    UartError write(const uint8_t* data, size_t length) override {
        if (handle_ == INVALID_HANDLE_VALUE) {
            return UartError::port_not_open;
        }

        DWORD bytes_written = 0;
        if (!WriteFile(handle_, data, static_cast<DWORD>(length), &bytes_written, nullptr)) {
            return UartError::write_failed;
        }

        if (bytes_written != static_cast<DWORD>(length)) {
            return UartError::write_failed;
        }

        return UartError::ok;
    }

    UartError read(uint8_t* buffer, size_t buffer_size, size_t& bytes_read, uint32_t timeout_ms) override {
        if (handle_ == INVALID_HANDLE_VALUE) {
            return UartError::port_not_open;
        }

        // Update read timeout if different from current
        COMMTIMEOUTS timeouts{};
        if (!GetCommTimeouts(handle_, &timeouts)) {
            return UartError::read_failed;
        }
        if (timeouts.ReadTotalTimeoutConstant != timeout_ms) {
            timeouts.ReadTotalTimeoutConstant = timeout_ms;
            SetCommTimeouts(handle_, &timeouts);
        }

        DWORD read_count = 0;
        if (!ReadFile(handle_, buffer, static_cast<DWORD>(buffer_size), &read_count, nullptr)) {
            bytes_read = 0;
            return UartError::read_failed;
        }

        bytes_read = static_cast<size_t>(read_count);

        if (read_count == 0) {
            return UartError::timeout;
        }

        return UartError::ok;
    }

private:
    void do_close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

} // namespace modem

#include "hal/uart_factory.h"

namespace modem {

UartHandle create_platform_uart() {
    return UartHandle(new Win32Uart(), UartHandleDeleter{});
}

} // namespace modem

#endif
