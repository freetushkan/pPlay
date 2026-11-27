//
// TorrServe virtual filesystem helpers.
//

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <cstring>
#include <deque>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <json.hpp>
#include "main.h"
#include "io.h"
#include "net_http.h"
#include "torrserve.h"
#include "dlna.h"
#include "i18n.h"
#include "saved.h"
#include "utility.h"
#include "worker.h"

namespace {

    struct TorrentFile {
        int id = 0;
        std::string path;
        int64_t length = 0;
    };

    struct Torrent {
        std::string title;
        std::string hash;
        std::string poster;
        std::string category;
        int64_t size = 0;
        std::vector<TorrentFile> files;
    };

    bool isTorrServe(const std::string &path) {
        auto *io = pplay::Io::get();
        return io != nullptr && io->getDeviceType(path) == pplay::Io::DeviceType::TorrServe;
    }

    // The configured type is simply TorrServer. Detect its API dialect using public,
    // mutually exclusive probes: MatriX /echo first, then Silo /api/system/ping.
    std::string apiRoot(const std::string &path);
    std::mutex g_protocolMutex;
    std::unordered_map<std::string, bool> g_siloProtocol;

    std::string probeRoot(std::string root) {
        const size_t scheme = root.find("://");
        if (scheme == std::string::npos) return root;
        const size_t authorityEnd = root.find_first_of("/?#", scheme + 3);
        const size_t at = root.rfind('@', authorityEnd == std::string::npos ? std::string::npos : authorityEnd);
        if (at != std::string::npos && at >= scheme + 3) root.erase(scheme + 3, at + 1 - (scheme + 3));
        return root;
    }

    pplay::Http::Response publicProbe(const std::string &url) {
        pplay::Http::Request request;
        request.url = url;
        request.timeout = 3;
        request.useNetworkCredentials = false;
        request.headers = {"Accept: application/json, text/plain"};
        return pplay::Http::perform(request);
    }

    bool isSilo(const std::string &path) {
        const std::string root = probeRoot(apiRoot(path));
        if (root.empty()) return false;
        {
            std::lock_guard<std::mutex> lock(g_protocolMutex);
            auto found = g_siloProtocol.find(root);
            if (found != g_siloProtocol.end()) return found->second;
        }

        const auto matrix = publicProbe(root + "echo");
        const std::string echo = matrix.body;
        if (matrix.ok() && !echo.empty() &&
            echo.find('<') == std::string::npos && echo.find('{') == std::string::npos) {
            std::lock_guard<std::mutex> lock(g_protocolMutex);
            g_siloProtocol[root] = false;
            return false;
        }

        const auto silo = publicProbe(root + "api/system/ping");
        const auto json = nlohmann::json::parse(silo.body, nullptr, false);
        if (silo.ok() && json.is_object() && json.value("status", std::string()) == "ok") {
            std::lock_guard<std::mutex> lock(g_protocolMutex);
            g_siloProtocol[root] = true;
            return true;
        }
        // Do not cache failures: a server may still be starting or temporarily offline.
        return false;
    }

    // in the log and the errors
    std::string nameOf(const std::string &path) {
        return isSilo(path) ? "TorrServer.Silo" : "TorrServer.MatriX";
    }

    std::string replaceScheme(const std::string &path) {
        if (c2d::Utility::startWith(path, "tss://")) {
            return "https://" + path.substr(6);
        }
        if (c2d::Utility::startWith(path, "ts://")) {
            return "http://" + path.substr(5);
        }
        return path;
    }

    // TorrServer root: the configured network url (may have a sub path), with a trailing slash
    std::string rootOf(const std::string &path) {
        auto *io = pplay::Io::get();
        std::string root = io != nullptr ? io->getNetworkRoot(path) : std::string();
        if (!root.empty()) {
            // keep the path spelling (user info, case...)
            if (path.size() >= root.size()) {
                std::string r = path.substr(0, root.size());
                if (c2d::Utility::endsWith(r, "/")) {
                    return r;
                }
            }
            return root;
        }
        size_t scheme = path.find("://");
        if (scheme == std::string::npos) {
            return path;
        }
        size_t slash = path.find('/', scheme + 3);
        if (slash == std::string::npos) {
            return path + "/";
        }
        return path.substr(0, slash + 1);
    }

    std::string relativeOf(const std::string &path) {
        std::string root = rootOf(path);
        if (path.size() <= root.size()) {
            return {};
        }
        std::string relative = path.substr(root.size());
        size_t query = relative.find('?');
        if (query != std::string::npos) {
            relative.erase(query);
        }
        return relative;
    }

    std::string apiRoot(const std::string &path) {
        return replaceScheme(rootOf(path));
    }

    std::string escapeSegment(const std::string &value) {
        return pplay::Http::escape(value);
    }

    std::string unescapeSegment(const std::string &value) {
        std::string out;
        for (size_t i = 0; i < value.size(); i++) {
            if (value[i] == '%' && i + 2 < value.size()) {
                int hi = pplay::Utility::hexValue(value[i + 1]);
                int lo = pplay::Utility::hexValue(value[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    out += (char) ((hi << 4) | lo);
                    i += 2;
                    continue;
                }
            }
            out += value[i];
        }
        return out;
    }

    std::vector<std::string> splitPath(const std::string &path) {
        std::vector<std::string> parts;
        std::stringstream ss(path);
        std::string part;
        while (std::getline(ss, part, '/')) {
            if (!part.empty()) {
                parts.push_back(unescapeSegment(part));
            }
        }
        return parts;
    }

    std::string httpRequest(const std::string &url, int timeout, const std::string &postBody = {}) {
        pplay::Http::Response response = pplay::Http::postJson(url, postBody, timeout);
        if (!response.ok()) {
            pplay::Utility::log(
                    pplay::Utility::LogLevel::Error,
                    nameOf(url) + ": request " + url + " failed: "
                            + (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error));
            return {};
        }
        return response.body;
    }

    // ok: answered (an empty list too)
    // the torrents list and the viewed marks asked again after (s, the setting)
    int syncSecs(const std::string &root) {
        return pplay::Io::get() != nullptr ? std::max(10, pplay::Io::get()->networkSync(root)) : 30;
    }

    void storeTorrentsList(const std::string &root, const std::string &response);

    // hash -> viewed file index -> position (seconds, 0 if none)
    typedef std::map<std::string, std::map<int, double>> ViewedByHash;

    void siloProgressSeen(const std::string &root);

    // progress: set when a TS Silo list carries the files positions
    bool parseTorrents(const std::string &response, std::vector<Torrent> *out, bool silo,
                       ViewedByHash *viewed = nullptr, bool *progress = nullptr);

    void applyViewed(const std::string &root, const std::vector<Torrent> &torrents, const ViewedByHash &viewed);

    // TS Silo tokens in use: root (its user in it) -> the settings it came from and the token
    struct SiloToken {
        std::string login, token;
    };
    std::mutex g_tokenMutex;
    std::unordered_map<std::string, SiloToken> g_tokens;
    // the ones got with a user and password: written in the settings (TorrServe::takeTokens)
    std::vector<pplay::TorrServe::Token> g_newTokens;

    // the token of a server: the kept one, else got with the user and password ("owner" without a
    // user); none: none needed. force: the kept one refused, a new one got with them if set
    bool siloLogin(const std::string &root, int timeout, bool force, long *status) {
        auto *io = pplay::Io::get();
        std::string user, password;
        if (io == nullptr || !io->loginOf(root, &user, &password)) {
            return true;
        }
        const std::string kept = io->siloToken(root);
        const std::string login = user + "\n" + password + "\n" + kept;
        {
            std::lock_guard<std::mutex> lock(g_tokenMutex);
            auto known = g_tokens.find(root);
            if (!force && known != g_tokens.end() && known->second.login == login) {
                return true;
            }
            g_tokens.erase(root);
        }
        if (!force && !kept.empty()) {
            std::lock_guard<std::mutex> lock(g_tokenMutex);
            g_tokens[root] = {login, kept};
            return true;
        }
        if (password.empty() && user.empty()) {
            // refused, nothing to get another one with
            *status = force ? 401 : 0;
            return !force;
        }
        nlohmann::json request = {{"username", user.empty() ? "owner" : user}, {"password", password}};
        pplay::Http::Response answer = pplay::Http::postJson(root + "api/auth/login", request.dump(), timeout);
        nlohmann::json json = nlohmann::json::parse(answer.body, nullptr, false);
        if (!answer.ok() || json.is_discarded() || !json.contains("token") || !json["token"].is_string()
            || json["token"].get<std::string>().empty()) {
            pplay::Utility::log(pplay::Utility::LogLevel::Error,
                                "TorrServer.Silo: login failed: "
                                        + (answer.error.empty() ? "HTTP " + std::to_string(answer.status) : answer.error));
            *status = answer.error.empty() && answer.status != 0 ? 401 : 0;
            return false;
        }
        std::string token = json["token"].get<std::string>();
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "TorrServer.Silo: token got for " + root);
        std::lock_guard<std::mutex> lock(g_tokenMutex);
        // (the settings changed with it: known by its token)
        g_tokens[root] = {user + "\n" + password + "\n" + token, token};
        g_newTokens.push_back({root, user, password, token});
        return true;
    }

