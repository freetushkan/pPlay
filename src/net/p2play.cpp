//
// P2Play MediaMesh network client.
//
// This file owns its complete P2Play API adapter. It deliberately does not include,
// call, or share implementation files with the TorrServer adapter in torrserve.*.
//

#include <algorithm>
#include <chrono>
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
#include "saved.h"
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

    json noBody() {
        return json(json::value_t::discarded);
    }

    std::string cleanRoot(const std::string &root) {
        std::string value = pplay::PPLAYConfig::withoutCredentials(root);
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
                                      const std::string &body, int timeout, const std::string &token) {
        std::string path = route;
        while (!path.empty() && path.front() == '/') {
            path.erase(path.begin());
        }
        pplay::Http::Request request;
        request.url = pplay::Http::normalize(root + path);
        request.method = method;
        request.body = body;
        request.timeout = std::max(1, timeout);
        request.maxSize = 32u * 1024u * 1024u;
        request.context = context;
        request.useNetworkCredentials = false;
        request.headers.emplace_back("Accept: application/json");
        if (!body.empty() || method == "POST" || method == "PUT") {
            request.headers.emplace_back("Content-Type: application/json");
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
        const auto response = sendRequest(root, context, "/api/auth/login", "POST", body.dump(), timeout, {});
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
        const auto response = sendRequest(root, context, "/api/p2play/version", "GET", {}, timeout, {});
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

    bool apiJson(const std::string &root, const std::string &context, const std::string &route,
                 const std::string &method, const json &body, int timeout, json *out, std::string *error) {
        if (!verifyServer(root, context, timeout, error)) {
            return false;
        }
        const std::string requestBody = body.is_discarded() ? std::string() : body.dump();
        std::string token = cachedToken(root, context);
        auto response = sendRequest(root, context, route, method, requestBody, timeout, token);
        if (response.status == 401) {
            token.clear();
            if (!login(root, context, timeout, &token, error)) {
                return false;
            }
            response = sendRequest(root, context, route, method, requestBody, timeout, token);
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

    bool arrayAt(const json &object, const char *key, const json **array) {
        auto it = object.find(key);
        if (it == object.end() || !it->is_array()) {
            return false;
        }
        *array = &*it;
        return true;
    }

    std::string baseName(const std::string &path) {
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
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
                       const std::string &tag, int timeout, json *records, std::string *error) {
        std::string route = "/api/catalog/search?limit=500";
        if (!query.empty()) {
            route += "&q=" + pplay::Http::escape(query);
        }
        if (!tag.empty()) {
            route += "&tag=" + pplay::Http::escape(tag);
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
        const std::string route = "/api/lists/" + pplay::Http::escape(pubkey) + "/status";
        if (!apiJson(root, context, route, "GET", noBody(), timeout, status, error)) {
            return false;
        }
        if (!status->contains("name")) {
            json ignored;
            if (!apiJson(root, context, "/api/lists/" + pplay::Http::escape(pubkey), "POST",
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
            if (!apiJson(root, context, "/api/catalog/" + pplay::Http::escape(recordHash),
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
            json addBody = {
                    {"link", link},
                    {"title", record.value("title", std::string())},
                    {"poster", record.value("poster", std::string())},
                    {"category", category},
                    {"save_to_db", true}};
            json added;
            if (!apiJson(root, context, "/api/torrents", "POST", addBody, timeout, &added, error)) {
                return false;
            }
            torrentHash = added.value("hash", std::string());
            if (torrentHash.empty()) {
                *error = "P2Play did not return a torrent hash after adding the catalog record";
                return false;
            }
            std::lock_guard<std::mutex> lock(g_mutex);
            g_torrents[key] = torrentHash;
        }

        const std::string statusRoute = "/api/torrents/" + pplay::Http::escape(torrentHash);
        for (int attempt = 0; attempt < 40; ++attempt) {
            if (!apiJson(root, context, statusRoute, "GET", noBody(), timeout, torrent, error)) {
                return false;
            }
            const json fileStats = torrent->value("file_stats", json::array());
            if (fileStats.is_array() && !fileStats.empty()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        *error = "P2Play torrent metadata did not become available";
        return false;
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
        const std::vector<std::string> parts = splitPath(relative);
        auto fail = [&](const std::string &message) {
            if (error != nullptr) {
                *error = message;
            }
        };

        if (parts.empty()) {
            files.push_back(directory("Discover", virtualPath(context, {".p2play", "discover"})));
            files.push_back(directory("My lists", virtualPath(context, {".p2play", "lists"})));
            files.push_back(directory("Search by tag", virtualPath(context, {".p2play", "search"})));
            return files;
        }
        if (parts[0] != ".p2play") {
            fail("Unknown P2Play virtual path");
            return files;
        }
        if (parts.size() == 2 && parts[1] == "discover") {
            json answer;
            if (!apiJson(root, context, "/api/lists/discover", "GET", noBody(),
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
            if (!apiJson(root, context, "/api/lists", "GET", noBody(), timeout, &answer, &failure)) {
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
        const std::string path = pplay::Saved::originalOf(input);
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
        std::string url = root + "api/stream/" + pplay::Http::escape(torrentHash) + "/" + index;
        if (!token.empty()) {
            url += "?token=" + pplay::Http::escape(token);
        }
        return pplay::Http::normalize(url);
    }

    void forgetFailures() {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_sessions.clear();
        g_verifiedServers.clear();
        g_torrents.clear();
    }

} // namespace pplay::P2Play
