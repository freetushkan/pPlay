// Real pPlay lifecycle wrapper + real patched HTTP server; torrent core C API stubbed.
#include "p2play_embedded.h"
#include "utility.h"
#include "http.hpp"
#include "../third_party/P2Play/src/core/p2play.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
static p2p::HttpServer server;
static bool delayed = false;
extern "C" int p2play_start(const p2play_config *) {
    if (delayed) std::this_thread::sleep_for(std::chrono::milliseconds(120));
    return server.start(0, [](const p2p::HttpRequest &, p2p::HttpResponse &r) { r.text(200, "ok"); }) ? 0 : -1;
}
extern "C" int p2play_http_port() { return 8090; }
extern "C" int p2play_active_requests() { return server.activeConnections(); }
extern "C" int p2play_active_streams() { return 0; }
extern "C" void p2play_stop_accepting() { server.stopAccepting(); }
extern "C" void p2play_stop() { server.stop(); }
void pplay::Utility::log(LogLevel, const std::string &) {}
void pplay::Utility::shutdownStage(const char *, bool) {}
int main(int argc, char **) {
    delayed = argc > 1;
    pplay::P2PlayEmbedded::activate("/tmp/test", "test-password");
    int fd = -1;
    if (!delayed) {
        for (int i = 0; i < 200 && !pplay::P2PlayEmbedded::isReady(); ++i) {
            pplay::P2PlayEmbedded::update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(pplay::P2PlayEmbedded::isReady());
        fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(server.port());
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        assert(connect(fd, (sockaddr *)&addr, sizeof(addr)) == 0);
        for (int i = 0; i < 200 && !server.activeConnections(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(server.activeConnections());
    }
    pplay::P2PlayEmbedded::shutdown();
    pplay::P2PlayEmbedded::shutdown();
    assert(!pplay::P2PlayEmbedded::isReady());
    assert(!pplay::P2PlayEmbedded::activate("/tmp/test", "test-password"));
    if (fd >= 0) close(fd);
    std::cout << "PASS embedded shutdown " << (delayed ? "during startup" : "with incomplete HTTP client") << '\n';
}