    pplay::Http::Response siloTorrents(const std::string &root, int timeout) {
        long status = 0;
        pplay::Http::Response answer;
        if (!siloLogin(root, timeout, false, &status)) {
            answer.status = status;
            answer.error = status == 0 ? "login failed" : "";
            return answer;
        }
        answer = pplay::Http::get(root + "api/torrents", timeout);
        // the token refused (regenerated, typed wrong): a new one with the user and password
        if (answer.status == 401) {
            if (!siloLogin(root, timeout, true, &status)) {
                answer.status = status != 0 ? status : 401;
                return answer;
            }
            answer = pplay::Http::get(root + "api/torrents", timeout);
        }
        return answer;
    }

    std::vector<Torrent> getTorrents(const std::string &root, int timeout, bool *ok = nullptr, long *status = nullptr) {
        std::vector<Torrent> torrents;
        if (ok != nullptr) {
            *ok = false;
        }
        bool silo = isSilo(root);
        std::string url = root + (silo ? "api/torrents" : "torrents");
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
                            nameOf(root) + ": torrents url=" + url + ", timeout=" + std::to_string(timeout));
        pplay::Http::Response answer = silo ? siloTorrents(root, timeout)
                                            : pplay::Http::postJson(url, "{\"action\":\"list\"}", timeout);
        if (status != nullptr) {
            *status = answer.status;
        }
        if (!answer.ok()) {
            pplay::Utility::log(pplay::Utility::LogLevel::Error,
                                nameOf(root) + ": request " + url + " failed: "
                                        + (answer.error.empty() ? "HTTP " + std::to_string(answer.status) : answer.error));
            return torrents;
        }
        const std::string &response = answer.body;

