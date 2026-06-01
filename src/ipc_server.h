#pragma once

#include <cstdint>
#include <functional>
#include <string>

/// Lightweight TCP IPC server for desktop testing only.
/// Not part of the modem library — linked only into modem_app.
///
/// Wire format: newline-delimited text (\n). Compatible with nc / telnet.
/// Each received line (stripped of \r\n) is delivered to the callback.
/// Replies are sent as a line terminated with \n.
///
/// On Zephyr this is replaced by a dedicated thread that calls
/// network.tx_write() directly from whatever data source is available.

class IpcServer {
public:
    /// Called from the background thread for each complete received message.
    using MessageCallback = std::function<void(const uint8_t* data, uint16_t length)>;

    explicit IpcServer(uint16_t port, MessageCallback on_message);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    /// Replace the message callback (must be called before start(), or while stopped).
    void set_callback(MessageCallback on_message) { on_message_ = std::move(on_message); }

    /// Start the server (non-blocking — spawns a background accept thread).
    bool start();

    /// Stop the server and close all sockets.
    void stop();

    /// Send a reply line to the currently connected client.
    /// Appends \n automatically. Returns false if no client is connected.
    bool send(const uint8_t* data, uint16_t length);

    bool is_running() const { return running_; }

private:
    void accept_loop();
    void client_loop(int client_fd);

    uint16_t port_;
    MessageCallback on_message_;

    int server_fd_  = -1;
    int client_fd_  = -1;
    bool running_   = false;
};
