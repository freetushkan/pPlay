//
// P2Play MediaMesh network client. This file owns the complete P2Play API adapter.
//

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include <json.hpp>

#include "io.h"
#include "net_http.h"
#include "p2play.h"
#include "pplay_config.h"
#include "utility.h"

namespace {

    using json = nlohmann::json;

    struct Session {
        std::string user;
        std::string password;
        std::string token;
    };

    std::mutex g_mutex;
    std::map<std::string, Session> g_sessions;
    std::set<std::string> g_verifiedServers;
    std::map<std::string, std::string> g_torrents;
    std::map<std::string, std::string> g_progressLabels;

    json noBody() {
        return json(json::value_t::discarded);
    }

    std::string cleanRoot(const std::string &root) {
        std::string value = PPLAYConfig::withoutCredentials(root);
        if (!value.empty() && value.back() != '/') {
            value += '/';
        }
        return value;
    }

    std::string rootOf(const std::string &path) {
        auto *io = pplay::Io::get();
        std::string root = io != nullptr ? io->getNetworkRoot(path) : std::string();
        if (root.empty()) {
            const size_t scheme = path.find("://");
            if (scheme == std::string::npos) {
                return {};
            }
            const size_t slash = path.find('/', scheme + 3);
            root = slash == std::string::npos ? path + "/" : path.substr(0, slash + 1);
        }
        if (root.back() != '/') {
            root += '/';
        }
        return root;
    }

    std::string relativeOf(const std::string &root, const std::string &path) {
        if (path.size() <= root.size() || path.compare(0, root.size(), root) != 0) {
            return {};
        }
        std::string relative = path.substr(root.size());
        const size_t query = relative.find_first_of("?#");
        if (query != std::string::npos) {
            relative.erase(query);
        }
        while (!relative.empty() && relative.back() == '/') {
            relative.pop_back();
        }
        return relative;
    }

    std::vector<std::string> splitPath(const std::string &path) {
        std::vector<std::string> parts;
        std::stringstream stream(path);
        std::string part;
        while (std::getline(stream, part, '/')) {
            if (!part.empty()) {
                parts.push_back(pplay::Http::unescape(part));
            }
        }
        return parts;
    }

    std::string virtualPath(const std::string &root, const std::vector<std::string> &parts) {
        std::string path = root;
        for (const auto &part : parts) {
            if (path.back() != '/') {
                path += '/';
            }
            path += pplay::Http::escape(part);
        }
        return path;
    }

    std::string accountKey(const std::string &root, const std::string &context, std::string *userOut = nullptr,
                           std::string *passwordOut = nullptr) {
        std::string user, password;
        const bool hasCredentials = pplay::Io::get() != nullptr &&
                                    pplay::Io::get()->credentialsOf(context, &user, &password);
        if (!hasCredentials) {
            user = "owner";
            password.clear();
        }
        if (userOut != nullptr) {
            *userOut = user;
        }
        if (passwordOut != nullptr) {
            *passwordOut = password;
        }
        return root + '\n' + user;
    }

    pplay::Http::Response sendRequest(const std::string &root, const std::string &context,
                                      const std::string &route, const std::string &method,
                                      const std::string &body, int timeout, const std::string &token,
                                      size_t maxSize = 32u * 1024u * 1024u,
                                      const std::string &contentType = "application/json") {
        std::string path = route;
        while (!path.empty() && path.front() == '/') {
            path.erase(path.begin());
        }
        pplay::Http::Request request;
        request.url = pplay::Http::normalize(root + path);
        request.method = method;
        request.body = body;
        request.timeout = std::max(1, timeout);
        request.maxSize = maxSize;
        request.context = context;
        request.useNetworkCredentials = false;
        request.headers.emplace_back("Accept: application/json");
        if (!body.empty() || method == "POST" || method == "PUT") {
            request.headers.emplace_back("Content-Type: " + contentType);
        }
        if (!token.empty()) {
            request.headers.emplace_back("Authorization: Bearer " + token);
        }
        return pplay::Http::perform(request);
    }

    bool login(const std::string &root, const std::string &context, int timeout,
               std::string *tokenOut, std::string *error) {
        std::string user, password;
        const std::string key = accountKey(root, context, &user, &password);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_sessions.find(key);
            if (it != g_sessions.end() && it->second.user == user && it->second.password == password &&
                !it->second.token.empty()) {
                *tokenOut = it->second.token;
                return true;
            }
        }