        pplay::Utility::log(pplay::Utility::LogLevel::Trace, nameOf(root) + ": torrents response=" + response);
        // Silo: the viewed files in the same answer
        ViewedByHash viewed;
        bool progress = false;
        if (!parseTorrents(response, &torrents, silo, silo ? &viewed : nullptr, &progress)) {
            return torrents;
        }
        if (progress) {
            siloProgressSeen(root);
        }
        if (ok != nullptr) {
            *ok = true;
        }
        if (silo) {
            applyViewed(root, torrents, viewed);
        }
        storeTorrentsList(root, response);
        return torrents;
    }

    // the last list of a server kept on disk: its listings offline (all the torrents, entered or not)
    std::string torrentsListPath(const std::string &root) {
        char name[32];
        snprintf(name, sizeof(name), "%016llx", (unsigned long long) std::hash<std::string>{}(root));
        return pplay::Utility::getDataPath() + "cache/torrserver_list_" + name;
    }

    void storeTorrentsList(const std::string &root, const std::string &response) {
        std::string path = torrentsListPath(root), old;
        if (pplay::Io::get()->readText(path, old) && old == response) {
            return;
        }
        pplay::Io::get()->create(pplay::Utility::getDataPath() + "cache");
        pplay::Io::get()->writeText(path, response);
    }

    // a TS Silo viewed file: its index, or an object with its position too
    bool siloViewed(const nlohmann::json &item, int *index, double *seconds) {
        *seconds = 0;
        if (item.is_number_integer()) {
            *index = item.get<int>();
            return true;
        }
        if (!item.is_object()) {
            return false;
        }
        for (const char *key: {"id", "file_index", "index"}) {
            if (item.contains(key) && item[key].is_number_integer()) {
                *index = item[key].get<int>();
                for (const char *time: {"timecode", "position"}) {
                    if (item.contains(time) && item[time].is_number()) {
                        *seconds = item[time].get<double>();
                    }
                }
                return true;
            }
        }
        return false;
    }

    // TorrServer: an array, its files in "data"; TS Silo: {"torrents": [...]}, "file_stats" and
    // "viewed_files"
    bool parseTorrents(const std::string &response, std::vector<Torrent> *out, bool silo,
                       ViewedByHash *viewed, bool *progress) {
        std::vector<Torrent> &torrents = *out;
        nlohmann::json json = nlohmann::json::parse(response, nullptr, false);
        // Use only the response format belonging to this network's configured API.
        if (silo) {
            if (json.is_discarded() || !json.is_object() || !json.contains("torrents")) {
                return false;
            }
            nlohmann::json list = json["torrents"];
            // none: null
            json = list.is_array() ? list : nlohmann::json::array();
        } else if (json.is_discarded() || !json.is_array()) {
            return false;
        }

        for (const auto &item: json) {
            if (!item.contains("hash") || !item["hash"].is_string()) {
                continue;
            }
            std::string title = item.contains("title") && item["title"].is_string() ? item["title"].get<std::string>() : "";
            if (title.empty() && item.contains("name") && item["name"].is_string()) {
                title = item["name"].get<std::string>();
            }
            if (title.empty()) {
                continue;
            }

            Torrent torrent;
            torrent.title = title;
            torrent.hash = item["hash"].get<std::string>();
            if (item.contains("poster") && item["poster"].is_string()) {
                torrent.poster = item["poster"].get<std::string>();
            }
            if (item.contains("category") && item["category"].is_string()) {
                torrent.category = item["category"].get<std::string>();
            }
            if (item.contains("torrent_size") && item["torrent_size"].is_number()) {
                torrent.size = item["torrent_size"].get<int64_t>();
            }
            pplay::Utility::log(pplay::Utility::LogLevel::Trace,
                                "TorrServer: torrent " + torrent.title + ", hash=" + torrent.hash);

            if (!silo && item.contains("data") && item["data"].is_string()) {
                nlohmann::json data = nlohmann::json::parse(item["data"].get<std::string>(), nullptr, false);
                if (!data.is_discarded() && data.contains("TorrServer") && data["TorrServer"].contains("Files")
                    && data["TorrServer"]["Files"].is_array()) {
                    for (const auto &file: data["TorrServer"]["Files"]) {
                        if (!file.contains("id") || !file.contains("path") || !file["path"].is_string()) {
                            continue;
                        }
                        TorrentFile tf;
                        tf.id = file["id"].get<int>();
                        tf.path = file["path"].get<std::string>();
                        if (file.contains("length") && file["length"].is_number()) {
                            tf.length = file["length"].get<int64_t>();
                        }
                        torrent.files.push_back(tf);
                    }
                }
            }
            if (silo && item.contains("file_stats") && item["file_stats"].is_array()) {
                for (const auto &file: item["file_stats"]) {
                    if (!file.contains("id") || !file["id"].is_number_integer() || !file.contains("path")
                        || !file["path"].is_string()) {
                        continue;
                    }
                    TorrentFile tf;
                    tf.id = file["id"].get<int>();
                    tf.path = file["path"].get<std::string>();
                    if (file.contains("length") && file["length"].is_number()) {
                        tf.length = file["length"].get<int64_t>();
                    }
                    torrent.files.push_back(tf);
                }
            }
            if (viewed != nullptr && item.contains("viewed_files") && item["viewed_files"].is_array()) {
                auto &files = (*viewed)[torrent.hash];
                for (const auto &file: item["viewed_files"]) {
                    int index;
                    double seconds;
                    if (siloViewed(file, &index, &seconds)) {
                        files[index] = seconds;
                    }
                }
            }
            // a Silo keeping positions: {"<index>": {"position": s, "duration": s}}
            if (viewed != nullptr && item.contains("progress") && item["progress"].is_object()) {
                if (progress != nullptr) {
                    *progress = true;
                }
                auto &files = (*viewed)[torrent.hash];
                for (const auto &entry: item["progress"].items()) {
                    char *end = nullptr;
                    long index = strtol(entry.key().c_str(), &end, 10);
                    const auto &value = entry.value();
                    if (end == entry.key().c_str() || *end != 0 || !value.is_object() || !value.contains("position")
                        || !value["position"].is_number()) {
                        continue;
                    }
                    files[(int) index] = value["position"].get<double>();
                }
            }
            torrents.push_back(torrent);
        }
        return true;
    }

    // the torrents list per server: asked once in a sync interval (all the listings, the
    // folder marks, the details panel), a failure not asked again for a few seconds
    std::mutex g_cacheMutex;
    std::unordered_map<std::string, std::vector<Torrent>> g_torrentsCache;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_torrentsAt;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_torrentsFailedAt;
    // the servers not answering their last list request
    std::unordered_set<std::string> g_unreachable;
    // the servers refusing it (a user or password): their kept list not shown
    std::unordered_set<std::string> g_refused;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_viewedFailedAt;
    // one request at a time (the others wait for its answer)
    std::mutex g_fetchMutex;
    std::atomic<int> g_torrentsFetching{0};
    constexpr int FailedRetrySecs = 5;
    // bumped when a server's list changes (torrents added, removed, renamed)
    std::atomic<unsigned int> g_torrentsGeneration{0};

    bool sameTorrents(const std::vector<Torrent> &a, const std::vector<Torrent> &b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); i++) {
            if (a[i].hash != b[i].hash || a[i].title != b[i].title || a[i].category != b[i].category
                || a[i].files.size() != b[i].files.size()) {
                return false;
            }
        }
        return true;
    }

    bool cachedTorrents(const std::string &root, std::vector<Torrent> *torrents, bool *failed) {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto failure = g_torrentsFailedAt.find(root);
        // unreachable: asked again in a sync interval (its kept list used meanwhile)
        int retry = g_unreachable.count(root) > 0 ? syncSecs(root) : FailedRetrySecs;
        *failed = failure != g_torrentsFailedAt.end() && now - failure->second < std::chrono::seconds(retry);
        auto at = g_torrentsAt.find(root);
        if (at == g_torrentsAt.end() || now - at->second >= std::chrono::seconds(syncSecs(root))) {
            return false;
        }
        *torrents = g_torrentsCache[root];
        return true;
    }

    // the server not answering: its last list (this run's, else the one kept on disk)
    std::vector<Torrent> staleTorrents(const std::string &root) {
        {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            auto known = g_torrentsCache.find(root);
            if (known != g_torrentsCache.end()) {
                return known->second;
            }
        }
        std::vector<Torrent> torrents;
        std::string response;
        if (pplay::Io::get()->readText(torrentsListPath(root), response) && parseTorrents(response, &torrents, isSilo(root))) {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            g_torrentsCache[root] = torrents;
        }
        return torrents;
    }

    std::vector<Torrent> fetchTorrents(const std::string &root, int timeout, bool *ok);

    bool refused(const std::string &root) {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        return g_refused.count(root) > 0;
    }

    std::vector<Torrent> torrentsOf(const std::string &root, int timeout, bool *ok) {
        std::vector<Torrent> torrents = fetchTorrents(root, timeout, ok);
        if (!*ok && !refused(root)) {
            torrents = staleTorrents(root);
            *ok = !torrents.empty();
        }
        return torrents;
    }

    std::vector<Torrent> fetchTorrents(const std::string &root, int timeout, bool *ok) {
        std::vector<Torrent> torrents;
        bool failed = false;
        *ok = false;
        if (cachedTorrents(root, &torrents, &failed)) {
            *ok = true;
            return torrents;
        }
        if (failed) {
            return torrents;
        }
        const auto waitStarted = std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> fetch(g_fetchMutex);
        const auto lockWaitMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - waitStarted).count();
        // Count list synchronization separately from viewed-mark synchronization so the filer
        // can expose a single "TorrServer is syncing" state to diagnostics.
        g_torrentsFetching.fetch_add(1);
        struct FetchCounter {
            ~FetchCounter() { g_torrentsFetching.fetch_sub(1); }
        } fetchCounter;
        // asked by another thread meanwhile
        if (cachedTorrents(root, &torrents, &failed)) {
            *ok = true;
            return torrents;
        }
        if (failed) {
            return torrents;
        }
        long status = 0;
        const auto requestStarted = std::chrono::steady_clock::now();
        torrents = getTorrents(root, timeout, ok, &status);
        const auto requestMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - requestStarted).count();
        if (lockWaitMs >= 100 || requestMs >= 500) {
            pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                                "TorrServer sync: lock_wait_ms=" + std::to_string(lockWaitMs) +
                                " request_ms=" + std::to_string(requestMs) +
                                " status=" + std::to_string(status) +
                                " torrents=" + std::to_string(torrents.size()));
        }
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        // refused: there (not offline), its kept list not shown
        bool refusedNow = status == 401 || status == 403;
        if (refusedNow) {
            g_refused.insert(root);
        } else {
            g_refused.erase(root);
        }
        if (*ok) {
            auto known = g_torrentsCache.find(root);
            if (known != g_torrentsCache.end() && !sameTorrents(known->second, torrents)) {
                g_torrentsGeneration++;
            }
            g_torrentsCache[root] = torrents;
            g_torrentsAt[root] = std::chrono::steady_clock::now();
            g_torrentsFailedAt.erase(root);
            // back: listed again (the kept listing replaced)
            if (g_unreachable.erase(root) > 0) {
                g_torrentsGeneration++;
            }
        } else {
            g_torrentsFailedAt[root] = std::chrono::steady_clock::now();
            if (!refusedNow) {
                g_unreachable.insert(root);
            }
        }
        return torrents;
    }

    bool hasPrefix(const std::vector<std::string> &path, const std::vector<std::string> &prefix) {
        if (path.size() < prefix.size()) {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); i++) {
            if (path[i] != prefix[i]) {
                return false;
            }
        }
        return true;
    }

    std::string joinVirtual(const std::string &root, const std::vector<std::string> &parts) {
        std::string out = root;
        for (size_t i = 0; i < parts.size(); i++) {
            if (!c2d::Utility::endsWith(out, "/")) {
                out += "/";
            }
            out += escapeSegment(parts[i]);
        }
        return out;
    }

    std::string basename(const std::string &path) {
        size_t pos = path.find_last_of('/');
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    // "?link=<hash>&index=<n>" of a file path
    bool fileLink(const std::string &path, std::string &hash, int &index) {
        size_t query = path.find('?');
        if (query == std::string::npos) {
            return false;
        }
        std::string indexText;
        std::stringstream ss(path.substr(query + 1));
        std::string param;
        while (std::getline(ss, param, '&')) {
            size_t eq = param.find('=');
            std::string key = param.substr(0, eq);
            std::string value = eq == std::string::npos ? "" : param.substr(eq + 1);
            if (key == "link") {
                hash = value;
            }
            if (key == "index") {
                indexText = value;
            }
        }
        char *end = nullptr;
        long value = std::strtol(indexText.c_str(), &end, 10);
        if (hash.empty() || indexText.empty() || *end != '\0') {
            return false;
        }
        index = (int) value;
        return true;
    }

    // viewed files of a torrent, cached (the filer asks for each item). The main thread
    // never waits for the server: what is stale or missing comes from a worker
    std::mutex g_viewedMutex;
    // viewed file index -> position (seconds, 0 if the server keeps none)
    struct Viewed {
        std::map<int, double> files;
        time_t updated = 0;
    };
    std::unordered_map<std::string, Viewed> g_viewed;
    // bumped on each answer: the marks shown may change
    unsigned int g_viewedGeneration = 0;
    // requests to the server running (the status shows a sync)
    std::atomic<int> g_viewedFetching{0};

    // kept on disk: the marks of the last run shown at once (stale: asked again), the
    // file written by the worker when its queue is done
    bool g_viewedLoaded = false;
    bool g_viewedDirty = false;

    std::string viewedCachePath() {
        return pplay::Utility::getDataPath() + "cache/torrserver_viewed";
    }

    // "root|hash \t index:seconds,...", g_viewedMutex held
    void loadViewed() {
        if (g_viewedLoaded) {
            return;
        }
        g_viewedLoaded = true;
        FILE *f = fopen(viewedCachePath().c_str(), "r");
        if (f == nullptr) {
            return;
        }
        std::string line;
        char buf[4096];
        size_t count = 0;
        while (fgets(buf, sizeof(buf), f) != nullptr) {
            line += buf;
            if (line.empty() || line.back() != '\n') {
                continue;
            }
            line.pop_back();
            size_t tab = line.find('\t');
            if (tab != std::string::npos && g_viewed.find(line.substr(0, tab)) == g_viewed.end()) {
                Viewed &viewed = g_viewed[line.substr(0, tab)];
                std::stringstream items(line.substr(tab + 1));
                std::string item;
                while (std::getline(items, item, ',')) {
                    size_t colon = item.find(':');
                    if (colon != std::string::npos) {
                        viewed.files[std::atoi(item.substr(0, colon).c_str())] = std::atof(item.c_str() + colon + 1);
                    }
                }
                count++;
            }
            line.clear();
        }
        fclose(f);
        pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                            "TorrServer: " + std::to_string(count) + " viewed lists from the last run");
    }

    void saveViewed() {
        std::string text;
        {
            std::lock_guard<std::mutex> lock(g_viewedMutex);
            if (!g_viewedDirty) {
                return;
            }
            g_viewedDirty = false;
            char number[64];
            for (const auto &entry: g_viewed) {
                std::string items;
                for (const auto &file: entry.second.files) {
                    snprintf(number, sizeof(number), "%d:%.0f", file.first, file.second);
                    items += (items.empty() ? "" : ",") + std::string(number);
                }
                text += entry.first + "\t" + items + "\n";
            }
        }
        if (pplay::Io::get() != nullptr && !pplay::Io::get()->exist(pplay::Utility::getDataPath() + "cache")) {
            pplay::Io::get()->create(pplay::Utility::getDataPath() + "cache");
        }
        // written aside, then renamed: never a half file
        std::string path = viewedCachePath();
        FILE *f = fopen((path + ".tmp").c_str(), "w");
        if (f == nullptr) {
            return;
        }
        bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
        ok = fclose(f) == 0 && ok;
        if (ok) {
            std::rename((path + ".tmp").c_str(), path.c_str());
        }
    }

    // the worker requests, in order: a body (set / rem, to url: TS Silo) or a list (empty body)
    struct ViewedJob {
        std::string root, hash, body, url;
        // a list after a body: its result asked
        bool after = false;
    };
    std::deque<ViewedJob> g_viewedJobs;
    // root|hash with a list queued or running
    std::set<std::string> g_viewedPending;
    bool g_viewedBusy = false;
    pplay::Worker *g_viewedWorker = nullptr;

    // positions sent to the server in a thread (the player saves them on the main one)
    struct ProgressJob {
        std::string root, hash;
        int index = -1;
        double seconds = 0, duration = 0;
    };
    std::mutex g_progressMutex;
    ProgressJob g_progressJob;
    bool g_progressBusy = false;
    pplay::Worker *g_progressWorker = nullptr;
    // server root -> keeps positions (its "track timecode" setting): unknown until checked
    // (TS Silo: its list or its progress-tracking answer)
    std::unordered_map<std::string, bool> g_timecodes;

    void siloProgressSeen(const std::string &root) {
        std::lock_guard<std::mutex> lock(g_progressMutex);
        auto it = g_timecodes.find(root);
        if (it == g_timecodes.end() || !it->second) {
            g_timecodes[root] = true;
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "TorrServer.Silo: positions kept by " + root);
        }
    }

    void siloProgress(const ProgressJob &job) {
        bool known;
        {
            std::lock_guard<std::mutex> lock(g_progressMutex);
            known = g_timecodes.count(job.root) > 0;
        }
        if (!known) {
            // nothing listed yet: asked once (an upstream Silo has no such route)
            pplay::Http::Response answer = pplay::Http::get(job.root + "api/system/progress-tracking", 5);
            auto json = nlohmann::json::parse(answer.ok() ? answer.body : "", nullptr, false);
            bool enabled = !json.is_discarded() && json.is_object() && json.contains("enabled")
                           && json["enabled"].is_boolean() && json["enabled"].get<bool>();
            if (!answer.ok() && answer.status == 0) {
                return; // not reachable: asked again next time
            }
            std::lock_guard<std::mutex> lock(g_progressMutex);
            g_timecodes[job.root] = enabled;
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                std::string("TorrServer.Silo: positions ") + (enabled ? "kept" : "not kept") + " by "
                                        + job.root);
            if (!enabled) {
                return;
            }
        }
        nlohmann::json request = {{"position", job.seconds}};
        if (job.duration > 0) {
            request["duration"] = job.duration;
        }
        std::string url = job.root + "api/torrents/" + job.hash + "/files/" + std::to_string(job.index) + "/progress";
        auto response = nlohmann::json::parse(httpRequest(url, 5, request.dump()), nullptr, false);
        if (!response.is_discarded() && response.is_object() && response.contains("tracked")
            && response["tracked"].is_boolean() && !response["tracked"].get<bool>()) {
            // switched off on the server: back with the positions in its list
            std::lock_guard<std::mutex> lock(g_progressMutex);
            g_timecodes[job.root] = false;
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "TorrServer.Silo: positions not kept by " + job.root);
        }
    }

    int progressThread(void *) {
        while (true) {
            ProgressJob job;
            {
                std::lock_guard<std::mutex> lock(g_progressMutex);
                if (g_progressJob.root.empty()) {
                    g_progressBusy = false;
                    return 0;
                }
                job = g_progressJob;
                g_progressJob = {};
            }
            if (isSilo(job.root)) {
                siloProgress(job);
                continue;
            }
            nlohmann::json request;
            request["action"] = "set";
            request["hash"] = job.hash;
            request["file_index"] = job.index;
            request["timecode"] = job.seconds;
            httpRequest(job.root + "viewed", 5, request.dump());
            {
                std::lock_guard<std::mutex> lock(g_progressMutex);
                if (g_timecodes.count(job.root) > 0) {
                    continue;
                }
            }
            // the first one: read it back, a server without positions gets no more
            request = {{"action", "list"}, {"hash", job.hash}};
            auto response = nlohmann::json::parse(httpRequest(job.root + "viewed", 5, request.dump()), nullptr, false);
            if (response.is_discarded() || !response.is_array()) {
                continue;
            }
            bool kept = false;
            for (const auto &item: response) {
                kept |= item.is_object() && item.contains("file_index") && item["file_index"].is_number_integer()
                        && item["file_index"].get<int>() == job.index && item.contains("timecode")
                        && item["timecode"].is_number() && item["timecode"].get<double>() > 0;
            }
            std::lock_guard<std::mutex> lock(g_progressMutex);
            g_timecodes[job.root] = kept;
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                std::string("TorrServer.MatriX: positions ") + (kept ? "kept" : "not kept") + " by " + job.root);
        }
    }

    // TS Silo: the viewed files of its torrents list (all of them listed: the others none)
    void applyViewed(const std::string &root, const std::vector<Torrent> &torrents, const ViewedByHash &viewed) {
        std::lock_guard<std::mutex> lock(g_viewedMutex);
        loadViewed();
        time_t now = time(nullptr);
        for (const auto &torrent: torrents) {
            Viewed &entry = g_viewed[root + "|" + torrent.hash];
            auto found = viewed.find(torrent.hash);
            entry.files = found != viewed.end() ? found->second : std::map<int, double>();
            entry.updated = now;
            g_viewedPending.erase(root + "|" + torrent.hash);
        }
        g_viewedDirty = true;
        g_viewedGeneration++;
    }

    std::vector<Torrent> fetchTorrents(const std::string &root, int timeout, bool *ok);

    // TS Silo: from the torrents list (asked again when stale)
    std::map<int, double> fetchSiloViewed(const std::string &root, const std::string &hash) {
        bool shown;
        {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            shown = g_unreachable.count(root) == 0;
        }
        if (shown) {
            g_viewedFetching++;
        }
        bool ok = false;
        fetchTorrents(root, 5, &ok);
        if (shown) {
            g_viewedFetching--;
        }
        std::lock_guard<std::mutex> lock(g_viewedMutex);
        const std::string key = root + "|" + hash;
        g_viewedPending.erase(key);
        // not listed (or no answer): the marks kept, asked again later
        Viewed &viewed = g_viewed[key];
        viewed.updated = std::max(viewed.updated, time(nullptr));
        g_viewedGeneration++;
        return viewed.files;
    }

    // all the viewed files of a server at once (one request a sync interval, not one a torrent):
    // the torrents without any kept empty, fresh too
    std::map<int, double> fetchViewed(const std::string &root, const std::string &hash) {
        if (isSilo(root)) {
            return fetchSiloViewed(root, hash);
        }
        const std::string key = root + "|" + hash;
        nlohmann::json request;
        request["action"] = "list";
        // unreachable: not asked again before the next sync (the kept marks shown meanwhile)
        bool skip;
        {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            auto failed = g_viewedFailedAt.find(root);
            skip = g_unreachable.count(root) > 0 && failed != g_viewedFailedAt.end()
                   && std::chrono::steady_clock::now() - failed->second < std::chrono::seconds(syncSecs(root));
        }
        nlohmann::json response;
        if (!skip) {
            // (a retry of a server not answering: silent)
            bool shown;
            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                shown = g_unreachable.count(root) == 0;
            }
            if (shown) {
                g_viewedFetching++;
            }
            response = nlohmann::json::parse(httpRequest(root + "viewed", 5, request.dump()), nullptr, false);
            if (shown) {
                g_viewedFetching--;
            }
        } else {
            response = nlohmann::json::value_t::discarded;
        }
        std::vector<std::string> hashes;
        {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            if (!skip) {
                if (!response.is_discarded() && response.is_array()) {
                    // back: listed again
                    if (g_unreachable.erase(root) > 0) {
                        g_torrentsAt.erase(root);
                        g_torrentsFailedAt.erase(root);
                        g_torrentsGeneration++;
                    }
                    g_viewedFailedAt.erase(root);
                } else {
                    g_unreachable.insert(root);
                    g_viewedFailedAt[root] = std::chrono::steady_clock::now();
                }
            }
            for (const auto &torrent: g_torrentsCache[root]) {
                hashes.push_back(torrent.hash);
            }
        }
        std::lock_guard<std::mutex> lock(g_viewedMutex);
        g_viewedPending.erase(key);
        time_t now = time(nullptr);
        // no answer: the marks kept, asked again later
        if (!response.is_discarded() && response.is_array()) {
            std::map<std::string, std::map<int, double>> byHash;
            for (const auto &item: response) {
                if (item.contains("hash") && item["hash"].is_string() && item.contains("file_index")
                    && item["file_index"].is_number_integer()) {
                    double timecode = item.contains("timecode") && item["timecode"].is_number()
                                              ? item["timecode"].get<double>()
                                              : 0;
                    byHash[item["hash"].get<std::string>()][item["file_index"].get<int>()] = timecode;
                }
            }
            hashes.push_back(hash);
            for (auto &entry: byHash) {
                hashes.push_back(entry.first);
            }
            for (const auto &h: hashes) {
                Viewed &viewed = g_viewed[root + "|" + h];
                auto found = byHash.find(h);
                viewed.files = found != byHash.end() ? found->second : std::map<int, double>();
                viewed.updated = now;
                g_viewedPending.erase(root + "|" + h);
            }
            g_viewedDirty = true;
        } else {
            g_viewed[key].updated = now;
        }
        g_viewedGeneration++;
        return g_viewed[key].files;
    }

    std::string siloViewedUrl(const std::string &root, const std::string &hash, int index) {
        return root + "api/torrents/" + hash + "/files/" + std::to_string(index) + "/viewed";
    }

    int viewedThread(void *) {
        while (true) {
            ViewedJob job;
            {
                std::lock_guard<std::mutex> lock(g_viewedMutex);
                if (g_viewedJobs.empty()) {
                    g_viewedBusy = false;
                    break;
                }
                job = g_viewedJobs.front();
                g_viewedJobs.pop_front();
                // more changes of this server queued: listed after them (not the ones not sent yet
                // undone meanwhile)
                bool later = false, again = false;
                for (const auto &next: g_viewedJobs) {
                    later |= !next.body.empty() && next.root == job.root;
                    again |= next.body.empty() && next.root == job.root && next.hash == job.hash;
                }
                if (job.body.empty() && later) {
                    if (!again) {
                        g_viewedPending.erase(job.root + "|" + job.hash);
                    }
                    continue;
                }
            }
            if (job.body.empty()) {
                // answered meanwhile (all of a server at once)
                bool fresh;
                {
                    std::lock_guard<std::mutex> lock(g_viewedMutex);
                    auto it = g_viewed.find(job.root + "|" + job.hash);
                    fresh = !job.after && it != g_viewed.end()
                            && difftime(time(nullptr), it->second.updated) < syncSecs(job.root);
                    if (fresh) {
                        g_viewedPending.erase(job.root + "|" + job.hash);
                    }
                }
                if (!fresh) {
                    fetchViewed(job.root, job.hash);
                }
            } else {
                httpRequest(job.url.empty() ? job.root + "viewed" : job.url, 5, job.body);
                if (!job.url.empty()) {
                    // its list after: asked again
                    std::lock_guard<std::mutex> lock(g_cacheMutex);
                    g_torrentsAt.erase(job.root);
                }
            }
        }
        saveViewed();
        return 0;
    }

    // to the worker: a body and a list after it (its result), or a list only (once at a time)
    void queueViewed(const std::string &root, const std::string &hash, const std::string &body = {},
                     const std::string &url = {}) {
        {
            std::lock_guard<std::mutex> lock(g_viewedMutex);
            bool queued = !g_viewedPending.insert(root + "|" + hash).second;
            if (!body.empty()) {
                g_viewedJobs.push_back({root, hash, body, url});
            } else if (queued) {
                return;
            }
            g_viewedJobs.push_back({root, hash, {}, {}, !body.empty()});
            if (g_viewedBusy) {
                return; // the running worker takes it
            }
            g_viewedBusy = true;
        }
        if (g_viewedWorker != nullptr) {
            g_viewedWorker->join();
            delete (g_viewedWorker);
        }
        g_viewedWorker = new pplay::Worker(viewedThread, nullptr);
    }

    // wait: from a worker (or a load), the server asked right away when needed; cached: only
    // what is known (no request)
    std::map<int, double> getViewed(const std::string &root, const std::string &hash, bool wait,
                                    bool cached = false) {
        const std::string key = root + "|" + hash;
        std::map<int, double> files;
        {
            std::lock_guard<std::mutex> lock(g_viewedMutex);
            loadViewed();
            auto it = g_viewed.find(key);
            if (cached) {
                return it != g_viewed.end() ? it->second.files : files;
            }
            bool found = it != g_viewed.end();
            if (found && difftime(time(nullptr), it->second.updated) < syncSecs(root)) {
                return it->second.files;
            }
            if (found) {
                files = it->second.files;
            }
        }
        if (wait) {
            return fetchViewed(root, hash);
        }
        // what is known now, the answer later (viewedGeneration)
        queueViewed(root, hash);
        return files;
    }

}

