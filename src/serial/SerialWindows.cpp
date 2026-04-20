/*
 * SerialWindows.cpp
 *
 * Windows (Win32) implementation of ISerial.
 */

#if defined(_WIN32) || defined(_WIN64)

#include "SerialWindows.hpp"
#include <cstring>
#include <cstdio>

SerialWindows::SerialWindows(const std::string& device)
    : _device(device), _handle(INVALID_HANDLE_VALUE) {}

SerialWindows::~SerialWindows() {
    end();
}

bool SerialWindows::begin(uint32_t baudrate) {
    end();

    // Support port names > COM9 using the extended path prefix
    std::string path = _device;
    if (path.rfind("\\\\.\\", 0) != 0)
        path = "\\\\.\\" + path;

    _handle = CreateFileA(path.c_str(),
                          GENERIC_READ | GENERIC_WRITE,
                          0,                // exclusive access
                          nullptr,
                          OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL,
                          nullptr);

    if (_handle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[SerialWindows] CreateFile(%s) failed: %lu\n",
                _device.c_str(), GetLastError());
        return false;
    }

    // Configure comm parameters
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(_handle, &dcb)) {
        fprintf(stderr, "[SerialWindows] GetCommState failed: %lu\n", GetLastError());
        end();
        return false;
    }

    dcb.BaudRate = (DWORD)baudrate;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_ENABLE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX  = FALSE;

    if (!SetCommState(_handle, &dcb)) {
        fprintf(stderr, "[SerialWindows] SetCommState failed: %lu\n", GetLastError());
        end();
        return false;
    }

    // Timeouts: non-blocking read
    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout         = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant   = 5000;

    if (!SetCommTimeouts(_handle, &timeouts)) {
        fprintf(stderr, "[SerialWindows] SetCommTimeouts failed: %lu\n", GetLastError());
        end();
        return false;
    }

    PurgeComm(_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

void SerialWindows::end() {
    if (_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(_handle);
        _handle = INVALID_HANDLE_VALUE;
    }
}

int SerialWindows::available() {
    if (_handle == INVALID_HANDLE_VALUE) return 0;
    COMSTAT cs = {};
    DWORD errors = 0;
    ClearCommError(_handle, &errors, &cs);
    return (int)cs.cbInQue;
}

int SerialWindows::read() {
    if (_handle == INVALID_HANDLE_VALUE) return -1;
    uint8_t c;
    DWORD n = 0;
    if (ReadFile(_handle, &c, 1, &n, nullptr) && n == 1)
        return (int)c;
    return -1;
}

size_t SerialWindows::read(uint8_t* buf, size_t len) {
    if (_handle == INVALID_HANDLE_VALUE || buf == nullptr || len == 0) return 0;
    DWORD n = 0;
    ReadFile(_handle, buf, (DWORD)len, &n, nullptr);
    return (size_t)n;
}

size_t SerialWindows::write(const uint8_t* buf, size_t len) {
    if (_handle == INVALID_HANDLE_VALUE || buf == nullptr || len == 0) return 0;
    DWORD n = 0;
    WriteFile(_handle, buf, (DWORD)len, &n, nullptr);
    return (size_t)n;
}

size_t SerialWindows::write(const char* str) {
    if (str == nullptr) return 0;
    return write((const uint8_t*)str, strlen(str));
}

void SerialWindows::flush() {
    if (_handle != INVALID_HANDLE_VALUE)
        FlushFileBuffers(_handle);
}

#endif // Windows
