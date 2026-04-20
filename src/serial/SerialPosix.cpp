/*
 * SerialPosix.cpp
 *
 * Linux / macOS (POSIX termios) implementation of ISerial.
 */

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)

#include "SerialPosix.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <cerrno>
#include <cstdio>

SerialPosix::SerialPosix(const std::string& device)
    : _device(device), _fd(-1) {}

SerialPosix::~SerialPosix() {
    end();
}

bool SerialPosix::begin(uint32_t baudrate) {
    end(); // close if already open

    _fd = open(_device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (_fd < 0) {
        fprintf(stderr, "[SerialPosix] open(%s) failed: %s\n",
                _device.c_str(), strerror(errno));
        return false;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(_fd, &tty) != 0) {
        fprintf(stderr, "[SerialPosix] tcgetattr failed: %s\n", strerror(errno));
        end();
        return false;
    }

    speed_t spd = _baudToSpeed(baudrate);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    // 8N1, no flow control
    tty.c_cflag &= ~PARENB;         // No parity
    tty.c_cflag &= ~CSTOPB;         // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;             // 8 data bits
    tty.c_cflag &= ~CRTSCTS;        // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL;  // Enable receiver, ignore modem status lines

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // No software flow control
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_oflag &= ~OPOST;  // Raw output
    tty.c_oflag &= ~ONLCR;

    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN); // Raw input

    // Non-blocking read
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1; // 0.1 s inter-character timeout

    if (tcsetattr(_fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "[SerialPosix] tcsetattr failed: %s\n", strerror(errno));
        end();
        return false;
    }

    tcflush(_fd, TCIOFLUSH);
    return true;
}

void SerialPosix::end() {
    if (_fd >= 0) {
        close(_fd);
        _fd = -1;
    }
}

int SerialPosix::available() {
    if (_fd < 0) return 0;
    int bytes = 0;
    ioctl(_fd, FIONREAD, &bytes);
    return bytes;
}

int SerialPosix::read() {
    if (_fd < 0) return -1;
    uint8_t c;
    ssize_t n = ::read(_fd, &c, 1);
    return (n == 1) ? (int)c : -1;
}

size_t SerialPosix::read(uint8_t* buf, size_t len) {
    if (_fd < 0 || buf == nullptr || len == 0) return 0;
    ssize_t n = ::read(_fd, buf, len);
    return (n > 0) ? (size_t)n : 0;
}

size_t SerialPosix::write(const uint8_t* buf, size_t len) {
    if (_fd < 0 || buf == nullptr || len == 0) return 0;
    ssize_t n = ::write(_fd, buf, len);
    return (n > 0) ? (size_t)n : 0;
}

size_t SerialPosix::write(const char* str) {
    if (str == nullptr) return 0;
    return write((const uint8_t*)str, strlen(str));
}

void SerialPosix::flush() {
    if (_fd >= 0)
        tcdrain(_fd);
}

speed_t SerialPosix::_baudToSpeed(uint32_t baud) {
    switch (baud) {
        case 1200:   return B1200;
        case 2400:   return B2400;
        case 4800:   return B4800;
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
        default:
            fprintf(stderr, "[SerialPosix] Unknown baud rate %u, defaulting to 115200\n", baud);
            return B115200;
    }
}

#endif // POSIX
