// Compile with the pinned (patched) core's src/core/http.cpp, no torrent deps.
#include "http.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

static void run(int scenario) {
    p2p::HttpServer server;
    assert(server.start(0, [](const p2p::HttpRequest &, p2p::HttpResponse &response) {
        response.text(200, "ok\n");
    }));
    int fd = -1;
    if (scenario != 0) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(server.port());
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        assert(connect(fd, (sockaddr *) &addr, sizeof(addr)) == 0);
        const char *data = scenario == 1 ? "GET / HTTP/1.1\r\nHost: localhost\r\n"
                                       : "POST / HTTP/1.1\r\nContent-Length: 999\r\n\r\nx";
        assert(send(fd, data, strlen(data), 0) == (ssize_t) strlen(data));
        for (int i = 0; i < 100 && server.activeConnections() == 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(server.activeConnections() == 1);
    }
    auto start = std::chrono::steady_clock::now();
    server.stop();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(elapsed < std::chrono::seconds(2));
    assert(server.activeConnections() == 0);
    server.stop(); // idempotent
    if (fd >= 0) close(fd);
    std::cout << "PASS HTTP shutdown scenario " << scenario << std::endl;
}
int main(int argc, char **argv) {
    run(argc > 1 ? atoi(argv[1]) : 0);
}
