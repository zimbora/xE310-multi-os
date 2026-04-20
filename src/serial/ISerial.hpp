#pragma once

/*
 * ISerial.hpp
 *
 * Abstract serial interface used by the xE310 library.
 * Platform-specific implementations live in separate files.
 */

#include <stdint.h>
#include <stddef.h>

class ISerial {
public:
    virtual ~ISerial() {}

    /*
     * Open the serial port at the given baud rate.
     * Returns true on success.
     */
    virtual bool begin(uint32_t baudrate) = 0;

    /*
     * Close the serial port.
     */
    virtual void end() = 0;

    /*
     * Returns the number of bytes available to read.
     */
    virtual int available() = 0;

    /*
     * Read a single byte (-1 if none available).
     */
    virtual int read() = 0;

    /*
     * Read up to @len bytes into @buf.
     * Returns the number of bytes actually read.
     */
    virtual size_t read(uint8_t* buf, size_t len) = 0;

    /*
     * Write @len bytes from @buf.
     * Returns the number of bytes written.
     */
    virtual size_t write(const uint8_t* buf, size_t len) = 0;

    /*
     * Write a null-terminated string.
     * Returns the number of bytes written.
     */
    virtual size_t write(const char* str) = 0;

    /*
     * Flush any pending TX data.
     */
    virtual void flush() = 0;
};
