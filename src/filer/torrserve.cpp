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

#include <curl/curl.h>
#include <json.hpp>
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
    std::vector<TorrentFile> files;
};

std::string replaceScheme(const std::string &path) {
    if (c2d::Utility::startWith(path, "tss://")) {
        return "https://" + path.substr(6);
    }
    if (c2d::Utility::startWith(path, "ts://")) {
        return "http://" + path.substr(5);
    }
    return path;
}

std::string rootOf(const std::string &path) {
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
    size_t scheme = path.find("://");
    if (scheme == std::string::npos) {
        return {};
    }
    size_t slash = path.find('/', scheme + 3);
    if (slash == std::string::npos || slash + 1 >= path.size()) {
        return {};
    }
    std::string relative = path.substr(slash + 1);
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
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c: value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char) c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
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

size_t writeToString(void *ptr, size_t size, size_t count, void *userdata) {
    auto *response = static_cast<std::string *>(userdata);
    response->append(static_cast<char *>(ptr), size * count);
    return size * count;
}

std::string httpRequest(const std::string &url, int timeout, const std::string &postBody = {}) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return {};
    }

    std::string response;
    char errbuf[CURL_ERROR_SIZE];
    std::memset(errbuf, 0, sizeof(errbuf));

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
#if defined(__PS4__) || defined(__PS5__)
    curl_easy_setopt(curl, CURLOPT_CAINFO, pplay::Utility::getCertificatesPath().c_str());
#endif

    struct curl_slist *headers = nullptr;
    if (!postBody.empty()) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) postBody.length());
    }

    pplay::Utility::log(pplay::Utility::LogLevel::Info, "TorrServe::request url=" + url);
    CURLcode result = curl_easy_perform(curl);
    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        pplay::Utility::log(pplay::Utility::LogLevel::Error,
                            std::string("TorrServe::request failed=") + curl_easy_strerror(result)
                            + ", errbuf=" + errbuf);
        return {};
    }
    return response;
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
        if (!item.contains("title") || !item["title"].is_string()
            || !item.contains("hash") || !item["hash"].is_string()) {
            continue;
        }

        Torrent torrent;
        torrent.title = item["title"].get<std::string>();
        torrent.hash = item["hash"].get<std::string>();
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
                    torrent.files.push_back(tf);
                }
            }
        }
        torrents.push_back(torrent);
    }
    return torrents;
}

bool hasPrefix(const std::vector<std::string> &path, const std::vector<std::string> &prefix) {
    if (path.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); i++) {
        if (path[i] != prefix[i]) return false;
    }
    return true;
}

std::string joinVirtual(const std::string &root, const std::vector<std::string> &parts) {
    std::string out = root;
    for (size_t i = 0; i < parts.size(); i++) {
        if (!c2d::Utility::endsWith(out, "/")) out += "/";
        out += escapeSegment(parts[i]);
    }
    return out;
}

std::string basename(const std::string &path) {
    size_t pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

}

namespace pplay::TorrServe {

std::set<int> getViewedRemote(const std::string &root, const std::string &hash, int timeout) {
    std::set<int> viewed;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "TorrServe::getViewedRemote hash=" + hash);
    nlohmann::json request;
    request["action"] = "list";
    request["hash"] = hash;
    nlohmann::json response = nlohmann::json::parse(
            httpRequest(root + "viewed", timeout, request.dump()), nullptr, false);
    if (response.is_discarded() || !response.is_array()) {
        return viewed;
    }
    for (const auto &item: response) {
        if (item.contains("file_index") && item["file_index"].is_number_integer()) {
            viewed.insert(item["file_index"].get<int>());
        }
    }
    forceViewedRefresh = false;
    return viewed;
}

std::set<int> getViewedCached(const std::string &root, const std::string &hash) {
    struct CacheEntry {
        std::set<int> viewed;
        time_t updated;
    };

    static std::mutex cacheMutex;
    static std::unordered_map<std::string, CacheEntry> cache;
    const std::string key = root + "|" + hash;

    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "TorrServe::getViewedCached key=" + key);

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cache.find(key);
        if (it != cache.end()) {
            double age = difftime(time(nullptr), it->second.updated);
            pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                "TorrServe::getViewedCached got age=" + std::to_string((int)age));
            if (age < 30 && !forceViewedRefresh) {
                return it->second.viewed;
            }
        }
    }

    std::set<int> viewed = getViewedRemote(root, hash, 5);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache[key] = CacheEntry{viewed, time(nullptr)};
    }
    return viewed;
}

std::set<int> getViewed(const std::string &root, const std::string &hash) {
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "TorrServe::getViewed hash=" + hash + " forced=" + std::to_string(forceViewedRefresh));
    return getViewedCached(root, hash);
}

