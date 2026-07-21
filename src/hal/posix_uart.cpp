#include "hal/uart_interface.h"

#ifdef MODEM_PLATFORM_POSIX

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <cerrno>
#include <cstring>

namespace modem {

namespace {

speed_t to_baud(uint32_t baud_rate) {
    switch (baud_rate) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default: return B115200;
    }
}

} // namespace

class PosixUart : public UartInterface {
public:
    PosixUart() = default;
    ~PosixUart() override { do_close(); }

    PosixUart(const PosixUart&) = delete;
    PosixUart& operator=(const PosixUart&) = delete;

    UartError open(const char* port, const UartConfig& config) override {
        if (fd_ >= 0) {
            close();
        }

        fd_ = ::open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) {
            return UartError::invalid_config;
        }

        // Clear non-blocking after open
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

        struct termios tty {};
        if (tcgetattr(fd_, &tty) != 0) {
            close();
            return UartError::invalid_config;
        }

        // Baud rate
        speed_t baud = to_baud(config.baud_rate);
        cfsetispeed(&tty, baud);
        cfsetospeed(&tty, baud);

        // Data bits
        tty.c_cflag &= ~CSIZE;
        switch (config.data_bits) {
            case 5: tty.c_cflag |= CS5; break;
            case 6: tty.c_cflag |= CS6; break;
            case 7: tty.c_cflag |= CS7; break;
            default: tty.c_cflag |= CS8; break;
        }

        // Stop bits
        if (config.stop_bits == 2) {
            tty.c_cflag |= CSTOPB;
        } else {
            tty.c_cflag &= ~CSTOPB;
        }

        // No parity
        tty.c_cflag &= ~PARENB;

        // Enable receiver, local mode
        tty.c_cflag |= (CLOCAL | CREAD);

        // No hardware flow control
        tty.c_cflag &= ~CRTSCTS;

        // Raw input
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

        // No software flow control
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

        // Raw output
        tty.c_oflag &= ~OPOST;

        // Blocking read with timeout
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = static_cast<cc_t>(config.timeout_ms / 100);

        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            close();
            return UartError::invalid_config;
        }

        // Flush pending data
        tcflush(fd_, TCIOFLUSH);

        return UartError::ok;
    }

    void close() override { do_close(); }

    bool is_open() const override { return fd_ >= 0; }

    UartError write(const uint8_t* data, size_t length) override {
        if (fd_ < 0) {
            return UartError::port_not_open;
        }

        size_t total_written = 0;
        while (total_written < length) {
            ssize_t n = ::write(fd_, data + total_written, length - total_written);
            if (n < 0) {
                if (errno == EINTR) continue;
                return UartError::write_failed;
            }
            total_written += static_cast<size_t>(n);
        }

        return UartError::ok;
    }

    UartError read(uint8_t* buffer, size_t buffer_size, size_t& bytes_read, uint32_t timeout_ms) override {
        if (fd_ < 0) {
            return UartError::port_not_open;
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd_, &read_fds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ret = select(fd_ + 1, &read_fds, nullptr, nullptr, &tv);
        if (ret < 0) {
            bytes_read = 0;
            return UartError::read_failed;
        }
        if (ret == 0) {
            bytes_read = 0;
            return UartError::timeout;
        }

        ssize_t n = ::read(fd_, buffer, buffer_size);
        if (n < 0) {
            bytes_read = 0;
            return UartError::read_failed;
        }

        bytes_read = static_cast<size_t>(n);
        if (n == 0) {
            return UartError::timeout;
        }

        return UartError::ok;
    }

private:
    void do_close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd_ = -1;
};

} // namespace modem

#include "hal/uart_factory.h"

namespace modem {

UartHandle create_platform_uart() {
    return UartHandle(new PosixUart(), UartHandleDeleter{});
}

} // namespace modem

#endif
