//
// TorrServe virtual filesystem helpers.
//

#include <algorithm>
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
#include <vector>

#include <json.hpp>
#include "main.h"
#include "io.h"
#include "net_http.h"
#include "torrserve.h"
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

    // ok: answered (an empty list too)
    std::vector<Torrent> getTorrents(const std::string &root, int timeout, bool *ok = nullptr) {
        std::vector<Torrent> torrents;
        if (ok != nullptr) {
            *ok = false;
        }
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
        if (ok != nullptr) {
            *ok = true;
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

    // the worker requests, in order: a body (set / rem) or a list (empty body)
    struct ViewedJob {
        std::string root, hash, body;
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
        double seconds = 0;
    };
    std::mutex g_progressMutex;
    ProgressJob g_progressJob;
    bool g_progressBusy = false;
    pplay::Worker *g_progressWorker = nullptr;
    // server root -> keeps positions (its "track timecode" setting): unknown until checked
    std::unordered_map<std::string, bool> g_timecodes;

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
                                std::string("TorrServe: positions ") + (kept ? "kept" : "not kept") + " by " + job.root);
        }
    }

    std::map<int, double> fetchViewed(const std::string &root, const std::string &hash) {
        const std::string key = root + "|" + hash;
        nlohmann::json request;
        request["action"] = "list";
        request["hash"] = hash;
        nlohmann::json response =
                nlohmann::json::parse(httpRequest(root + "viewed", 5, request.dump()), nullptr, false);
        std::lock_guard<std::mutex> lock(g_viewedMutex);
        g_viewedPending.erase(key);
        Viewed &cached = g_viewed[key];
        // no answer: the marks kept, asked again later
        if (!response.is_discarded() && response.is_array()) {
            cached.files.clear();
            for (const auto &item: response) {
                if (item.contains("file_index") && item["file_index"].is_number_integer()) {
                    double timecode = item.contains("timecode") && item["timecode"].is_number()
                                              ? item["timecode"].get<double>()
                                              : 0;
                    cached.files[item["file_index"].get<int>()] = timecode;
                }
            }
        }
        cached.updated = time(nullptr);
        g_viewedGeneration++;
        return cached.files;
    }

    int viewedThread(void *) {
        while (true) {
            ViewedJob job;
            {
                std::lock_guard<std::mutex> lock(g_viewedMutex);
                if (g_viewedJobs.empty()) {
                    g_viewedBusy = false;
                    return 0;
                }
                job = g_viewedJobs.front();
                g_viewedJobs.pop_front();
            }
            if (job.body.empty()) {
                fetchViewed(job.root, job.hash);
            } else {
                httpRequest(job.root + "viewed", 5, job.body);
            }
        }
    }

    // to the worker: a body and a list after it (its result), or a list only (once at a time)
    void queueViewed(const std::string &root, const std::string &hash, const std::string &body = {}) {
        {
            std::lock_guard<std::mutex> lock(g_viewedMutex);
            bool queued = !g_viewedPending.insert(root + "|" + hash).second;
            if (!body.empty()) {
                g_viewedJobs.push_back({root, hash, body});
            } else if (queued) {
                return;
            }
            g_viewedJobs.push_back({root, hash, {}});
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

    // wait: from a worker (or a load), the server asked right away when needed
    std::map<int, double> getViewed(const std::string &root, const std::string &hash, bool wait) {
        const std::string key = root + "|" + hash;
        std::map<int, double> files;
        {
            std::lock_guard<std::mutex> lock(g_viewedMutex);
            auto it = g_viewed.find(key);
            bool found = it != g_viewed.end();
            if (found && difftime(time(nullptr), it->second.updated) < 30) {
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

    std::vector<c2d::Io::File> getDirList(const std::string &path, int timeout, std::string *error) {
        std::vector<c2d::Io::File> files;
        std::string root = rootOf(path);
        std::string httpRoot = apiRoot(path);
        std::vector<std::string> parts = splitPath(relativeOf(path));
        bool ok = false;
        std::vector<Torrent> torrents = getTorrents(httpRoot, timeout, &ok);
        if (!ok && error) {
            *error = "TorrServer: not reachable";
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
                files.emplace_back(name, virtualPath, c2d::Io::Type::File, (uint64_t) torrentFile.length);
            }
        }
        return files;
    }

    bool getTree(const std::string &path, int timeout, std::vector<c2d::Io::File> *files) {
        std::string root = rootOf(path);
        std::string httpRoot = apiRoot(path);
        std::vector<std::string> parts = splitPath(relativeOf(path));
        bool ok = false;
        std::vector<Torrent> torrents = getTorrents(httpRoot, timeout, &ok);
        if (!ok) {
            return false;
        }
        cacheTorrents(httpRoot, torrents);
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
            return path;
        }

        std::string hash;
        int index;
        if (!fileLink(path, hash, index)) {
            return path;
        }
        return replaceScheme(rootOf(path)) + "stream/?link=" + hash + "&index=" + std::to_string(index) + "&play";
    }

    unsigned int viewedGeneration() {
        std::lock_guard<std::mutex> lock(g_viewedMutex);
        return g_viewedGeneration;
    }

    bool isFileViewed(const std::string &p, bool wait) {
        // a saved copy: its network file
        const std::string path = pplay::Saved::originalOf(p);
        std::string hash;
        int index;
        return isTorrServe(path) && fileLink(path, hash, index) && getViewed(apiRoot(path), hash, wait).count(index) > 0;
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

    void setFileProgress(const std::string &p, double seconds) {
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
        {
            std::lock_guard<std::mutex> lock(g_progressMutex);
            auto support = g_timecodes.find(root);
            if (support != g_timecodes.end() && !support->second) {
                return;
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_viewedMutex);
            auto cached = g_viewed.find(root + "|" + hash);
            if (cached != g_viewed.end()) {
                cached->second.files[index] = seconds;
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_progressMutex);
            g_progressJob = {root, hash, index, seconds};
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