namespace pplay::TorrServe {

    bool isSiloServer(const std::string &path) {
        return isSilo(path);
    }

    bool isAllFiles(const std::string &path) {
        if (!isTorrServe(path)) {
            return false;
        }
        std::vector<std::string> parts = splitPath(relativeOf(path));
        return parts.size() == 2 && parts[1] == ALL_FILES;
    }

    std::string dirOf(const std::string &path) {
        if (!isTorrServe(path)) {
            return "";
        }
        // <torrent title>/<dirs...>/<file>
        std::vector<std::string> parts = splitPath(relativeOf(path));
        std::string dir;
        for (size_t i = 1; i + 1 < parts.size(); i++) {
            dir += (dir.empty() ? "" : "/") + parts[i];
        }
        return dir;
    }

    std::vector<c2d::Io::File> getDirList(const std::string &path, int timeout, std::string *error) {
        std::vector<c2d::Io::File> files;
        std::string root = rootOf(path);
        std::string httpRoot = apiRoot(path);
        std::vector<std::string> parts = splitPath(relativeOf(path));
        bool ok = false;
        std::vector<Torrent> torrents = torrentsOf(httpRoot, timeout, &ok);
        if (!ok && error) {
            *error = nameOf(httpRoot) + (refused(httpRoot) ? ": HTTP 401" : ": not reachable");
        }

        if (parts.empty()) {
            for (const auto &torrent: torrents) {
                files.emplace_back(torrent.title, root + escapeSegment(torrent.title), c2d::Io::Type::Directory);
            }
            return files;
        }

        auto torrentIt = std::find_if(torrents.begin(), torrents.end(),
                                      [&parts](const Torrent &torrent) { return torrent.title == parts[0]; });
        if (torrentIt == torrents.end()) {
            return getDirList(root, timeout, error);
        }

        // all the files at once, by index
        if (parts.size() == 2 && parts[1] == ALL_FILES) {
            std::vector<TorrentFile> sorted = torrentIt->files;
            std::stable_sort(sorted.begin(), sorted.end(),
                             [](const TorrentFile &a, const TorrentFile &b) { return a.id < b.id; });
            for (const auto &torrentFile: sorted) {
                std::vector<std::string> virtualParts = {torrentIt->title};
                for (auto &part: splitPath(torrentFile.path)) {
                    virtualParts.push_back(part);
                }
                std::string virtualPath = joinVirtual(root, virtualParts) + "?link=" + escapeSegment(torrentIt->hash)
                                          + "&index=" + std::to_string(torrentFile.id)
                                          + "&dummy=" + escapeSegment(basename(torrentFile.path));
                files.emplace_back(virtualParts.back(), virtualPath, c2d::Io::Type::File,
                                   (uint64_t) torrentFile.length);
            }
            return files;
        }

        std::vector<std::string> prefix(parts.begin() + 1, parts.end());
        std::set<std::string> dirs;

        for (const auto &torrentFile: torrentIt->files) {
            std::vector<std::string> fileParts = splitPath(torrentFile.path);
            if (!hasPrefix(fileParts, prefix) || fileParts.size() <= prefix.size()) {
                continue;
            }

            std::string name = fileParts[prefix.size()];
            std::vector<std::string> virtualParts = parts;
            virtualParts.push_back(name);
            if (fileParts.size() > prefix.size() + 1) {
                if (dirs.insert(name).second) {
                    files.emplace_back(name, joinVirtual(root, virtualParts), c2d::Io::Type::Directory);
                }
            } else {
                std::string virtualPath = joinVirtual(root, virtualParts) + "?link=" + escapeSegment(torrentIt->hash)
                                          + "&index=" + std::to_string(torrentFile.id)
                                          + "&dummy=" + escapeSegment(basename(torrentFile.path));
                files.emplace_back(name, virtualPath, c2d::Io::Type::File, (uint64_t) torrentFile.length);
            }
        }
        // the torrent root with directories: all the files too
        if (parts.size() == 1 && !dirs.empty()) {
            files.emplace_back(pplay::tr(ALL_FILES), joinVirtual(root, {parts[0], ALL_FILES}),
                               c2d::Io::Type::Directory);
        }
        return files;
    }

