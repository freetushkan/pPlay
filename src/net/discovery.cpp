//
// Servers of the local network (see discovery.h). The subnet: this address /24 (the usual home
// one), its hosts tried a batch at a time (a TCP connection made or refused, not waited for).
//

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

// as ftplib (all the platforms)
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include <json.hpp>
#include "discovery.h"
#include "dlna.h"
#include "net_http.h"
#include "utility.h"

using namespace pplay;

#define BATCH 24
#define CONNECT_MS 400

namespace {

    std::mutex g_mutex;
    std::vector<Discovery::Server> g_found;
    std::atomic<bool> g_searching{false};
    Io::NetworkType g_type = Io::NetworkType::Auto;
    std::thread *g_thread = nullptr;

    sockaddr_in addressOf(uint32_t ip, int port) {
        sockaddr_in addr {};
#if defined(__PS4__) || defined(__PS5__)
        addr.sin_len = sizeof(addr);
#endif
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t) port);
        addr.sin_addr.s_addr = htonl(ip);
        return addr;
    }

    std::string text(uint32_t ip) {
        return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255)
               + "." + std::to_string(ip & 255);
    }

    // this device address on the local network (the route out: nothing sent), 0 if none
    uint32_t localAddress() {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            return 0;
        }
        sockaddr_in to = addressOf(0x08080808, 53), self {};
        socklen_t length = sizeof(self);
        uint32_t ip = 0;
        if (connect(sock, (sockaddr *) &to, sizeof(to)) == 0 && getsockname(sock, (sockaddr *) &self, &length) == 0) {
            ip = ntohl(self.sin_addr.s_addr);
        }
        close(sock);
        return ip == 0 || (ip >> 24) == 127 ? 0 : ip;
    }

    // the hosts of the /24 accepting a TCP connection on the port (this one too)
    std::vector<uint32_t> scan(int port) {
        std::vector<uint32_t> open;
        uint32_t self = localAddress();
        if (self == 0) {
            Utility::log(Utility::LogLevel::Error, "Discovery: no local address");
            return open;
        }
        uint32_t net = self & 0xFFFFFF00;
        for (uint32_t first = 1; first < 255; first += BATCH) {
            std::map<int, uint32_t> pending;
            for (uint32_t host = first; host < first + BATCH && host < 255; host++) {
                int sock = socket(AF_INET, SOCK_STREAM, 0);
                if (sock < 0) {
                    break;
                }
                fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
                sockaddr_in addr = addressOf(net | host, port);
                if (connect(sock, (sockaddr *) &addr, sizeof(addr)) == 0) {
                    open.push_back(net | host);
                    close(sock);
                    continue;
                }
                pending[sock] = net | host;
            }
            c2d::C2DClock clock;
            while (!pending.empty() && clock.getElapsedTime().asMilliseconds() < CONNECT_MS) {
                fd_set set;
                FD_ZERO(&set);
                int top = 0;
                for (auto &p: pending) {
                    FD_SET(p.first, &set);
                    top = std::max(top, p.first);
                }
                struct timeval tv {0, 50 * 1000};
                if (select(top + 1, nullptr, &set, nullptr, &tv) <= 0) {
                    continue;
                }
                for (auto it = pending.begin(); it != pending.end();) {
                    if (!FD_ISSET(it->first, &set)) {
                        ++it;
                        continue;
                    }
                    int error = 0;
                    socklen_t length = sizeof(error);
                    getsockopt(it->first, SOL_SOCKET, SO_ERROR, (char *) &error, &length);
                    if (error == 0) {
                        open.push_back(it->second);
                    }
                    close(it->first);
                    it = pending.erase(it);
                }
            }
            for (auto &p: pending) {
                close(p.first);
            }
        }
        Utility::log(Utility::LogLevel::Info, "Discovery: " + text(net) + "/24 port " + std::to_string(port) + ", "
                                                      + std::to_string(open.size()) + " open");
        return open;
    }

    // P2Play exposes a native public identity endpoint distinct from the Silo
    // compatibility ping. Probe it first so an OFF-MatriX P2Play node is not
    // mistaken for a TorrServer Silo instance.
    bool isP2PlayNode(const std::string &url, std::string *version = nullptr) {
        Http::Response response = Http::get(url + "api/p2play/version", 3);
        nlohmann::json answer = nlohmann::json::parse(response.body, nullptr, false);
        if (!response.ok() || !answer.is_object() || answer.value("name", std::string()) != "P2Play") {
            return false;
        }
        if (version) *version = answer.value("version", std::string());
        return true;
    }

    std::vector<Discovery::Server> p2playServers() {
        std::vector<Discovery::Server> servers;
        for (uint32_t ip: scan(8090)) {
            const std::string url = "http://" + text(ip) + ":8090/";
            std::string version;
            if (isP2PlayNode(url, &version)) {
                std::string label = "P2Play";
                if (!version.empty()) label += " " + version;
                label += " (" + text(ip) + ")";
                servers.push_back({"P2Play", label, url});
            }
        }
        return servers;
    }

    // One visible TorrServer type: MatriX is identified by /echo, Silo by /api/system/ping.
    // Native P2Play is excluded first by its separate /api/p2play/version identity.
    std::vector<Discovery::Server> torrServers() {
        std::vector<Discovery::Server> servers;
        for (uint32_t ip: scan(8090)) {
            std::string url = "http://" + text(ip) + ":8090/";
            if (isP2PlayNode(url)) continue;

            Http::Response matrix = Http::get(url + "echo", 3);
            std::string version = matrix.body.substr(0, 40);
            if (matrix.ok() && !version.empty() &&
                version.find('<') == std::string::npos && version.find('{') == std::string::npos) {
                servers.push_back({"TorrServer", "TorrServer MatriX " + version + " (" + text(ip) + ")", url});
                continue;
            }

            Http::Response silo = Http::get(url + "api/system/ping", 3);
            nlohmann::json answer = nlohmann::json::parse(silo.body, nullptr, false);
            if (silo.ok() && answer.is_object() && answer.value("status", std::string()) == "ok") {
                servers.push_back({"TorrServer", "TorrServer Silo (" + text(ip) + ")", url});
            }
        }
        return servers;
    }

    // a NetBIOS node status query (the name "*")
    std::string statusQuery(uint16_t id) {
        std::string q;
        q += (char) (id >> 8);
        q += (char) (id & 255);
        q += std::string("\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10);
        q += (char) 32;
        std::string name = std::string("*") + std::string(15, '\0');
        for (unsigned char c: name) {
            q += (char) ('A' + (c >> 4));
            q += (char) ('A' + (c & 15));
        }
        q += std::string("\x00\x00\x21\x00\x01", 5);
        return q;
    }

    // the computer name of a node status answer: a unique file server (0x20), else workstation one
    std::string statusName(const unsigned char *data, size_t size) {
        // header, the name (34), type, class, ttl, length
        size_t at = 12 + 34 + 2 + 2 + 4 + 2;
        if (size <= at) {
            return "";
        }
        int count = data[at++];
        std::string workstation;
        for (int i = 0; i < count && at + 18 <= size; i++, at += 18) {
            bool group = (data[at + 16] & 0x80) != 0;
            unsigned char suffix = data[at + 15];
            std::string name((const char *) data + at, 15);
            name = name.substr(0, name.find_last_not_of(' ') + 1);
            if (group || name.empty()) {
                continue;
            }
            if (suffix == 0x20) {
                return name;
            }
            if (suffix == 0x00 && workstation.empty()) {
                workstation = name;
            }
        }
        return workstation;
    }

    // SMB hosts, named by NetBIOS when they answer
    std::vector<Discovery::Server> smbServers() {
        std::vector<Discovery::Server> servers;
        std::vector<uint32_t> hosts = scan(445);
        std::map<uint32_t, std::string> names;
        int sock = hosts.empty() ? -1 : socket(AF_INET, SOCK_DGRAM, 0);
        if (sock >= 0) {
            for (size_t i = 0; i < hosts.size(); i++) {
                std::string query = statusQuery((uint16_t) (0x5050 + i));
                sockaddr_in to = addressOf(hosts[i], 137);
                sendto(sock, query.data(), query.size(), 0, (sockaddr *) &to, sizeof(to));
            }
            c2d::C2DClock clock;
            while (names.size() < hosts.size() && clock.getElapsedTime().asMilliseconds() < 1500) {
                fd_set set;
                FD_ZERO(&set);
                FD_SET(sock, &set);
                struct timeval tv {0, 100 * 1000};
                if (select(sock + 1, &set, nullptr, nullptr, &tv) <= 0) {
                    continue;
                }
                unsigned char buffer[1024];
                sockaddr_in from {};
                socklen_t length = sizeof(from);
                ssize_t n = recvfrom(sock, (char *) buffer, sizeof(buffer), 0, (sockaddr *) &from, &length);
                if (n > 0) {
                    names[ntohl(from.sin_addr.s_addr)] = statusName(buffer, (size_t) n);
                }
            }
            close(sock);
        }
        for (uint32_t ip: hosts) {
            std::string name = names[ip];
            servers.push_back({name.empty() ? text(ip) : name, name.empty() ? text(ip) : name + " (" + text(ip) + ")",
                               "smb://" + text(ip) + "/"});
        }
        return servers;
    }
}

