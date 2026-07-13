#include "ipc_server.h"

#ifndef _WIN32

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <thread>
#include <cstring>

IpcServer::IpcServer(uint16_t port, MessageCallback on_message, Mode mode)
    : port_(port), mode_(mode), on_message_(std::move(on_message)) {}

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start() {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return false;

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(srv, 1) != 0) {
        close(srv);
        return false;
    }

    server_fd_ = srv;
    running_ = true;
    std::thread(&IpcServer::accept_loop, this).detach();
    return true;
}

void IpcServer::stop() {
    running_ = false;
    if (client_fd_ != -1) {
        close(client_fd_);
        client_fd_ = -1;
    }
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
}

bool IpcServer::send(const uint8_t* data, uint16_t length) {
    if (client_fd_ == -1) return false;
    if (mode_ == Mode::framed) {
        uint8_t hdr[2] = {static_cast<uint8_t>(length & 0xFF), static_cast<uint8_t>((length >> 8) & 0xFF)};
        if (::send(client_fd_, hdr, 2, 0) != 2) return false;
        return ::send(client_fd_, data, length, 0) == static_cast<ssize_t>(length);
    }
    // Mode::line
    if (::send(client_fd_, data, length, 0) != static_cast<ssize_t>(length)) return false;
    return ::send(client_fd_, "\n", 1, 0) == 1;
}

void IpcServer::accept_loop() {
    while (running_) {
        int client = accept(server_fd_, nullptr, nullptr);
        if (client < 0) break;
        client_fd_ = client;
        if (on_connect_) on_connect_();
        client_loop(client_fd_);
        if (on_disconnect_) on_disconnect_();
        close(client);
        client_fd_ = -1;
    }
}

void IpcServer::client_loop(int fd) {
    if (mode_ == Mode::framed)
        client_loop_framed(fd);
    else
        client_loop_line(fd);
}

void IpcServer::client_loop_line(int fd) {
    std::string line;
    char ch;
    while (running_) {
        ssize_t r = recv(fd, &ch, 1, 0);
        if (r <= 0) break;
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && on_message_) {
                on_message_(reinterpret_cast<const uint8_t*>(line.data()), static_cast<uint16_t>(line.size()));
            }
            line.clear();
        } else {
            line += ch;
        }
    }
}

void IpcServer::client_loop_framed(int fd) {
    while (running_) {
        uint8_t hdr[2];
        ssize_t r = recv(fd, hdr, 2, MSG_WAITALL);
        if (r != 2) break;
        uint16_t len = static_cast<uint16_t>(hdr[0]) | (static_cast<uint16_t>(hdr[1]) << 8);
        if (len == 0) continue;

        uint8_t buf[4096];
        size_t recv_len = (len <= sizeof(buf)) ? len : sizeof(buf);
        ssize_t total = 0;
        while (total < static_cast<ssize_t>(recv_len)) {
            ssize_t n = recv(fd, buf + total, recv_len - total, 0);
            if (n <= 0) return;
            total += n;
        }
        if (on_message_) on_message_(buf, recv_len);
    }
}

#endif // !_WIN32