    bool getTree(const std::string &path, int timeout, std::vector<c2d::Io::File> *files) {
        std::string root = rootOf(path);
        std::string httpRoot = apiRoot(path);
        std::vector<std::string> parts = splitPath(relativeOf(path));
        bool ok = false;
        std::vector<Torrent> torrents = torrentsOf(httpRoot, timeout, &ok);
        if (!ok) {
            return false;
        }
        // all the files: the torrent itself
        if (parts.size() == 2 && parts[1] == ALL_FILES) {
            parts.pop_back();
        }
        files->clear();
        for (const auto &torrent: torrents) {
            if (!parts.empty() && torrent.title != parts[0]) {
                continue;
            }
            std::vector<std::string> prefix;
            if (!parts.empty()) {
                prefix.assign(parts.begin() + 1, parts.end());
            }
            for (const auto &torrentFile: torrent.files) {
                std::vector<std::string> fileParts = splitPath(torrentFile.path);
                if (!hasPrefix(fileParts, prefix) || fileParts.size() <= prefix.size()) {
                    continue;
                }
                std::vector<std::string> virtualParts = {torrent.title};
                virtualParts.insert(virtualParts.end(), fileParts.begin(), fileParts.end());
                std::string virtualPath = joinVirtual(root, virtualParts) + "?link=" + escapeSegment(torrent.hash)
                                          + "&index=" + std::to_string(torrentFile.id)
                                          + "&dummy=" + escapeSegment(basename(torrentFile.path));
                files->emplace_back(fileParts.back(), virtualPath, c2d::Io::Type::File, (uint64_t) torrentFile.length);
            }
        }
        return true;
    }