std::vector<c2d::Io::File> getDirList(Browser *browser, const std::string &path, int timeout) {
    std::vector<c2d::Io::File> files;
    std::string root = rootOf(path);
    std::string httpRoot = apiRoot(path);
    std::vector<std::string> parts = splitPath(relativeOf(path));
    (void) browser;
    std::vector<Torrent> torrents = getTorrents(httpRoot, timeout);

    if (parts.empty()) {
        for (const auto &torrent: torrents) {
            files.emplace_back(torrent.title,
                root + escapeSegment(torrent.title), c2d::Io::Type::Directory);
        }
        return files;
    }

    auto torrentIt = std::find_if(torrents.begin(), torrents.end(), [&parts](const Torrent &torrent) {
        return torrent.title == parts[0];
    });
    if (torrentIt == torrents.end()) {
        return getDirList(browser, root, timeout);
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
            std::string virtualPath = joinVirtual(root, virtualParts)
                                      + "?link=" + escapeSegment(torrentIt->hash)
                                      + "&index=" + std::to_string(torrentFile.id)
                                      + "&dummy=" + escapeSegment(basename(torrentFile.path));
            files.emplace_back(name, virtualPath, c2d::Io::Type::File, torrentFile.length);
        }
    }
    return files;
}

std::string toStreamUrl(const std::string &path) {
    if (!c2d::Utility::startWith(path, "ts://") && !c2d::Utility::startWith(path, "tss://")) {
        return path;
    }

    size_t query = path.find('?');
    if (query == std::string::npos) {
        return path;
    }

    std::string link;
    std::string index;
    std::stringstream ss(path.substr(query + 1));
    std::string param;
    while (std::getline(ss, param, '&')) {
        size_t eq = param.find('=');
        std::string key = eq == std::string::npos ? param : param.substr(0, eq);
        std::string value = eq == std::string::npos ? "" : param.substr(eq + 1);
        if (key == "link") link = value;
        if (key == "index") index = value;
    }
    if (link.empty() || index.empty()) {
        return path;
    }

    return replaceScheme(rootOf(path)) + "stream/?link=" + link + "&index=" + index + "&play";
}

bool isFileViewed(const std::string &path) {
    if (!c2d::Utility::startWith(path, "ts://") && !c2d::Utility::startWith(path, "tss://")) {
        return false;
    }

    size_t query = path.find('?');
    if (query == std::string::npos) {
        return false;
    }

    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "TorrServe::isFileViewed started path=" + path);
    std::string hash;
    std::string index_str;
    std::stringstream ss(path.substr(query + 1));
    std::string param;
    while (std::getline(ss, param, '&')) {
        size_t eq = param.find('=');
        std::string key = eq == std::string::npos ? param : param.substr(0, eq);
        std::string value = eq == std::string::npos ? "" : param.substr(eq + 1);
        if (key == "link") hash = value;
        if (key == "index") index_str = value;
    }
    if (hash.empty() || index_str.empty()) {
        return false;
    }

    char *end = nullptr;
    long parsedIndex = std::strtol(index_str.c_str(), &end, 10);
    if (end == index_str.c_str() || *end != '\0') {
        return false;
    }
    int index = static_cast<int>(parsedIndex);
    std::set<int> viewed = getViewed(apiRoot(path), hash);
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "TorrServe::isFileViewed finished path=" + path
            + " root=" + apiRoot(path) + " index=" + index_str);
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "TorrServe::isFileViewed finished path=" + path
            + " result=" + std::to_string(viewed.count(index) > 0));
    return viewed.count(index) > 0;
}

bool remFileViewed(const std::string &path) {
    if (!c2d::Utility::startWith(path, "ts://") && !c2d::Utility::startWith(path, "tss://")) {
        return false;
    }

    size_t query = path.find('?');
    if (query == std::string::npos) {
        return false;
    }

    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "TorrServe::remFileViewed path=" + path);
    std::string hash;
    std::string index_str;
    std::stringstream ss(path.substr(query + 1));
    std::string param;
    while (std::getline(ss, param, '&')) {
        size_t eq = param.find('=');
        std::string key = eq == std::string::npos ? param : param.substr(0, eq);
        std::string value = eq == std::string::npos ? "" : param.substr(eq + 1);
        if (key == "link") hash = value;
        if (key == "index") index_str = value;
    }
    if (hash.empty() || index_str.empty()) {
        return false;
    }

    char *end = nullptr;
    long parsedIndex = std::strtol(index_str.c_str(), &end, 10);
    if (end == index_str.c_str() || *end != '\0') {
        return false;
    }
    nlohmann::json request;
    request["action"] = "rem";
    request["hash"] = hash;
    request["file_index"] = static_cast<int>(parsedIndex);
    httpRequest(apiRoot(path) + "viewed", 5, request.dump());
    forceViewedRefresh = true;
    getViewed(apiRoot(path), hash);
    return true;
}

}
