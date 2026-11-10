#pragma once

#include <cstdint>
#include <string>
#include <vector>

inline constexpr const char *GameVersion = "1.2.5";

// One entry from the server's LIST reply.
struct LobbyInfo {
    int id = 0;
    std::string name;
    std::string mode;
    int players = 0;
};

// Line-based TCP connection to the lobby server (see server/src/main.cpp).
class LobbyConnection {
public:
    LobbyConnection() = default;
    ~LobbyConnection();
    LobbyConnection(const LobbyConnection &) = delete;
    LobbyConnection &operator=(const LobbyConnection &) = delete;

    // Accepts "host:port" (host may be a name such as 0.tcp.ngrok.io). Blocks up to a few seconds.
    bool open(const std::string &address, std::string &error);
    void close();
    bool isOpen() const { return fd_ >= 0; }
    void sendLine(const std::string &line);
    // Non-blocking: returns any complete lines received since the last call.
    std::vector<std::string> poll();

private:
    std::intptr_t fd_ = -1;
    std::string buffer_;
};
