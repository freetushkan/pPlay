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
    // the torrents list and the viewed marks asked again after (s, the setting)
    std::atomic<int> g_syncSecs{30};

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

    // the torrents list per server: asked once in a sync interval (all the listings, the
    // folder marks, the details panel), a failure not asked again for a few seconds
    std::mutex g_cacheMutex;
    std::unordered_map<std::string, std::vector<Torrent>> g_torrentsCache;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_torrentsAt;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_torrentsFailedAt;
    // the servers not answering their last list request
    std::unordered_set<std::string> g_unreachable;
    // one request at a time (the others wait for its answer)
    std::mutex g_fetchMutex;
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
        *failed = failure != g_torrentsFailedAt.end() && now - failure->second < std::chrono::seconds(FailedRetrySecs);
        auto at = g_torrentsAt.find(root);
        if (at == g_torrentsAt.end() || now - at->second >= std::chrono::seconds(g_syncSecs.load())) {
            return false;
        }
        *torrents = g_torrentsCache[root];
        return true;
    }

    std::vector<Torrent> torrentsOf(const std::string &root, int timeout, bool *ok) {
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
        std::lock_guard<std::mutex> fetch(g_fetchMutex);
        // asked by another thread meanwhile
        if (cachedTorrents(root, &torrents, &failed)) {
            *ok = true;
            return torrents;
        }
        if (failed) {
            return torrents;
        }
        torrents = getTorrents(root, timeout, ok);
        std::lock_guard<std::mutex> lock(g_cacheMutex);
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
            g_unreachable.insert(root);
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
                            "TorrServe: " + std::to_string(count) + " viewed lists from the last run");
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

    // all the viewed files of a server at once (one request a sync interval, not one a torrent):
    // the torrents without any kept empty, fresh too
    std::map<int, double> fetchViewed(const std::string &root, const std::string &hash) {
        const std::string key = root + "|" + hash;
        nlohmann::json request;
        request["action"] = "list";
        g_viewedFetching++;
        nlohmann::json response =
                nlohmann::json::parse(httpRequest(root + "viewed", 5, request.dump()), nullptr, false);
        g_viewedFetching--;
        std::vector<std::string> hashes;
        {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
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
            }
            if (job.body.empty()) {
                // answered meanwhile (all of a server at once)
                bool fresh;
                {
                    std::lock_guard<std::mutex> lock(g_viewedMutex);
                    auto it = g_viewed.find(job.root + "|" + job.hash);
                    fresh = it != g_viewed.end() && difftime(time(nullptr), it->second.updated) < g_syncSecs;
                    if (fresh) {
                        g_viewedPending.erase(job.root + "|" + job.hash);
                    }
                }
                if (!fresh) {
                    fetchViewed(job.root, job.hash);
                }
            } else {
                httpRequest(job.root + "viewed", 5, job.body);
            }
        }
        saveViewed();
        return 0;
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
            if (found && difftime(time(nullptr), it->second.updated) < g_syncSecs) {
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
        std::vector<Torrent> torrents = torrentsOf(httpRoot, timeout, &ok);
        if (!ok && error) {
            *error = "TorrServer: not reachable";
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
        std::vector<Torrent> torrents = torrentsOf(httpRoot, timeout, &ok);
        if (!ok) {
            return false;
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
            return path;
        }

        std::string hash;
        int index;
        if (!fileLink(path, hash, index)) {
            return path;
        }
        return replaceScheme(rootOf(path)) + "stream/?link=" + hash + "&index=" + std::to_string(index) + "&play";
    }

    namespace {
        struct LinkParts {
            std::string user, host, base, hash;
            int index = -1;
        };

        // scheme://[user[:password]@]host[:port]/<base>[stream...|play/...]
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
            if (play != std::string::npos) {
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
        return !link.hash.empty() && !link.host.empty() && link.host == net.host && link.base == net.base
               && (link.user.empty() || link.user == net.user);
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
        return g_viewedFetching > 0;
    }

    void saveViewedCache() {
        saveViewed();
    }

    void setSyncInterval(int seconds) {
        g_syncSecs = std::max(10, seconds);
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
            g_viewedDirty = true;
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
