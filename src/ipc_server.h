#pragma once

#include <cstdint>
#include <functional>
#include <string>

/// Lightweight TCP IPC server for desktop testing only.
/// Not part of the modem library — linked only into modem_app.
///
/// Two wire formats selectable at construction time:
///   Mode::line   — newline-delimited text (\n). Compatible with nc/telnet.
///   Mode::framed — binary [uint16_t len LE][payload]. Compatible with CoAP clients.
///
/// On Zephyr this is replaced by a dedicated thread that calls
/// network.tx_write() directly from whatever data source is available.

class IpcServer {
public:
    enum class Mode { line, framed };

    /// Called from the background thread for each complete received message.
    using MessageCallback = std::function<void(const uint8_t* data, uint16_t length)>;

    explicit IpcServer(uint16_t port, MessageCallback on_message, Mode mode = Mode::line);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    /// Replace the message callback (must be called before start(), or while stopped).
    void set_callback(MessageCallback on_message) { on_message_ = std::move(on_message); }

    /// Start the server (non-blocking — spawns a background accept thread).
    bool start();

    /// Stop the server and close all sockets.
    void stop();

    /// Send a reply to the currently connected client.
    /// Mode::line  — appends \n automatically.
    /// Mode::framed — prepends uint16_t length header.
    /// Returns false if no client is connected.
    bool send(const uint8_t* data, uint16_t length);

    bool is_running() const { return running_; }

private:
    void accept_loop();
    void client_loop(int client_fd);
    void client_loop_line(int fd);
    void client_loop_framed(int fd);

    uint16_t port_;
    Mode mode_;
    MessageCallback on_message_;

    int server_fd_  = -1;
    int client_fd_  = -1;
    bool running_   = false;
};
