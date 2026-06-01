#include "ipc_server.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include <thread>
#include <cstring>
#include <cstdio>

// On Windows map SOCKET to our int-typed fd field via a small adapter
// (we store SOCKET as intptr_t-cast to fit in int on LP64/LLP64)
static_assert(sizeof(SOCKET) <= sizeof(int) * 2,
              "SOCKET wider than expected — adjust ipc_server internals");

// helpers to hide the cast noise
static SOCKET to_sock(int fd)  { return static_cast<SOCKET>(fd); }
static int    to_fd(SOCKET s)  { return static_cast<int>(s);      }

IpcServer::IpcServer(uint16_t port, MessageCallback on_message)
    : port_(port), on_message_(std::move(on_message)) {}

IpcServer::~IpcServer() { stop(); }

bool IpcServer::start() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) { WSACleanup(); return false; }

    // Allow quick rebind after restart
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port_);

    if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(srv, 1) != 0) {
        closesocket(srv); WSACleanup(); return false;
    }

    server_fd_ = to_fd(srv);
    running_   = true;
    std::thread(&IpcServer::accept_loop, this).detach();
    return true;
}

void IpcServer::stop() {
    running_ = false;
    if (client_fd_ != -1) { closesocket(to_sock(client_fd_)); client_fd_ = -1; }
    if (server_fd_ != -1) { closesocket(to_sock(server_fd_)); server_fd_ = -1; }
    WSACleanup();
}

bool IpcServer::send(const uint8_t* data, uint16_t length) {
    if (client_fd_ == -1) return false;
    SOCKET c = to_sock(client_fd_);
    // Send payload then newline
    int sent = ::send(c, reinterpret_cast<const char*>(data), length, 0);
    if (sent != static_cast<int>(length)) return false;
    return ::send(c, "\n", 1, 0) == 1;
}

void IpcServer::accept_loop() {
    while (running_) {
        SOCKET client = accept(to_sock(server_fd_), nullptr, nullptr);
        if (client == INVALID_SOCKET) break;
        client_fd_ = to_fd(client);
        client_loop(client_fd_);
        closesocket(client); client_fd_ = -1;
    }
}

void IpcServer::client_loop(int fd) {
    SOCKET s = to_sock(fd);
    std::string line;
    char ch;
    while (running_) {
        int r = recv(s, &ch, 1, 0);
        if (r <= 0) break;
        if (ch == '\n') {
            // Strip trailing \r if present
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && on_message_) {
                on_message_(reinterpret_cast<const uint8_t*>(line.data()),
                            static_cast<uint16_t>(line.size()));
            }
            line.clear();
        } else {
            line += ch;
        }
    }
}

#endif // _WIN32
