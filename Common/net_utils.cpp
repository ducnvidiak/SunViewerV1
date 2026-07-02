#include "net_utils.h"
#include <iostream>

namespace common {

    bool initWinsock() {
        WSADATA wsa{};
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }

    void cleanupWinsock() {
        WSACleanup();
    }

    bool sendAll(SOCKET s, const void* data, size_t size) {
        const char* buf = (const char*)data;
        size_t sent = 0;

        while (sent < size) {
            int n = send(s, buf + sent, (int)(size - sent), 0);
            if (n <= 0) return false;
            sent += n;
        }
        return true;
    }

    bool recvAll(SOCKET s, void* data, size_t size) {
        char* buf = (char*)data;
        size_t rec = 0;

        while (rec < size) {
            int n = recv(s, buf + rec, (int)(size - rec), 0);
            if (n <= 0) return false;
            rec += n;
        }
        return true;
    }

    SOCKET createServer(uint16_t port) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        bind(s, (sockaddr*)&addr, sizeof(addr));
        listen(s, 1);

        return s;
    }

    SOCKET acceptClient(SOCKET server) {
        return accept(server, nullptr, nullptr);
    }

    SOCKET connectToServer(const std::string& ip, uint16_t port) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0)
            return INVALID_SOCKET;

        return s;
    }

    void closeSocket(SOCKET s) {
        if (s != INVALID_SOCKET)
            closesocket(s);
    }

}