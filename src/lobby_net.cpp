#include "lobby_net.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket InvalidSocket = INVALID_SOCKET;

bool ensureNetworking() {
    static const bool ok = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}
void closeSocket(NativeSocket s) { closesocket(s); }
bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
bool interrupted() { return false; }
void setNonBlocking(NativeSocket s) {
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
}
void setSendTimeout(NativeSocket s, int seconds) {
    const DWORD ms = static_cast<DWORD>(seconds * 1000);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&ms), sizeof(ms));
}
#else
using NativeSocket = int;
constexpr NativeSocket InvalidSocket = -1;

bool ensureNetworking() { return true; }
void closeSocket(NativeSocket s) { ::close(s); }
bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
bool interrupted() { return errno == EINTR; }
void setNonBlocking(NativeSocket s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
void setSendTimeout(NativeSocket s, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}
#endif

} // namespace

LobbyConnection::~LobbyConnection() {
    close();
}

bool LobbyConnection::open(const std::string &address, std::string &error) {
    close();
    if (!ensureNetworking()) {
        error = "Network initialisation failed";
        return false;
    }
    const size_t colon = address.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= address.size()) {
        error = "Address must be host:port";
        return false;
    }
    const std::string host = address.substr(0, colon);
    const std::string port = address.substr(colon + 1);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        error = "Could not resolve host";
        return false;
    }

    NativeSocket sock = InvalidSocket;
    for (addrinfo *ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == InvalidSocket) {
            continue;
        }
        // Bounded connect so the menu cannot hang for long on a dead address.
        setSendTimeout(sock, 4);
        if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        closeSocket(sock);
        sock = InvalidSocket;
    }
    freeaddrinfo(res);

    if (sock == InvalidSocket) {
        error = "Could not connect to server";
        return false;
    }

    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof(one));
    setNonBlocking(sock);
    fd_ = static_cast<std::intptr_t>(sock);
    buffer_.clear();
    return true;
}

void LobbyConnection::close() {
    if (fd_ >= 0) {
        closeSocket(static_cast<NativeSocket>(fd_));
        fd_ = -1;
    }
    buffer_.clear();
}

void LobbyConnection::sendLine(const std::string &line) {
    if (fd_ < 0) {
        return;
    }
    const std::string data = line + "\n";
    const NativeSocket sock = static_cast<NativeSocket>(fd_);
    size_t sent = 0;
    while (sent < data.size()) {
        const auto n = send(sock, data.data() + sent, static_cast<int>(data.size() - sent), MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
        } else if (n < 0 && interrupted()) {
            continue;
        } else {
            // Socket is non-blocking; a short write here is rare for small lines, so drop the link.
            close();
            return;
        }
    }
}

std::vector<std::string> LobbyConnection::poll() {
    std::vector<std::string> lines;
    if (fd_ < 0) {
        return lines;
    }
    const NativeSocket sock = static_cast<NativeSocket>(fd_);
    bool remoteClosed = false;
    bool failed = false;
    char chunk[1024];
    for (;;) {
        const auto n = recv(sock, chunk, sizeof(chunk), 0);
        if (n > 0) {
            buffer_.append(chunk, static_cast<size_t>(n));
        } else if (n == 0) {
            remoteClosed = true; // server closed the connection; keep lines already received
            break;
        } else if (interrupted()) {
            continue;
        } else {
            failed = !wouldBlock();
            break;
        }
    }
    size_t start = 0;
    for (size_t nl = buffer_.find('\n'); nl != std::string::npos; nl = buffer_.find('\n', start)) {
        std::string line = buffer_.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
        start = nl + 1;
    }
    buffer_.erase(0, start);
    if (remoteClosed || failed) {
        close();
    }
    return lines;
}