    std::string toStreamUrl(const std::string &p) {
        // a saved copy: its network file
        const std::string path = pplay::Saved::originalOf(p);
        if (!isTorrServe(path)) {
            auto *io = pplay::Io::get();
            auto type = io != nullptr ? io->getDeviceType(path) : pplay::Io::DeviceType::Local;
            if (type == pplay::Io::DeviceType::Http || type == pplay::Io::DeviceType::WebDav) {
                return pplay::Http::normalize(path);
            }
            if (type == pplay::Io::DeviceType::Dlna) {
                std::string url = pplay::Dlna::streamUrl(path);
                return url.empty() ? path : url;
            }
            return path;
        }

        std::string hash;
        int index;
        if (!fileLink(path, hash, index)) {
            return path;
        }
        std::string root = replaceScheme(rootOf(path));
        if (isSilo(root)) {
            return root + "api/stream/" + hash + "/" + std::to_string(index);
        }
        return root + "stream/?link=" + hash + "&index=" + std::to_string(index) + "&play";
    }

    namespace {
        struct LinkParts {
            std::string user, host, base, hash;
            int index = -1;
            // TS Silo: api/stream/<hash>/<index> (from 0)
            bool silo = false;
        };

        // scheme://[user[:password]@]host[:port]/<base>[stream...|play/...|api/stream/...]
        LinkParts linkParts(const std::string &u, bool link) {
            LinkParts parts;
            std::string url = replaceScheme(u);
            size_t scheme = url.find("://");
            if (scheme == std::string::npos) {
                return parts;
            }
            size_t slash = url.find('/', scheme + 3);
            std::string authority = url.substr(scheme + 3, slash == std::string::npos ? std::string::npos : slash - scheme - 3);
            std::string path = slash == std::string::npos ? "/" : url.substr(slash);
            size_t at = authority.rfind('@');
            if (at != std::string::npos) {
                parts.user = authority.substr(0, std::min(authority.find(':'), at));
                authority = authority.substr(at + 1);
            }
            parts.host = c2d::Utility::toLower(authority);
            if (!link) {
                parts.base = c2d::Utility::endsWith(path, "/") ? path : path + "/";
                return parts;
            }
            size_t query = path.find('?');
            std::string route = path.substr(0, query);
            size_t play = route.find("/play/");
            size_t stream = route.find("/stream");
            size_t api = route.find("/api/stream/");
            if (api != std::string::npos) {
                parts.base = route.substr(0, api + 1);
                std::string rest = route.substr(api + 12);
                size_t sep = rest.find('/');
                char *end = nullptr;
                long value = sep == std::string::npos ? -1 : std::strtol(rest.c_str() + sep + 1, &end, 10);
                if (value >= 0 && end != nullptr && (*end == '\0' || *end == '/')) {
                    parts.hash = rest.substr(0, sep);
                    parts.index = (int) value;
                    parts.silo = true;
                }
            } else if (play != std::string::npos) {
                parts.base = route.substr(0, play + 1);
                std::string rest = route.substr(play + 6);
                size_t sep = rest.find('/');
                parts.hash = rest.substr(0, sep);
                parts.index = sep == std::string::npos ? 1 : std::atoi(rest.c_str() + sep + 1);
            } else if (stream != std::string::npos && fileLink(path, parts.hash, parts.index)) {
                parts.base = route.substr(0, stream + 1);
            }
            parts.hash = c2d::Utility::toLower(unescapeSegment(parts.hash));
            return parts;
        }
    }

