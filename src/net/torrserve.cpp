//
// TorrServe virtual filesystem helpers.
//

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <set>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <json.hpp>
#include "main.h"
#include "io.h"
#include "net_http.h"
#include "torrserve.h"
#include "utility.h"

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
                    "TorrServe::request " + url + " failed: "
                            + (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error));
            return {};
        }
        return response.body;
    }

    std::vector<Torrent> getTorrents(const std::string &root, int timeout) {
        std::vector<Torrent> torrents;
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
                            "TorrServe::getTorrents url=" + root + "torrents, timeout=" + std::to_string(timeout));
        std::string response = httpRequest(root + "torrents", timeout, "{\"action\":\"list\"}");
        if (response.empty()) {
            return torrents;
        }

        pplay::Utility::log(pplay::Utility::LogLevel::Trace, "TorrServe::getTorrents response=" + response);
        nlohmann::json json = nlohmann::json::parse(response, nullptr, false);
        if (json.is_discarded() || !json.is_array()) {
            return torrents;
        }

        for (const auto &item: json) {
            if (!item.contains("title") || !item["title"].is_string() || !item.contains("hash")
                || !item["hash"].is_string()) {
                continue;
            }

            Torrent torrent;
            torrent.title = item["title"].get<std::string>();
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
                                "TorrServe::getTorrents item=" + torrent.title + ", hash=" + torrent.hash);

            if (item.contains("data") && item["data"].is_string()) {
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
            torrents.push_back(torrent);
        }
        return torrents;
    }

    // last torrents list per server (details panel)
    std::mutex g_cacheMutex;
    std::unordered_map<std::string, std::vector<Torrent>> g_torrentsCache;

    void cacheTorrents(const std::string &root, const std::vector<Torrent> &torrents) {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_torrentsCache[root] = torrents;
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

    // viewed files of a torrent, cached a bit (the filer asks for each item)
    std::mutex g_viewedMutex;
    struct Viewed {
        std::set<int> files;
        time_t updated = 0;
    };
    std::unordered_map<std::string, Viewed> g_viewed;
    bool g_viewedDirty = false;

    std::set<int> getViewed(const std::string &root, const std::string &hash) {
        const std::string key = root + "|" + hash;
        {
            std::lock_guard<std::mutex> lock(g_viewedMutex);
            auto it = g_viewed.find(key);
            if (it != g_viewed.end() && !g_viewedDirty && difftime(time(nullptr), it->second.updated) < 30) {
                return it->second.files;
            }
        }

        std::set<int> viewed;
        nlohmann::json request;
        request["action"] = "list";
        request["hash"] = hash;
        nlohmann::json response =
                nlohmann::json::parse(httpRequest(root + "viewed", 5, request.dump()), nullptr, false);
        if (!response.is_discarded() && response.is_array()) {
            for (const auto &item: response) {
                if (item.contains("file_index") && item["file_index"].is_number_integer()) {
                    viewed.insert(item["file_index"].get<int>());
                }
            }
        }
        std::lock_guard<std::mutex> lock(g_viewedMutex);
        g_viewedDirty = false;
        g_viewed[key] = Viewed{viewed, time(nullptr)};
        return viewed;
    }

}

namespace pplay::TorrServe {

    std::vector<c2d::Io::File> getDirList(const std::string &path, int timeout, std::string *error) {
        std::vector<c2d::Io::File> files;
        std::string root = rootOf(path);
        std::string httpRoot = apiRoot(path);
        std::vector<std::string> parts = splitPath(relativeOf(path));
        std::vector<Torrent> torrents = getTorrents(httpRoot, timeout);
        if (torrents.empty() && error) {
            *error = "TorrServer: no torrent (or server not reachable)";
        }
        cacheTorrents(httpRoot, torrents);

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
                files.emplace_back(name, virtualPath, c2d::Io::Type::File, (size_t) torrentFile.length);
            }
        }
        return files;
    }

    std::string toStreamUrl(const std::string &path) {
        if (!isTorrServe(path)) {
            auto *io = pplay::Io::get();
            auto type = io != nullptr ? io->getDeviceType(path) : pplay::Io::DeviceType::Local;
            if (type == pplay::Io::DeviceType::Http || type == pplay::Io::DeviceType::WebDav) {
                return pplay::Http::normalize(path);
            }
            return path;
        }

        std::string hash;
        int index;
        if (!fileLink(path, hash, index)) {
            return path;
        }
        return replaceScheme(rootOf(path)) + "stream/?link=" + hash + "&index=" + std::to_string(index) + "&play";
    }

    bool isFileViewed(const std::string &path) {
        std::string hash;
        int index;
        return isTorrServe(path) && fileLink(path, hash, index) && getViewed(apiRoot(path), hash).count(index) > 0;
    }

    bool remFileViewed(const std::string &path) {
        std::string hash;
        int index;
        if (!isTorrServe(path) || !fileLink(path, hash, index)) {
            return false;
        }
        nlohmann::json request;
        request["action"] = "rem";
        request["hash"] = hash;
        request["file_index"] = index;
        httpRequest(apiRoot(path) + "viewed", 5, request.dump());
        {
            std::lock_guard<std::mutex> lock(g_viewedMutex);
            g_viewedDirty = true;
        }
        getViewed(apiRoot(path), hash);
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