namespace pplay::Discovery {

    bool supports(Io::NetworkType type) {
#ifdef __SMB2__
        if (type == Io::NetworkType::Smb) {
            return true;
        }
#endif
        return type == Io::NetworkType::Dlna || type == Io::NetworkType::TorrServe ||
               type == Io::NetworkType::P2Play;
    }

    void start(Io::NetworkType type) {
        if (g_searching) {
            return;
        }
        if (g_thread != nullptr) {
            g_thread->join();
            delete g_thread;
        }
        g_searching = true;
        g_type = type;
        g_thread = new std::thread([type] {
            std::vector<Server> servers;
            if (type == Io::NetworkType::Dlna) {
                for (auto &s: Dlna::search(4)) {
                    servers.push_back({s.name, s.name, s.location});
                }
            } else if (type == Io::NetworkType::TorrServe) {
                servers = torrServers();
            } else if (type == Io::NetworkType::P2Play) {
                servers = p2playServers();
            } else if (type == Io::NetworkType::Smb) {
                servers = smbServers();
            }
            std::lock_guard<std::mutex> lock(g_mutex);
            g_found = servers;
            g_searching = false;
        });
    }

    bool result(Io::NetworkType type, std::vector<Server> *servers) {
        if (g_searching) {
            return false;
        }
        if (type != g_type) {
            start(type);
            return false;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        *servers = g_found;
        return true;
    }
}