        json body = {{"username", user}, {"password", password}};
        const auto response = sendRequest(root, context, "/api/v1/auth/login", "POST", body.dump(), timeout, {});
        if (!response.ok()) {
            *error = "P2Play login failed: " +
                     (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error);
            return false;
        }
        const json result = json::parse(response.body, nullptr, false);
        if (!result.is_object() || !result.value("token", json()).is_string() ||
            result["token"].get<std::string>().empty()) {
            *error = "P2Play login returned no token";
            return false;
        }
        const std::string token = result["token"].get<std::string>();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_sessions[key] = {user, password, token};
        }
        *tokenOut = token;
        return true;
    }

    std::string cachedToken(const std::string &root, const std::string &context) {
        std::string user, password;
        const std::string key = accountKey(root, context, &user, &password);
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_sessions.find(key);
        return it != g_sessions.end() && it->second.user == user && it->second.password == password
                       ? it->second.token
                       : std::string();
    }

    bool verifyServer(const std::string &root, const std::string &context, int timeout, std::string *error) {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_verifiedServers.count(root) != 0) {
                return true;
            }
        }
        const auto response = sendRequest(root, context, "/api/v1/system/version", "GET", {}, timeout, {});
        if (!response.ok()) {
            *error = response.status == 404
                             ? "This server does not expose the native P2Play version endpoint"
                             : "P2Play version check failed: " +
                                       (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error);
            return false;
        }
        const json version = json::parse(response.body, nullptr, false);
        if (!version.is_object() || version.value("name", std::string()) != "P2Play" ||
            !version.value("version", json()).is_string()) {
            *error = "The endpoint did not identify a native P2Play server";
            return false;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        g_verifiedServers.insert(root);
        return true;
    }

    bool apiBody(const std::string &root, const std::string &context, const std::string &route,
                 const std::string &method, const std::string &requestBody, const std::string &contentType,
                 int timeout, json *out, std::string *error, size_t maxSize = 32u * 1024u * 1024u) {
        if (!out || !error) return false;
        if (!verifyServer(root, context, timeout, error)) return false;
        std::string token = cachedToken(root, context);
        auto response = sendRequest(root, context, route, method, requestBody, timeout, token, maxSize, contentType);
        if (response.status == 401) {
            token.clear();
            if (!login(root, context, timeout, &token, error)) return false;
            response = sendRequest(root, context, route, method, requestBody, timeout, token, maxSize, contentType);
        }
        if (!response.ok()) {
            *error = "P2Play " + method + " " + route + " failed: " +
                     (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error);
            if (!response.body.empty()) {
                const json detail = json::parse(response.body, nullptr, false);
                if (detail.is_object() && detail.value("error", json()).is_string()) {
                    *error += ": " + detail["error"].get<std::string>();
                }
            }
            return false;
        }
        *out = response.body.empty() ? json::object() : json::parse(response.body, nullptr, false);
        if (out->is_discarded()) {
            *error = "P2Play returned invalid JSON for " + route;
            return false;
        }
        return true;
    }

    bool apiJson(const std::string &root, const std::string &context, const std::string &route,
                 const std::string &method, const json &body, int timeout, json *out, std::string *error) {
        const std::string requestBody = body.is_discarded() ? std::string() : body.dump();
        return apiBody(root, context, route, method, requestBody, "application/json", timeout, out, error);
    }

    bool arrayAt(const json &object, const char *key, const json **array) {
        auto it = object.find(key);
        if (it == object.end() || !it->is_array()) return false;
        *array = &*it;
        return true;
    }

    bool fileAddressOf(const std::string &filePath, std::string *context, std::string *root,
                        std::string *hash, int *index, std::string *error) {
        if (!context || !root || !hash || !index || !error) return false;
        *error = "";
        const std::string &original = filePath;
        const std::string networkRoot = rootOf(original);
        if (networkRoot.empty()) { *error = "P2Play file path has no network root"; return false; }
        const auto parts = splitPath(relativeOf(networkRoot, original));
        if (parts.size() < 6 || parts[0] != ".p2play" || parts[1] != "file" ||
            parts[3].empty() || parts[4].empty() || parts[4].size() > 9 ||
            !std::all_of(parts[4].begin(), parts[4].end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            *error = "P2Play path is not a playable torrent file"; return false;
        }
        int parsed = 0;
        for (unsigned char c : parts[4]) parsed = parsed * 10 + (c - '0');
        *context = original; *root = cleanRoot(networkRoot); *hash = parts[3]; *index = parsed;
        return true;
    }

    std::string baseName(const std::string &path) {
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    std::string progressTime(double value) {
        const long total = static_cast<long>(std::max(0.0, value));
        const long seconds = total % 60;
        const long minutes = (total / 60) % 60;
        const long hours = total / 3600;
        const std::string mm = (minutes < 10 && hours > 0 ? "0" : "") + std::to_string(minutes);
        const std::string ss = (seconds < 10 ? "0" : "") + std::to_string(seconds);
        return hours > 0 ? std::to_string(hours) + ":" + mm + ":" + ss
                         : std::to_string(total / 60) + ":" + ss;
    }

    c2d::Io::File directory(const std::string &name, const std::string &path) {
        return c2d::Io::File(name, path, c2d::Io::Type::Directory, 0);
    }

    bool containsList(const json &record, const std::string &pubkey) {
        const json *lists = nullptr;
        if (!arrayAt(record, "lists", &lists)) {
            return false;
        }
        return std::any_of(lists->begin(), lists->end(), [&](const json &value) {
            return value.is_string() && value.get<std::string>() == pubkey;
        });
    }

    bool searchResults(const std::string &root, const std::string &context, const std::string &query,
                       const std::string &tag, int timeout, json *records, std::string *error,
                       const std::vector<std::string> &selectedLists = {}) {
        std::string route = "/api/v1/catalog/search?limit=500";
        if (!query.empty()) {
            route += "&q=" + pplay::Http::escape(query);
        }
        if (!tag.empty()) {
            route += "&tag=" + pplay::Http::escape(tag);
        }
        if (!selectedLists.empty()) {
            route += "&lists=";
            for (size_t i = 0; i < selectedLists.size(); ++i) {
                if (i) route += ",";
                route += selectedLists[i];
            }
        }
        json answer;
        if (!apiJson(root, context, route, "GET", noBody(), timeout, &answer, error)) {
            return false;
        }
        const json *results = nullptr;
        if (!answer.is_object() || !arrayAt(answer, "results", &results)) {
            *error = "P2Play catalog search returned no results array";
            return false;
        }
        *records = *results;
        return true;
    }

    void addRecordDirectories(const std::string &pathRoot, const json &records,
                              const std::string &listFilter, std::vector<c2d::Io::File> *files) {
        for (const auto &record : records) {
            if (!record.is_object() || !record.value("hash", json()).is_string()) {
                continue;
            }
            const std::string hash = record["hash"].get<std::string>();
            if (hash.empty() || (!listFilter.empty() && !containsList(record, listFilter))) {
                continue;
            }
            const std::string title = record.value("title", std::string("Untitled"));
            files->push_back(directory(title, virtualPath(pathRoot, {".p2play", "record", hash})));
        }
    }

    bool statusForList(const std::string &root, const std::string &context, const std::string &pubkey,
                       int timeout, json *status, std::string *error) {
        const std::string route = "/api/v1/lists/" + pplay::Http::escape(pubkey) + "/status";
        if (!apiJson(root, context, route, "GET", noBody(), timeout, status, error)) {
            return false;
        }
        if (!status->contains("name")) {
            json ignored;
            if (!apiJson(root, context, "/api/v1/lists/" + pplay::Http::escape(pubkey), "POST",
                         json::object(), timeout, &ignored, error)) {
                return false;
            }
            for (int attempt = 0; attempt < 16; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (!apiJson(root, context, route, "GET", noBody(), timeout, status, error)) {
                    return false;
                }
                if (status->contains("name")) {
                    return true;
                }
                const auto net = status->value("net", json::object());
                if (net.is_object() && net.value("error", json()).is_string() &&
                    !net["error"].get<std::string>().empty()) {
                    *error = "P2Play subscription failed: " + net["error"].get<std::string>();
                    return false;
                }
            }
            *error = "P2Play subscription is pending; reopen this catalog in a few seconds";
            return false;
        }
        return true;
    }

    bool openTorrent(const std::string &root, const std::string &context, const std::string &recordHash,
                     int timeout, json *torrent, std::string *error) {
        const std::string key = root + '\n' + recordHash;
        std::string torrentHash;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_torrents.find(key);
            if (it != g_torrents.end()) {
                torrentHash = it->second;
            }
        }
        if (torrentHash.empty()) {
            json record;
            if (!apiJson(root, context, "/api/v1/catalog/" + pplay::Http::escape(recordHash),
                         "GET", noBody(), timeout, &record, error)) {
                return false;
            }
            std::string link = record.value("torrs", std::string());
            if (link.empty()) {
                const std::string hash = record.value("hash", recordHash);
                if (hash.size() == 40) {
                    link = "magnet:?xt=urn:btih:" + hash;
                } else if (hash.size() == 64) {
                    link = "magnet:?xt=urn:btmh:1220" + hash;
                }
                const json trackers = record.value("trackers", json::array());
                if (trackers.is_array()) {
                    for (const auto &tracker : trackers) {
                        if (tracker.is_string()) {
                            link += "&tr=" + pplay::Http::escape(tracker.get<std::string>());
                        }
                    }
                }
            }
            if (link.empty()) {
                *error = "The MediaMesh record has no playable torrent link";
                return false;
            }
            json tags = record.value("tags", json::array());
            std::string category;
            if (tags.is_array() && !tags.empty() && tags[0].is_string()) {
                category = tags[0].get<std::string>();
            }
            // Persistent library adds use the product-owned, versioned API.
            // The native detailed torrent status response below contains file_stats.
            json addBody = {
                    {"link", link},
                    {"title", record.value("title", std::string())},
                    {"category", category}};
            json added;
            if (!apiJson(root, context, "/api/v1/torrents", "POST", addBody, timeout, &added, error)) {
                return false;
            }
            const json addedTorrent = added.value("torrent", json::object());
            torrentHash = addedTorrent.value("hash", std::string());
            if (torrentHash.empty()) {
                *error = "P2Play did not return a torrent hash after adding the catalog record";
                return false;
            }
            std::lock_guard<std::mutex> lock(g_mutex);
            g_torrents[key] = torrentHash;
        }

        const std::string statusRoute = "/api/v1/torrents/" + pplay::Http::escape(torrentHash);
        for (int attempt = 0; attempt < 40; ++attempt) {
            json response;
            if (!apiJson(root, context, statusRoute, "GET", noBody(), timeout, &response, error)) {
                return false;
            }
            *torrent = response.value("details", json::object());
            const json fileStats = torrent->value("file_stats", json::array());
            if (fileStats.is_array() && !fileStats.empty()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        *error = "P2Play torrent metadata did not become available";
        return false;
    }

    bool parseTorrentInfo(const json &status, pplay::P2Play::TorrentInfo *out) {
        if (!out || !status.is_object()) return false;
        const json stats = status.value("file_stats", json::array());
        if (!stats.is_array() || stats.empty()) return false;
        pplay::P2Play::TorrentInfo info;
        info.hash = status.value("hash", std::string());
        info.title = status.value("title", std::string());
        info.size = status.value("size", int64_t(0));
        int64_t measuredSize = 0;
        for (const auto &file : stats) {
            if (!file.is_object() || !file.value("id", json()).is_number_integer() ||
                !file.value("path", json()).is_string()) continue;
            pplay::P2Play::TorrentFile item;
            item.index = file["id"].get<int>();
            item.path = file["path"].get<std::string>();
            item.size = file.value("length", int64_t(0));
            measuredSize += std::max<int64_t>(0, item.size);
            info.files.push_back(std::move(item));
        }
        if (info.size <= 0) info.size = measuredSize;
        if (info.hash.empty() || info.files.empty()) return false;
        *out = std::move(info);
        return true;
    }

    bool addTorrent(const std::string &root, const std::string &context, const std::string &link,
                    const std::string &title, const std::string &poster, const std::string &category,
                    bool save, int timeout, pplay::P2Play::TorrentInfo *torrent, std::string *error) {
        if (link.empty() || !torrent || !error) return false;
        json added;
        std::string hash;
        if (save) {
            // The new native route always adds the torrent to this user's library.
            // Poster remains a catalog-media concern and is not part of this API contract.
            json body = {{"link", link}, {"title", title}, {"category", category}};
            if (!apiJson(root, context, "/api/v1/torrents", "POST", body, timeout, &added, error)) return false;
            const json torrentCard = added.value("torrent", json::object());
            hash = torrentCard.value("hash", std::string());
        } else {
            // Preview torrents are held in memory and scoped to this user; they are not
            // written to the persistent library.
            json body = {{"link", link}, {"title", title}, {"poster", poster}, {"category", category}};
            if (!apiJson(root, context, "/api/v1/torrents/preview", "POST", body, timeout, &added, error)) return false;
            const json torrentCard = added.value("torrent", json::object());
            hash = torrentCard.value("hash", std::string());
        }
        if (hash.empty()) { *error = "P2Play did not return a torrent hash after adding the link"; return false; }
        const std::string route = (save ? "/api/v1/torrents/" : "/api/v1/torrents/") +
                                  pplay::Http::escape(hash);
        for (int attempt = 0; attempt < 40; ++attempt) {
            json status;
            if (!apiJson(root, context, route, "GET", noBody(), timeout, &status, error)) return false;
            status = status.value("details", json::object());
            if (parseTorrentInfo(status, torrent)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        *error = "P2Play torrent metadata did not become available";
        return false;
    }

    bool getCatalogRecord(const std::string &root, const std::string &context, const std::string &hash,
                          int timeout, json *record, std::string *error) {
        return apiJson(root, context, "/api/v1/catalog/" + pplay::Http::escape(hash),
                       "GET", noBody(), timeout, record, error);
    }

    bool getCatalogMedia(const std::string &root, const std::string &context, const std::string &hash,
                         const std::string &kind, int timeout, std::string *bytes, std::string *error) {
        if (bytes == nullptr || error == nullptr || (kind != "description" && kind != "poster")) {
            if (error) *error = "invalid catalog media request";
            return false;
        }
        const size_t maxSize = kind == "description" ? (64u << 10) : (2u << 20);
        const std::string route = "/api/v1/catalog/" + pplay::Http::escape(hash) + "/" + kind;
        std::string token = cachedToken(root, context);
        for (int attempt = 0; attempt < 4; ++attempt) {
            auto response = sendRequest(root, context, route, "GET", {}, timeout, token, maxSize);
            if (response.status == 401) {
                token.clear();
                if (!login(root, context, timeout, &token, error)) return false;
                response = sendRequest(root, context, route, "GET", {}, timeout, token, maxSize);
            }
            if (response.status == 503 && attempt < 3) {
                // The daemon queues the lazy media-pack request and advertises Retry-After: 2.
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            if (!response.ok()) {
                *error = response.status == 404 ? "catalog media is not published"
                                                : "catalog " + kind + " request failed (HTTP " +
                                                      std::to_string(response.status) + ")";
                if (!response.error.empty()) *error += ": " + response.error;
                return false;
            }
            if (response.body.empty()) {
                *error = "catalog " + kind + " is empty";
                return false;
            }
            *bytes = std::move(response.body);
            return true;
        }
        *error = "catalog " + kind + " is still being fetched by the daemon";
        return false;
    }

    std::string recordLink(const json &record) {
        std::string link = record.value("torrs", std::string());
        if (!link.empty()) return link;
        const std::string hash = record.value("hash", std::string());
        if (hash.size() == 40) link = "magnet:?xt=urn:btih:" + hash;
        else if (hash.size() == 64) link = "magnet:?xt=urn:btmh:1220" + hash;
        const json trackers = record.value("trackers", json::array());
        if (trackers.is_array()) for (const auto &tracker : trackers) {
            if (tracker.is_string()) {
                link += (link.find('?') == std::string::npos ? "?" : "&") +
                        std::string("tr=") + pplay::Http::escape(tracker.get<std::string>());
            }
        }
        return link;
    }

    std::vector<std::string> pathParts(const std::string &path) {
        const std::string context = rootOf(path);
        return context.empty() ? std::vector<std::string>() : splitPath(relativeOf(context, path));
    }

    std::string dateLabel(int64_t date) {
        if (date <= 0) return "Unknown date";
        const std::time_t value = static_cast<std::time_t>(date);
        std::tm tm{};
        if (!gmtime_r(&value, &tm)) return std::to_string(date);
        char buffer[24]{};
        if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm) == 0) return std::to_string(date);
        return buffer;
    }

    std::string streamToken(const std::string &root, const std::string &context, int timeout) {
        std::string token = cachedToken(root, context);
        if (!token.empty()) {
            return token;
        }
        std::string failure;
        if (!login(root, context, timeout, &token, &failure)) {
            return {};
        }
        return token;
    }

} // namespace

namespace pplay::P2Play {

    bool isSearchPromptPath(const std::string &path) {
        const auto p = pathParts(path);
        return p.size() == 2 && p[0] == ".p2play" && p[1] == "search";
    }
    bool isSearchResultsPath(const std::string &path) {
        const auto p = pathParts(path);
        return (p.size() == 3 || p.size() == 4) && p[0] == ".p2play" && p[1] == "results";
    }
    bool isContinueWatchingPath(const std::string &path) {
        const auto p = pathParts(path);
        return p.size() == 2 && p[0] == ".p2play" && p[1] == "progress";
    }
    std::string progressLabel(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_progressLabels.find(path);
        return it == g_progressLabels.end() ? std::string() : it->second;
    }
    bool isSearchResultPath(const std::string &path) {
        const auto p = pathParts(path);
        return p.size() == 3 && p[0] == ".p2play" && p[1] == "result";
    }
    std::string searchResultsPath(const std::string &contextPath, const std::string &query,
                                  const std::vector<std::string> &selectedLists) {
        const auto root = rootOf(contextPath);
        if (root.empty() || query.empty()) return {};
        std::vector<std::string> parts = {".p2play", "results", query};
        std::set<std::string> unique;
        for (const auto &key : selectedLists) {
            if (key.size() == 64 && std::all_of(key.begin(), key.end(), [](unsigned char c) {
                    return std::isxdigit(c) != 0;
                })) unique.insert(key);
        }
        if (!unique.empty()) {
            std::string filter;
            for (const auto &key : unique) {
                if (!filter.empty()) filter += ',';
                filter += key;
            }
            parts.push_back(filter);
        }
        return virtualPath(root, parts);
    }

    std::string libraryPath(const std::string &contextPath, const LibraryQuery &query) {
        const std::string root = rootOf(contextPath);
        if (root.empty()) return {};
        if (query.query.empty() && query.category.empty() &&
            (query.sort.empty() || query.sort == "date") &&
            (query.order.empty() || query.order == "desc")) {
            return virtualPath(root, {".p2play", "library"});
        }
        const std::vector<std::string> parts = {
            ".p2play", "library", "q-" + query.query,
            query.category.empty() ? "all" : "category-" + query.category,
            query.sort.empty() ? "date" : query.sort,
            query.order.empty() ? "desc" : query.order
        };
        return virtualPath(root, parts);
    }

    bool libraryAction(const std::string &path, std::string *action, std::string *hash) {
        if (!action) return false;
        const auto parts = pathParts(path);
        if (parts.size() < 3 || parts[0] != ".p2play" || parts[1] != "action") return false;
        *action = parts[2];
        if (hash) *hash = parts.size() >= 4 ? parts[3] : std::string();
        return true;
    }

    bool getCatalogLists(const std::string &contextPath, int timeout,
                         std::vector<CatalogList> *lists, std::string *error) {
        if (lists == nullptr || error == nullptr) return false;
        lists->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) {
            *error = "P2Play network path is invalid";
            return false;
        }
        json answer;
        if (!apiJson(cleanRoot(context), context, "/api/v1/lists", "GET", noBody(), timeout, &answer, error)) return false;
        const json *items = nullptr;
        if (!answer.is_object() || !arrayAt(answer, "lists", &items)) {
            *error = "P2Play list response has no lists array";
            return false;
        }
        for (const auto &item : *items) {
            if (!item.is_object() || !item.value("pubkey", json()).is_string()) continue;
            CatalogList list;
            list.pubkey = item["pubkey"].get<std::string>();
            if (list.pubkey.size() != 64 ||
                !std::all_of(list.pubkey.begin(), list.pubkey.end(), [](unsigned char c) {
                    return std::isxdigit(c) != 0;
                })) continue;
            list.name = item.value("name", list.pubkey.substr(0, 12));
            list.description = item.value("description", std::string());
            list.meta = item.value("type", std::string("catalog")) == "meta";
            if (list.meta) {
                const json children = item.value("items", json::array());
                if (children.is_array()) for (const auto &child : children) {
                    if (!child.is_object()) continue;
                    const json enabled = child.value("enabled", json(true));
                    if (!enabled.is_boolean() || !enabled.get<bool>() ||
                        !child.value("pubkey", json()).is_string()) continue;
                    const std::string key = child["pubkey"].get<std::string>();
                    if (key.size() == 64 && std::all_of(key.begin(), key.end(), [](unsigned char c) {
                            return std::isxdigit(c) != 0;
                        })) list.includes.push_back(key);
                }
            } else {
                list.includes.push_back(list.pubkey);
            }
            lists->push_back(std::move(list));
        }
        std::stable_sort(lists->begin(), lists->end(), [](const CatalogList &a, const CatalogList &b) {
            if (a.meta != b.meta) return !a.meta;
            return a.name < b.name;
        });
        return true;
    }
    bool getCatalogDetails(const std::string &contextPath, const std::string &recordHash, int timeout,
                           CatalogDetails *details, std::string *error) {
        if (details == nullptr || error == nullptr) return false;
        *details = CatalogDetails();
        const std::string context = rootOf(contextPath);
        if (context.empty()) {
            *error = "P2Play network path is invalid";
            return false;
        }
        const std::string root = cleanRoot(context);
        json source;
        if (!getCatalogRecord(root, context, recordHash, timeout, &source, error) || !source.is_object()) return false;

        details->title = source.value("title", std::string("Untitled"));
        details->size = source.value("size", int64_t(0));
        details->date = source.value("date", int64_t(0));
        details->files = source.value("files", 0);
        const json tags = source.value("tags", json::array());
        if (tags.is_array()) {
            for (const auto &tag : tags) {
                if (tag.is_string()) details->tags.push_back(tag.get<std::string>());
            }
        }

        // The API resolves the signed media reference across the record's source lists.
        // Pack and slice hashes are verified by the native daemon before these bytes are served.
        const auto descriptionRef = source.find("description_ref");
        if (descriptionRef != source.end() && descriptionRef->is_object()) {
            if (!getCatalogMedia(root, context, recordHash, "description", timeout,
                                 &details->description, &details->descriptionError)) {
                details->description.clear();
            }
        }
        const auto posterRef = source.find("poster_ref");
        if (posterRef != source.end() && posterRef->is_object()) {
            if (getCatalogMedia(root, context, recordHash, "poster", timeout,
                                &details->posterBytes, &details->posterError)) {
                // A stable local cache key, with credentials removed from the network origin.
                details->posterCacheKey = "p2play-media:" + root + recordHash + "/poster";
            } else {
                details->posterBytes.clear();
            }
        }
        return true;
    }

    std::string searchResultHash(const std::string &path) {
        const auto p = pathParts(path);
        return p.size() == 3 && p[0] == ".p2play" && p[1] == "result" ? p[2] : std::string();
    }
    std::string directTorrentPath(const std::string &contextPath, const std::string &hash) {
        const auto root = rootOf(contextPath);
        return root.empty() ? std::string() : virtualPath(root, {".p2play", "direct", hash});
    }

    bool previewCatalogRecord(const std::string &contextPath, const std::string &recordHash, int timeout,
                              CatalogRecord *record, TorrentInfo *torrent, std::string *error) {
        const std::string context = rootOf(contextPath);
        if (context.empty()) { if (error) *error = "P2Play network path is invalid"; return false; }
        const std::string root = cleanRoot(context);
        json source;
        if (!getCatalogRecord(root, context, recordHash, timeout, &source, error)) return false;
        if (record) {
            record->hash = source.value("hash", recordHash);
            record->title = source.value("title", std::string("Untitled"));
            record->size = source.value("size", int64_t(0));
            record->date = source.value("date", int64_t(0));
            record->files = source.value("files", 0);
            const json tags = source.value("tags", json::array());
            if (tags.is_array()) for (const auto &tag : tags) if (tag.is_string()) record->tags.push_back(tag.get<std::string>());
        }
        const std::string link = recordLink(source);
        if (link.empty()) { if (error) *error = "Catalog record has no playable torrent link"; return false; }
        const json tags = source.value("tags", json::array());
        const std::string category = tags.is_array() && !tags.empty() && tags[0].is_string()
                                         ? tags[0].get<std::string>() : std::string();
        return addTorrent(root, context, link, source.value("title", std::string()),
                          source.value("poster", std::string()), category, false, timeout, torrent, error);
    }

    bool addCatalogRecord(const std::string &contextPath, const std::string &recordHash, int timeout,
                          TorrentInfo *torrent, std::string *error) {
        const std::string context = rootOf(contextPath);
        if (context.empty()) { if (error) *error = "P2Play network path is invalid"; return false; }
        const std::string root = cleanRoot(context);
        json source;
        if (!getCatalogRecord(root, context, recordHash, timeout, &source, error)) return false;
        const std::string link = recordLink(source);
        if (link.empty()) { if (error) *error = "Catalog record has no playable torrent link"; return false; }
        const json tags = source.value("tags", json::array());
        const std::string category = tags.is_array() && !tags.empty() && tags[0].is_string()
                                         ? tags[0].get<std::string>() : std::string();
        return addTorrent(root, context, link, source.value("title", std::string()),
                          source.value("poster", std::string()), category, true, timeout, torrent, error);
    }

    bool previewTorrentLink(const std::string &contextPath, const std::string &link, const std::string &title,
                            int timeout, TorrentInfo *torrent, std::string *error) {
        const std::string context = rootOf(contextPath);
        if (context.empty()) { if (error) *error = "P2Play network path is invalid"; return false; }
        return addTorrent(cleanRoot(context), context, link, title, "", "", false, timeout, torrent, error);
    }

    bool addTorrentLink(const std::string &contextPath, const std::string &link, const std::string &title,
                        int timeout, TorrentInfo *torrent, std::string *error) {
        const std::string context = rootOf(contextPath);
        if (context.empty()) { if (error) *error = "P2Play network path is invalid"; return false; }
        return addTorrent(cleanRoot(context), context, link, title, "", "", true, timeout, torrent, error);
    }

    std::vector<c2d::Io::File> getDirList(const std::string &path, int timeout, std::string *error) {
        std::vector<c2d::Io::File> files;
        const std::string context = rootOf(path);
        if (context.empty()) {
            if (error != nullptr) {
                *error = "P2Play network URL must be HTTP or HTTPS";
            }
            return files;
        }
        const std::string root = cleanRoot(context);
        std::string failure;
        if (!verifyServer(root, context, timeout, &failure)) {
            if (error != nullptr) {
                *error = failure;
            }
            return files;
        }
        const std::string relative = relativeOf(context, path);
        std::vector<std::string> parts = splitPath(relative);
        // Breadcrumbs and restored paths can address intermediate URL segments
        // of this virtual filesystem. Treat those non-leaf route prefixes as
        // its landing directory rather than rejecting them as malformed routes.
        if (parts.size() == 1 && parts[0] == ".p2play") {
            parts.clear();
        } else if (parts.size() == 2 && parts[0] == ".p2play" &&
                   (parts[1] == "list" || parts[1] == "record" ||
                    parts[1] == "results" || parts[1] == "file")) {
            parts.clear();
        }
        auto fail = [&](const std::string &message) {
            if (error != nullptr) {
                *error = message;
            }
        };

        if (parts.empty()) {
            files.push_back(directory("My torrents", virtualPath(context, {".p2play", "library"})));
            files.push_back(directory("Search torrents", virtualPath(context, {".p2play", "search"})));
            files.push_back(directory("Continue watching", virtualPath(context, {".p2play", "progress"})));
            files.push_back(directory("Discover", virtualPath(context, {".p2play", "discover"})));
            files.push_back(directory("My lists", virtualPath(context, {".p2play", "lists"})));
            files.push_back(directory("Search by tag", virtualPath(context, {".p2play", "search"})));
            return files;
        }
        if (parts[0] != ".p2play") {
            fail("Unknown P2Play virtual path");
            return files;
        }
        if (parts.size() >= 2 && parts[1] == "library") {
            LibraryQuery query;
            if (parts.size() == 6) {
                if (parts[2].rfind("q-", 0) != 0 ||
                    (parts[3] != "all" && parts[3].rfind("category-", 0) != 0)) {
                    fail("Invalid P2Play library filter path");
                    return files;
                }
                query.query = parts[2].substr(2);
                query.category = parts[3] == "all" ? std::string() : parts[3].substr(9);
                query.sort = parts[4];
                query.order = parts[5];
            } else if (parts.size() != 2) {
                fail("Invalid P2Play library filter path");
                return files;
            }
            std::vector<LibraryTorrent> library;
            if (!getLibrary(context, timeout, &library, &failure, query)) { fail(failure); return files; }
            if (parts.size() == 2) {
                files.emplace_back("Add magnet or link…",
                    virtualPath(context, {".p2play", "action", "add-link"}), c2d::Io::Type::File, 0);
                files.emplace_back("Add .torrent file…",
                    virtualPath(context, {".p2play", "action", "add-file"}), c2d::Io::Type::File, 0);
                files.emplace_back("Search / filter / sort…",
                    virtualPath(context, {".p2play", "action", "filter-library"}), c2d::Io::Type::File, 0);
            }
            for (const auto &torrent : library) {
                std::string label = torrent.title.empty() ? torrent.hash : torrent.title;
                if (!torrent.category.empty()) label += "  [" + torrent.category + "]";
                files.push_back(directory(label,
                    virtualPath(context, {".p2play", "manage", torrent.hash})));
            }
            return files;
        }
        if (parts.size() == 3 && parts[1] == "manage") {
            const std::string &hash = parts[2];
            std::vector<LibraryTorrent> library;
            if (!getLibrary(context, timeout, &library, &failure)) { fail(failure); return files; }
            auto torrent = std::find_if(library.begin(), library.end(), [&](const LibraryTorrent &item) {
                return item.hash == hash;
            });
            if (torrent == library.end()) { fail("Torrent is no longer in this user's library"); return files; }
            files.push_back(directory("Open torrent files",
                virtualPath(context, {".p2play", "direct", hash})));
            files.emplace_back("Edit title…", virtualPath(context, {".p2play", "action", "edit-title", hash}),
                               c2d::Io::Type::File, 0);
            files.emplace_back("Edit category…", virtualPath(context, {".p2play", "action", "edit-category", hash}),
                               c2d::Io::Type::File, 0);
            files.emplace_back("Remove from my library…",
                virtualPath(context, {".p2play", "action", "remove", hash}), c2d::Io::Type::File, 0);
            return files;
        }
        if (parts.size() == 2 && parts[1] == "progress") {
            std::vector<ProgressEntry> progress;
            if (!getProgress(context, timeout, &progress, &failure)) {
                fail(failure);
                return files;
            }
            for (const auto &entry : progress) {
                if (entry.viewed || entry.position <= 0 || entry.hash.empty() || entry.fileIndex < 0 ||
                    entry.fileName.empty()) continue;
                const std::string labelBase = entry.title.empty() || entry.title == entry.fileName
                                                  ? entry.fileName : entry.title + " — " + entry.fileName;
                std::string label = labelBase + "  (" + progressTime(entry.position);
                if (entry.duration > 0) label += " / " + progressTime(entry.duration);
                label += ")";
                const std::string filePath = virtualPath(context, {".p2play", "file", "progress", entry.hash,
                                                                    std::to_string(entry.fileIndex), entry.fileName});
                c2d::Io::File file(entry.fileName, filePath, c2d::Io::Type::File, 0);
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_progressLabels[filePath] = label;
                }
                files.push_back(std::move(file));
            }
            return files;
        }
        if ((parts.size() == 3 || parts.size() == 4) && parts[1] == "results") {
            json records;
            std::vector<std::string> selectedLists;
            if (parts.size() == 4) {
                std::stringstream filter(parts[3]);
                std::string key;
                while (std::getline(filter, key, ',')) {
                    if (key.size() == 64) selectedLists.push_back(key);
                }
            }
            if (!searchResults(root, context, parts[2], {}, timeout, &records, &failure, selectedLists)) {
                fail(failure);
                return files;
            }
            std::vector<json> ordered;
            for (const auto &record : records) if (record.is_object()) ordered.push_back(record);
            std::stable_sort(ordered.begin(), ordered.end(), [](const json &a, const json &b) {
                return a.value("date", int64_t(0)) > b.value("date", int64_t(0));
            });
            for (const auto &record : ordered) {
                const std::string hash = record.value("hash", std::string());
                if (hash.empty()) continue;
                files.push_back(directory(dateLabel(record.value("date", int64_t(0))) + "  |  " +
                                          record.value("title", std::string("Untitled")),
                                          virtualPath(context, {".p2play", "result", hash})));
            }
            return files;
        }
        if (parts.size() == 3 && parts[1] == "direct") {
            json response;
            if (!apiJson(root, context, "/api/v1/torrents/" + pplay::Http::escape(parts[2]),
                         "GET", noBody(), timeout, &response, &failure)) {
                fail(failure);
                return files;
            }
            // Native v1 intentionally wraps the current user's detailed status separately
            // from the compact library card returned by GET /library/torrents.
            const json status = response.value("details", json::object());
            const json stats = status.value("file_stats", json::array());
            if (!stats.is_array()) { fail("P2Play torrent response has no file list"); return files; }
            for (const auto &file : stats) {
                if (!file.is_object() || !file.value("id", json()).is_number_integer() ||
                    !file.value("path", json()).is_string()) continue;
                const int index = file["id"].get<int>();
                const std::string filePath = file["path"].get<std::string>();
                files.emplace_back(baseName(filePath),
                    virtualPath(context, {".p2play", "file", "direct", parts[2],
                                          std::to_string(index), baseName(filePath)}),
                    c2d::Io::Type::File,
                    static_cast<uint64_t>(std::max<int64_t>(0, file.value("length", int64_t(0)))));
            }
            return files;
        }
        if (parts.size() == 2 && parts[1] == "discover") {
            json answer;
            if (!apiJson(root, context, "/api/v1/lists/discover", "GET", noBody(),
                         timeout, &answer, &failure)) {
                fail(failure);
                return files;
            }
            const json list = answer.value("lists", json::array());
            if (!list.is_array()) {
                fail("P2Play discovery response is invalid");
                return files;
            }
            for (const auto &item : list) {
                if (!item.is_object() || !item.value("pubkey", json()).is_string()) {
                    continue;
                }
                const std::string key = item["pubkey"].get<std::string>();
                const json holdersValue = item.value("holders", json(0));
                const int holders = holdersValue.is_number_integer()
                                        ? holdersValue.get<int>()
                                        : holdersValue.is_array() ? static_cast<int>(holdersValue.size()) : 0;
                const std::string name = item.value("name", key.substr(0, 12));
                files.push_back(directory(name + " (" + std::to_string(holders) + ")",
                                          virtualPath(context, {".p2play", "list", key})));
            }
            return files;
        }
        if (parts.size() == 2 && parts[1] == "lists") {
            json answer;
            if (!apiJson(root, context, "/api/v1/lists", "GET", noBody(), timeout, &answer, &failure)) {
                fail(failure);
                return files;
            }
            const json list = answer.value("lists", json::array());
            if (!list.is_array()) {
                fail("P2Play list response is invalid");
                return files;
            }
            for (const auto &item : list) {
                if (!item.is_object() || !item.value("pubkey", json()).is_string()) {
                    continue;
                }
                const std::string key = item["pubkey"].get<std::string>();
                files.push_back(directory(item.value("name", key.substr(0, 12)),
                                          virtualPath(context, {".p2play", "list", key})));
            }
            return files;
        }
        if (parts.size() == 2 && parts[1] == "search") {
            json records;
            if (!searchResults(root, context, {}, {}, timeout, &records, &failure)) {
                fail(failure);
                return files;
            }
            std::set<std::string> tags;
            for (const auto &record : records) {
                const json value = record.value("tags", json::array());
                if (value.is_array()) {
                    for (const auto &tag : value) {
                        if (tag.is_string() && !tag.get<std::string>().empty()) {
                            tags.insert(tag.get<std::string>());
                        }
                    }
                }
            }
            files.push_back(directory("All records", virtualPath(context, {".p2play", "search", "all"})));
            for (const auto &tag : tags) {
                files.push_back(directory(tag, virtualPath(context, {".p2play", "search", "tag", tag})));
            }
            return files;
        }
        if (parts.size() >= 3 && parts[1] == "search") {
            const std::string tag = parts.size() >= 4 && parts[2] == "tag" ? parts[3] : std::string();
            json records;
            if (!searchResults(root, context, {}, tag, timeout, &records, &failure)) {
                fail(failure);
                return files;
            }
            addRecordDirectories(context, records, {}, &files);
            return files;
        }
        if (parts.size() == 3 && parts[1] == "disabled") {
            fail("This item is disabled in its parent meta-list");
            return files;
        }
        if (parts.size() == 3 && parts[1] == "list") {
            const std::string pubkey = parts[2];
            json status;
            if (!statusForList(root, context, pubkey, timeout, &status, &failure)) {
                fail(failure);
                return files;
            }
            if (status.value("type", std::string("catalog")) == "meta") {
                const json items = status.value("items", json::array());
                if (!items.is_array()) {
                    fail("P2Play meta-list response is invalid");
                    return files;
                }
                for (const auto &item : items) {
                    if (!item.is_object() || !item.value("pubkey", json()).is_string()) {
                        continue;
                    }
                    const std::string child = item["pubkey"].get<std::string>();
                    const std::string annotation = item.value("annotation", child.substr(0, 12));
                    const bool enabled = item.value("enabled", false);
                    files.push_back(directory((enabled ? "" : "[disabled] ") + annotation,
                                              virtualPath(context, {".p2play", enabled ? "list" : "disabled", child})));
                }
                return files;
            }
            json records;
            if (!searchResults(root, context, {}, {}, timeout, &records, &failure)) {
                fail(failure);
                return files;
            }
            addRecordDirectories(context, records, pubkey, &files);
            return files;
        }
        if (parts.size() == 3 && parts[1] == "record") {
            json torrent;
            if (!openTorrent(root, context, parts[2], timeout, &torrent, &failure)) {
                fail(failure);
                return files;
            }
            const json fileStats = torrent.value("file_stats", json::array());
            if (!fileStats.is_array()) {
                fail("P2Play torrent response has no file list");
                return files;
            }
            const std::string torrentHash = torrent.value("hash", std::string());
            for (const auto &file : fileStats) {
                if (!file.is_object() || !file.value("id", json()).is_number_integer() ||
                    !file.value("path", json()).is_string()) {
                    continue;
                }
                const int index = file["id"].get<int>();
                const std::string filePath = file["path"].get<std::string>();
                const std::string name = baseName(filePath);
                const auto size = file.value("length", int64_t(0));
                files.emplace_back(name,
                                   virtualPath(context, {".p2play", "file", parts[2], torrentHash,
                                                         std::to_string(index), name}),
                                   c2d::Io::Type::File, static_cast<uint64_t>(std::max<int64_t>(0, size)));
            }
            return files;
        }
        fail("Unknown P2Play virtual path");
        return files;
    }

    std::string toStreamUrl(const std::string &input) {
        const std::string &path = input;
        const std::string context = rootOf(path);
        if (context.empty()) {
            return input;
        }
        const std::string relative = relativeOf(context, path);
        const auto parts = splitPath(relative);
        if (parts.size() < 6 || parts[0] != ".p2play" || parts[1] != "file") {
            return input;
        }
        const std::string root = cleanRoot(context);
        const std::string torrentHash = parts[3];
        const std::string index = parts[4];
        if (torrentHash.empty() || index.empty()) {
            return input;
        }
        const int timeout = pplay::Io::get() != nullptr ? pplay::Io::get()->networkTimeout(path) : 15;
        std::string failure;
        if (!verifyServer(root, context, timeout, &failure)) {
            pplay::Utility::log(pplay::Utility::LogLevel::Error, failure);
            return input;
        }
        const std::string token = streamToken(root, context, timeout);
        std::string url = root + "api/v1/torrents/" + pplay::Http::escape(torrentHash) +
                          "/files/" + index + "/stream";
        if (!token.empty()) url += "?token=" + pplay::Http::escape(token);
        return pplay::Http::normalize(url);
    }

    bool getLibrary(const std::string &contextPath, int timeout, std::vector<LibraryTorrent> *torrents,
                    std::string *error, const LibraryQuery &query) {
        if (!torrents || !error) return false;
        torrents->clear(); error->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) { *error = "P2Play network path is invalid"; return false; }
        if (query.sort != "date" && query.sort != "title" && query.sort != "size") {
            *error = "P2Play library sort must be date, title or size";
            return false;
        }
        if (query.order != "asc" && query.order != "desc") {
            *error = "P2Play library order must be asc or desc";
            return false;
        }
        if (query.offset < 0 || query.limit < 1 || query.limit > 1000) {
            *error = "P2Play library paging values are out of range";
            return false;
        }
        std::string route = "/api/v1/library/torrents?sort=" + pplay::Http::escape(query.sort) +
                            "&order=" + pplay::Http::escape(query.order) +
                            "&offset=" + std::to_string(query.offset) +
                            "&limit=" + std::to_string(query.limit);
        if (!query.query.empty()) route += "&q=" + pplay::Http::escape(query.query);
        if (!query.category.empty()) route += "&category=" + pplay::Http::escape(query.category);
        json answer;
        if (!apiJson(cleanRoot(context), context, route, "GET", noBody(), timeout, &answer, error)) return false;
        const json *items = nullptr;
        if (!answer.is_object() || !arrayAt(answer, "items", &items)) {
            *error = "P2Play v1 library response has no items array";
            return false;
        }
        for (const auto &item : *items) {
            if (!item.is_object() || !item.value("hash", json()).is_string()) continue;
            LibraryTorrent t;
            t.hash = item["hash"].get<std::string>();
            t.title = item.value("title", std::string("Untitled"));
            t.category = item.value("category", std::string());
            t.size = item.value("size", int64_t(0));
            if (item.value("added_at", json()).is_number_integer()) t.addedAt = item["added_at"].get<int64_t>();
            // v1 returns compact library cards, not file_stats/viewed_files. Detailed file
            // status is fetched through the detailed torrent route when a torrent opens.
            if (!t.hash.empty()) torrents->push_back(std::move(t));
        }
        return true;
    }

    bool getLibraryCategories(const std::string &contextPath, int timeout, std::vector<std::string> *categories,
                              std::string *error) {
        if (!categories || !error) return false;
        categories->clear(); error->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) { *error = "P2Play network path is invalid"; return false; }
        json answer;
        if (!apiJson(cleanRoot(context), context, "/api/v1/library/categories", "GET", noBody(),
                     timeout, &answer, error)) return false;
        const json *values = nullptr;
        if (!answer.is_object() || !arrayAt(answer, "categories", &values)) {
            *error = "P2Play v1 library categories response has no categories array";
            return false;
        }
        for (const auto &value : *values) {
            if (value.is_string() && !value.get<std::string>().empty()) categories->push_back(value.get<std::string>());
        }
        return true;
    }

    bool updateTorrent(const std::string &contextPath, const std::string &hash,
                       const std::string &title, bool updateTitle,
                       const std::string &category, bool updateCategory,
                       int timeout, std::string *error) {
        if (!error || hash.empty() || (!updateTitle && !updateCategory)) return false;
        error->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) { *error = "P2Play network path is invalid"; return false; }
        json body = json::object();
        if (updateTitle) body["title"] = title;
        if (updateCategory) body["category"] = category;
        json answer;
        return apiJson(cleanRoot(context), context, "/api/v1/torrents/" + pplay::Http::escape(hash),
                       "PUT", body, timeout, &answer, error);
    }

    bool deleteTorrent(const std::string &contextPath, const std::string &hash, int timeout, std::string *error) {
        if (!error) return false;
        error->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) { *error = "P2Play network path is invalid"; return false; }
        json answer;
        return apiJson(cleanRoot(context), context, "/api/v1/torrents/" + pplay::Http::escape(hash),
                       "DELETE", json::object(), timeout, &answer, error);
    }

    bool addTorrentFile(const std::string &contextPath, const std::string &localFilePath, int timeout,
                        TorrentInfo *torrent, std::string *error) {
        if (!torrent || !error || localFilePath.empty()) return false;
        error->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) { *error = "P2Play network path is invalid"; return false; }

        std::ifstream input(localFilePath, std::ios::binary);
        if (!input) { *error = "Could not open the selected .torrent file"; return false; }
        input.seekg(0, std::ios::end);
        const std::streamoff size = input.tellg();
        if (size <= 0) { *error = "The selected .torrent file is empty or unreadable"; return false; }
        // Leave room for multipart headers and fields under the core's 16 MiB HTTP body limit.
        if (size > 15 * 1024 * 1024) { *error = "The .torrent file must be smaller than 15 MiB"; return false; }
        input.seekg(0, std::ios::beg);
        std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (bytes.size() != static_cast<size_t>(size)) { *error = "Could not read the complete .torrent file"; return false; }

        std::string filename = localFilePath.substr(localFilePath.find_last_of("/\\") == std::string::npos
                                                         ? 0 : localFilePath.find_last_of("/\\") + 1);
        for (char &ch : filename) {
            const unsigned char value = static_cast<unsigned char>(ch);
            if (ch == '"' || ch == '\\' || value < 0x20 || value == 0x7f) ch = '_';
        }
        if (filename.empty()) filename = "upload.torrent";

        const std::string boundary = "pplay-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::string body;
        body.reserve(bytes.size() + 512);
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\n";
        body += "Content-Type: application/x-bittorrent\r\n\r\n";
        body.append(bytes);
        body += "\r\n--" + boundary + "--\r\n";

        json answer;
        if (!apiBody(cleanRoot(context), context, "/api/v1/torrents", "POST", body,
                     "multipart/form-data; boundary=" + boundary, timeout, &answer, error,
                     16u * 1024u * 1024u)) return false;
        const json card = answer.value("torrent", json::object());
        const std::string hash = card.value("hash", std::string());
        if (hash.empty()) { *error = "P2Play did not return a torrent hash after the file upload"; return false; }

        const std::string route = "/api/v1/torrents/" + pplay::Http::escape(hash);
        for (int attempt = 0; attempt < 40; ++attempt) {
            json response;
            if (!apiJson(cleanRoot(context), context, route, "GET", noBody(), timeout, &response, error)) return false;
            const json details = response.value("details", json::object());
            if (parseTorrentInfo(details, torrent)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        *error = "P2Play torrent metadata did not become available after upload";
        return false;
    }

    bool setFileViewed(const std::string &filePath, bool viewed, int timeout, std::string *error) {
        if (!error) return false;
        std::string context, root, hash; int index = -1;
        if (!fileAddressOf(filePath, &context, &root, &hash, &index, error)) return false;
        json answer; const json body = {{"viewed", viewed}};
        return apiJson(root, context, "/api/v1/torrents/" + pplay::Http::escape(hash) + "/files/" + std::to_string(index) + "/viewed", "PUT", body, timeout, &answer, error);
    }

    bool getFileProgress(const std::string &filePath, int timeout, PlaybackProgress *progress, std::string *error) {
        if (!progress || !error) return false;
        *progress = PlaybackProgress();
        std::string context, root, hash; int index = -1;
        if (!fileAddressOf(filePath, &context, &root, &hash, &index, error)) return false;
        json status;
        json response;
        if (!apiJson(root, context, "/api/v1/torrents/" + pplay::Http::escape(hash), "GET", noBody(), timeout, &response, error)) return false;
        status = response.value("details", json::object());
        const json *items = nullptr;
        if (!status.is_object() || !arrayAt(status, "file_progress", &items)) {
            *error = "P2Play torrent response has no file progress array"; return false;
        }
        for (const auto &item : *items) {
            if (!item.is_object() || !item.value("id", json()).is_number_integer() || item["id"].get<int>() != index) continue;
            if (item.value("position", json()).is_number()) progress->position = item["position"].get<double>();
            if (item.value("duration", json()).is_number()) progress->duration = item["duration"].get<double>();
            progress->viewed = item.value("viewed", false);
            break;
        }
        return true;
    }

    bool setFileProgress(const std::string &filePath, double position, double duration, int timeout, std::string *error) {
        if (!error) return false;
        std::string context, root, hash; int index = -1;
        if (!fileAddressOf(filePath, &context, &root, &hash, &index, error)) return false;
        if (position < 0 || duration < 0) { *error = "P2Play progress values must not be negative"; return false; }
        json answer;
        const json body = {{"position", position}, {"duration", duration}};
        return apiJson(root, context, "/api/v1/torrents/" + pplay::Http::escape(hash) + "/files/" +
                       std::to_string(index) + "/progress", "PUT", body, timeout, &answer, error);
    }

    bool getProgress(const std::string &contextPath, int timeout, std::vector<ProgressEntry> *progress,
                    std::string *error) {
        if (progress == nullptr || error == nullptr) return false;
        progress->clear();
        error->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) {
            *error = "P2Play network path is invalid";
            return false;
        }
        json answer;
        if (!apiJson(cleanRoot(context), context, "/api/v1/progress", "GET", noBody(), timeout, &answer, error)) {
            return false;
        }
        const json *items = nullptr;
        if (!answer.is_object() || !arrayAt(answer, "progress", &items)) {
            *error = "P2Play progress response has no progress array";
            return false;
        }
        for (const auto &item : *items) {
            if (!item.is_object() || !item.value("hash", json()).is_string() ||
                !item.value("file_id", json()).is_number_integer() ||
                !item.value("file_name", json()).is_string()) continue;
            ProgressEntry entry;
            entry.hash = item["hash"].get<std::string>();
            entry.fileIndex = item["file_id"].get<int>();
            entry.fileName = item["file_name"].get<std::string>();
            if (entry.hash.empty() || entry.fileIndex < 0 || entry.fileName.empty()) continue;
            if (item.value("title", json()).is_string()) entry.title = item["title"].get<std::string>();
            if (item.value("updated_at", json()).is_string()) entry.updatedAt = item["updated_at"].get<std::string>();
            if (item.value("position", json()).is_number()) entry.position = item["position"].get<double>();
            if (item.value("duration", json()).is_number()) entry.duration = item["duration"].get<double>();
            if (!std::isfinite(entry.position) || !std::isfinite(entry.duration) ||
                entry.position < 0 || entry.duration < 0) continue;
            entry.viewed = item.value("viewed", false);
            progress->push_back(std::move(entry));
        }
        return true;
    }

    bool getUsers(const std::string &contextPath, int timeout, std::vector<ServerUser> *users,
                  int *actorRank, std::string *error) {
        if (users == nullptr || actorRank == nullptr || error == nullptr) return false;
        users->clear();
        *actorRank = 0;
        error->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) {
            *error = "P2Play network path is invalid";
            return false;
        }
        const std::string root = cleanRoot(context);
        json current;
        if (!apiJson(root, context, "/api/v1/auth/me", "GET", noBody(), timeout, &current, error)) return false;
        if (!current.is_object() || !current.value("rank", json()).is_number_integer()) {
            *error = "P2Play current-user response has no rank";
            return false;
        }
        *actorRank = current["rank"].get<int>();

        json answer;
        if (!apiJson(root, context, "/api/v1/users", "GET", noBody(), timeout, &answer, error)) return false;
        const json *items = nullptr;
        if (!answer.is_object() || !arrayAt(answer, "users", &items)) {
            *error = "P2Play user response has no users array";
            return false;
        }
        for (const auto &item : *items) {
            if (!item.is_object() || !item.value("id", json()).is_string() ||
                !item.value("username", json()).is_string() ||
                !item.value("rank", json()).is_number_integer()) continue;
            ServerUser user;
            user.id = item["id"].get<std::string>();
            user.username = item["username"].get<std::string>();
            user.rank = item["rank"].get<int>();
            user.banned = item.value("is_banned", false);
            if (!user.id.empty() && !user.username.empty()) users->push_back(std::move(user));
        }
        return true;
    }

    bool createUser(const std::string &contextPath, const std::string &username, const std::string &password,
                    int rank, int timeout, std::string *error) {
        if (error == nullptr) return false;
        error->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) { *error = "P2Play network path is invalid"; return false; }
        json answer;
        const json body = {{"username", username}, {"password", password}, {"rank", rank}};
        return apiJson(cleanRoot(context), context, "/api/v1/users", "POST", body, timeout, &answer, error);
    }

    bool updateUserPassword(const std::string &contextPath, const std::string &userId,
                            const std::string &password, int timeout, std::string *error) {
        if (error == nullptr) return false;
        error->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) { *error = "P2Play network path is invalid"; return false; }
        json answer;
        const json body = {{"password", password}};
        return apiJson(cleanRoot(context), context, "/api/v1/users/" + pplay::Http::escape(userId),
                       "PUT", body, timeout, &answer, error);
    }

    bool setUserRank(const std::string &contextPath, const std::string &userId,
                     int rank, int timeout, std::string *error) {
        if (error == nullptr) return false;
        error->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) { *error = "P2Play network path is invalid"; return false; }
        json answer;
        const json body = {{"rank", rank}};
        return apiJson(cleanRoot(context), context, "/api/v1/users/" + pplay::Http::escape(userId) + "/rank",
                       "POST", body, timeout, &answer, error);
    }

    bool deleteUser(const std::string &contextPath, const std::string &userId,
                    int timeout, std::string *error) {
        if (error == nullptr) return false;
        error->clear();
        const std::string context = rootOf(contextPath);
        if (context.empty()) { *error = "P2Play network path is invalid"; return false; }
        json answer;
        return apiJson(cleanRoot(context), context, "/api/v1/users/" + pplay::Http::escape(userId),
                       "DELETE", json::object(), timeout, &answer, error);
    }

    void forgetFailures() {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_sessions.clear();
        g_verifiedServers.clear();
        g_torrents.clear();
    }

} // namespace pplay::P2Play