    bool isLinkOf(const std::string &network, const std::string &url) {
        LinkParts link = linkParts(url, true), net = linkParts(network, false);
        // the indexes of the other api differ
        return !link.hash.empty() && !link.host.empty() && link.host == net.host && link.base == net.base
               && (link.user.empty() || link.user == net.user) && link.silo == isSilo(apiRoot(network));
    }

    bool findLinkFile(const std::string &network, const std::string &url, int timeout, c2d::Io::File *file) {
        LinkParts link = linkParts(url, true);
        if (link.hash.empty()) {
            return false;
        }
        // the list asked again: the torrent may have been added just now
        {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            g_torrentsAt.erase(apiRoot(network));
            g_torrentsFailedAt.erase(apiRoot(network));
        }
        std::vector<c2d::Io::File> files;
        if (!getTree(network, timeout, &files)) {
            return false;
        }
        for (auto &f: files) {
            std::string hash;
            int index;
            if (fileLink(f.path, hash, index) && index == link.index
                && c2d::Utility::toLower(unescapeSegment(hash)) == link.hash) {
                *file = f;
                return true;
            }
        }
        return false;
    }

    bool isReachable(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        return g_unreachable.count(apiRoot(path)) == 0;
    }

    unsigned int torrentsGeneration() {
        return g_torrentsGeneration;
    }

    unsigned int viewedGeneration() {
        std::lock_guard<std::mutex> lock(g_viewedMutex);
        return g_viewedGeneration;
    }

    bool isFileViewed(const std::string &p, bool wait, bool cached) {
        // a saved copy: its network file
        const std::string path = pplay::Saved::originalOf(p);
        std::string hash;
        int index;
        return isTorrServe(path) && fileLink(path, hash, index)
               && getViewed(apiRoot(path), hash, wait, cached).count(index) > 0;
    }

