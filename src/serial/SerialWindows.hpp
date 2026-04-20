#pragma once

/*
 * SerialWindows.hpp
 *
 * Windows (Win32) implementation of ISerial.
 */

#if defined(_WIN32) || defined(_WIN64)

#include "ISerial.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

class SerialWindows : public ISerial {
public:
    /*
     * @device – COM port name, e.g. "COM3" or "\\\\.\\COM10" for ports > 9
     */
    explicit SerialWindows(const std::string& device);
    ~SerialWindows() override;

    bool   begin(uint32_t baudrate) override;
    void   end()                    override;
    int    available()              override;
    int    read()                   override;
    size_t read(uint8_t* buf, size_t len) override;
    size_t write(const uint8_t* buf, size_t len) override;
    size_t write(const char* str)   override;
    void   flush()                  override;

private:
    std::string _device;
    HANDLE      _handle;
};

#endif // Windows
