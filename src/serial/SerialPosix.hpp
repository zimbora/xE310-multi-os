#pragma once

/*
 * SerialPosix.hpp
 *
 * Linux / macOS (POSIX termios) implementation of ISerial.
 */

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)

#include "ISerial.hpp"
#include <string>
#include <termios.h>

class SerialPosix : public ISerial {
public:
    /*
     * @device – path to the serial device, e.g. "/dev/ttyUSB0"
     */
    explicit SerialPosix(const std::string& device);
    ~SerialPosix() override;

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
    int         _fd;    // file descriptor (-1 when closed)

    static speed_t _baudToSpeed(uint32_t baud);
};

#endif // POSIX