    bool isSyncing() {
        return g_viewedFetching > 0 || g_torrentsFetching.load() > 0;
    }

    void saveViewedCache() {
        saveViewed();
    }

    void forgetFailures() {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_torrentsFailedAt.clear();
        g_viewedFailedAt.clear();
        g_refused.clear();
        {
            std::lock_guard<std::mutex> protocols(g_protocolMutex);
            g_siloProtocol.clear();
        }
        std::lock_guard<std::mutex> tokens(g_tokenMutex);
        g_tokens.clear();
    }

    std::string tokenOf(const std::string &url) {
        std::string root = apiRoot(url);
        {
            std::lock_guard<std::mutex> lock(g_tokenMutex);
            auto known = g_tokens.find(root);
            if (known != g_tokens.end()) {
                return known->second.token;
            }
        }
        return pplay::Io::get() != nullptr ? pplay::Io::get()->siloToken(url) : std::string();
    }

    std::string siloToken(const std::string &network, int timeout) {
        std::string root = apiRoot(network);
        long status = 0;
        if (!isSilo(root) || !siloLogin(root, timeout, false, &status)) {
            return {};
        }
        return tokenOf(network);
    }

    std::vector<Token> takeTokens() {
        std::lock_guard<std::mutex> lock(g_tokenMutex);
        std::vector<Token> tokens;
        tokens.swap(g_newTokens);
        return tokens;
    }

    std::string linkToken(const std::string &url) {
        size_t query = url.find('?');
        if (query == std::string::npos) {
            return {};
        }
        std::stringstream ss(url.substr(query + 1));
        std::string param;
        while (std::getline(ss, param, '&')) {
            if (param.rfind("token=", 0) == 0) {
                return unescapeSegment(param.substr(6));
            }
        }
        return {};
    }

    double fileProgress(const std::string &p, bool wait) {
        // a saved copy: its network file
        const std::string path = pplay::Saved::originalOf(p);
        std::string hash;
        int index;
        if (!isTorrServe(path) || !fileLink(path, hash, index)) {
            return 0;
        }
        auto viewed = getViewed(apiRoot(path), hash, wait);
        auto it = viewed.find(index);
        return it != viewed.end() ? it->second : 0;
    }

    void setFileProgress(const std::string &p, double seconds, double duration) {
        // a saved copy: its network file
        const std::string path = pplay::Saved::originalOf(p);
        std::string hash;
        int index;
        if (!isTorrServe(path) || !fileLink(path, hash, index)) {
            return;
        }
        if (seconds < 1) {
            return;
        }
        std::string root = apiRoot(path);
        bool known;
        {
            std::lock_guard<std::mutex> lock(g_progressMutex);
            auto support = g_timecodes.find(root);
            if (support != g_timecodes.end() && !support->second) {
                return;
            }
            known = support != g_timecodes.end();
        }
        // a Silo not known to keep them: its file not marked here before it says so
        if (known || !isSilo(root)) {
            std::lock_guard<std::mutex> lock(g_viewedMutex);
            auto cached = g_viewed.find(root + "|" + hash);
            if (cached != g_viewed.end()) {
                cached->second.files[index] = seconds;
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_progressMutex);
            g_progressJob = {root, hash, index, seconds, duration};
            if (g_progressBusy) {
                return; // sent by the running worker: the last value only
            }
            g_progressBusy = true;
        }
        if (g_progressWorker != nullptr) {
            g_progressWorker->join();
            delete (g_progressWorker);
        }
        g_progressWorker = new pplay::Worker(progressThread, nullptr);
    }

    void flushProgress() {
        if (g_progressWorker != nullptr) {
            g_progressWorker->join();
            delete (g_progressWorker);
            g_progressWorker = nullptr;
        }
    }

    double keptPosition(const std::string &p, int mode) {
        // a saved copy: its network file
        const std::string path = pplay::Saved::originalOf(p);
        double local = pplay::Utility::watchLaterPosition(toStreamUrl(path));
        if (mode == 0) {
            return local;
        }
        double server = fileProgress(path, false);
        if (mode == 2) {
            return server > 0 ? server : local;
        }
        return local > 0 ? local : server;
    }

    bool setFileViewed(const std::string &p, double seconds) {
        // a saved copy: its network file
        const std::string path = pplay::Saved::originalOf(p);
        std::string hash;
        int index;
        if (!isTorrServe(path) || !fileLink(path, hash, index)) {
            return false;
        }
        // "set" marks it viewed, the timecode is kept by the servers that support it
        nlohmann::json request;
        request["action"] = "set";
        request["hash"] = hash;
        request["file_index"] = index;
        if (seconds >= 1) {
            request["timecode"] = seconds;
        }
        std::string root = apiRoot(path);
        {
            // shown at once, the server answer confirms it
            std::lock_guard<std::mutex> lock(g_viewedMutex);
            g_viewed[root + "|" + hash].files[index] = seconds;
            g_viewedGeneration++;
            g_viewedDirty = true;
        }
        if (isSilo(root)) {
            queueViewed(root, hash, "{\"viewed\":true}", siloViewedUrl(root, hash, index));
            return true;
        }
        queueViewed(root, hash, request.dump());
        return true;
    }

    bool remFileViewed(const std::string &p) {
        // a saved copy: its network file
        const std::string path = pplay::Saved::originalOf(p);
        std::string hash;
        int index;
        if (!isTorrServe(path) || !fileLink(path, hash, index)) {
            return false;
        }
        nlohmann::json request;
        request["action"] = "rem";
        request["hash"] = hash;
        request["file_index"] = index;
        std::string root = apiRoot(path);
        {
            std::lock_guard<std::mutex> lock(g_viewedMutex);
            g_viewed[root + "|" + hash].files.erase(index);
            g_viewedGeneration++;
            g_viewedDirty = true;
        }
        if (isSilo(root)) {
            queueViewed(root, hash, "{\"viewed\":false}", siloViewedUrl(root, hash, index));
            return true;
        }
        queueViewed(root, hash, request.dump());
        return true;
    }

    bool getInfo(const std::string &path, Info &info) {
        if (!isTorrServe(path)) {
            return false;
        }
        std::vector<std::string> parts = splitPath(relativeOf(path));
        if (parts.empty()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto it = g_torrentsCache.find(apiRoot(path));
        if (it == g_torrentsCache.end()) {
            return false;
        }
        auto torrent = std::find_if(it->second.begin(), it->second.end(),
                                    [&parts](const Torrent &t) { return t.title == parts[0]; });
        if (torrent == it->second.end()) {
            return false;
        }

        info = Info{};
        info.title = torrent->title;
        info.hash = torrent->hash;
        info.poster = torrent->poster;
        info.category = torrent->category;
        info.torrentSize = torrent->size;
        info.fileCount = torrent->files.size();
        for (auto &f: torrent->files) {
            info.filesSize += f.length;
        }

        std::string hash;
        int index;
        if (fileLink(path, hash, index)) {
            for (auto &f: torrent->files) {
                if (f.id == index) {
                    info.fileIndex = index;
                    info.fileName = basename(f.path);
                    info.fileSize = f.length;
                    break;
                }
            }
        }
        return true;
    }

}
