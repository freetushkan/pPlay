//
// Saved copies (see saved.h). Index lines, tab separated (escaped as the media information):
// "N <network id> <name> <root url>",
// "T <torrent network path> <hash> <title> <poster url> <category> <sizes...> <poster saved>",
// "F <file> <network id> <name> <network path> <size> <done 1/0> <added> <last access>",
// "S <network directory saved whole>".
//

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iterator>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "av_open.h"
#include "i18n.h"
#include "io.h"
#include "media_info.h"
#include "net_http.h"
#include "paths.h"
#include "poster_cache.h"
#include "pplay_config.h"
#include "saved.h"
#include "utility.h"
#include "worker.h"

#ifdef __SMB2__
#include "smb.h"
#endif

#define INDEX_VERSION "pplay-saved 2"
#define INDEX_VERSION_1 "pplay-saved 1"
#define SAVE_DIR ".pplay_save"
#define DAY (24 * 3600)
// kept free on the drive
#define FREE_MARGIN (64LL * 1024 * 1024)
#define CHUNK (512 * 1024)

using namespace pplay;

namespace {

    struct Entry {
        std::string net;
        std::string name;
        std::string remote;
        int64_t size = 0;
        int64_t done = 0;
        time_t added = 0;
        time_t access = 0;
        bool saved = false;
        bool active = false;
        bool failed = false;
        // set: the download stops (the part removed if the entry is gone)
        std::shared_ptr<std::atomic<bool>> cancel;
    };

    struct Network {
        std::string name;
        std::string root;
    };

    struct Torrent {
        TorrServe::Info info;
        bool poster = false;
    };

    struct Job {
        // a directory listed, else a file (its entry)
        bool dir = false;
        c2d::Io::File file;
        std::string key;
    };

    struct SaveThread {
        Worker *worker = nullptr;
        std::atomic<bool> finished{false};
    };

    std::mutex g_mutex;
    PPLAYConfig *g_config = nullptr;
    std::string g_root;
    // by their file name in the root
    std::map<std::string, Entry> g_files;
    std::map<std::string, Network> g_networks;
    // by the torrent network path
    std::map<std::string, Torrent> g_torrents;
    // network path -> file name
    std::map<std::string, std::string> g_byRemote;
    // the directories saved whole (all their files listed then)
    std::set<std::string> g_wholeDirs;
    std::deque<Job> g_jobs;
    std::vector<SaveThread *> g_threads;
    std::atomic<bool> g_stopping{false};
    std::atomic<unsigned int> g_generation{1};
    bool g_dirty = false;
    // the index read (or none yet): else not written over (an unplugged drive back)
    bool g_indexFound = false;
    bool g_accessDirty = false;
    time_t g_writtenAt = 0;
    bool g_cleanup = false;
    std::deque<std::pair<std::string, std::string>> g_messages;

    void message(const std::string &type, const std::string &text) {
        g_messages.emplace_back(type, text);
    }

    void changed() {
        g_generation++;
    }

    // tab separated fields
    std::string escape(const std::string &s) {
        std::string out;
        for (char c: s) {
            out += c == '\\' ? "\\\\" : c == '\t' ? "\\t" : c == '\n' ? "\\n" : std::string(1, c);
        }
        return out;
    }

    std::vector<std::string> fields(const std::string &line) {
        std::vector<std::string> out(1);
        for (size_t i = 0; i < line.size(); i++) {
            char c = line[i];
            if (c == '\t') {
                out.emplace_back();
            } else if (c == '\\' && i + 1 < line.size()) {
                char n = line[++i];
                out.back() += n == 't' ? '\t' : n == 'n' ? '\n' : n;
            } else {
                out.back() += c;
            }
        }
        return out;
    }

    // without its trailing slash ("" kept: c2d's throws on it)
    std::string noSlash(const std::string &path) {
        std::string out = path;
        while (out.size() > 1 && out.back() == '/') {
            out.pop_back();
        }
        return out;
    }

    // path under dir ("" : anything)
    bool under(const std::string &path, const std::string &dir) {
        return dir.empty() || path == dir || (path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0
                                              && path[dir.size()] == '/');
    }

    bool isFile(const std::string &path, int64_t *size = nullptr) {
        struct stat st {};
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            return false;
        }
        if (size != nullptr) {
            *size = (int64_t) st.st_size;
        }
        return true;
    }

    // the parents too ("sdmc:/", "ums0:/" not created)
    bool makeDirs(const std::string &path) {
        for (size_t i = 1; i <= path.size(); i++) {
            if (i == path.size() || path[i] == '/') {
                if (path[i - 1] == ':' || path[i - 1] == '/') {
                    continue;
                }
                mkdir(path.substr(0, i).c_str(), 0777);
            }
        }
        struct stat st {};
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    int64_t freeSpace(const std::string &path) {
        struct statvfs st {};
        if (statvfs(path.c_str(), &st) != 0 || st.f_frsize == 0) {
            return -1;
        }
        return (int64_t) st.f_bavail * (int64_t) st.f_frsize;
    }

    std::string hashOf(const std::string &s) {
        return Utility::md5hash(s).substr(0, 16);
    }

    // a file name of ours: 16 hex, a short ascii extension (a damaged index removes nothing else)
    bool ownName(const std::string &key) {
        if (key.size() < 16) {
            return false;
        }
        for (size_t i = 0; i < 16; i++) {
            if (!std::isxdigit((unsigned char) key[i]) || std::isupper((unsigned char) key[i])) {
                return false;
            }
        }
        if (key.size() == 16) {
            return true;
        }
        if (key[16] != '.' || key.size() > 22 || key.size() < 18) {
            return false;
        }
        for (size_t i = 17; i < key.size(); i++) {
            if (!std::isdigit((unsigned char) key[i]) && !std::islower((unsigned char) key[i])) {
                return false;
            }
        }
        return true;
    }

    // the extension mpv and the players know the file by, ascii only
    std::string extensionOf(const std::string &name) {
        size_t dot = name.rfind('.');
        if (dot == std::string::npos || name.size() - dot < 2 || name.size() - dot > 6) {
            return "";
        }
        std::string ext;
        for (size_t i = dot + 1; i < name.size(); i++) {
            auto c = (unsigned char) name[i];
            if (!std::isalnum(c) || c > 0x7F) {
                return "";
            }
            ext += (char) std::tolower(c);
        }
        return "." + ext;
    }

    std::string posterPath(const std::string &torrent) {
        return g_root + hashOf(torrent) + ".poster";
    }

    std::string vroot() {
        return noSlash(g_root);
    }

    // a Saved section path: <root>/<network id>/<network path>
    bool isVirtual(const std::string &path) {
        std::string root = vroot();
        return !root.empty() && under(path, root);
    }

    // a Saved section path -> its network one ("" at the root, the network root url for a network)
    std::string remoteOfVirtual(const std::string &path) {
        std::string root = vroot();
        if (path.size() <= root.size() + 1) {
            return "";
        }
        std::string rel = path.substr(root.size() + 1);
        size_t slash = rel.find('/');
        auto n = g_networks.find(rel.substr(0, slash));
        if (n == g_networks.end()) {
            return "";
        }
        return slash == std::string::npos ? n->second.root : n->second.root + rel.substr(slash + 1);
    }

    std::string virtualOf(const Entry &e) {
        auto n = g_networks.find(e.net);
        if (n == g_networks.end() || e.remote.size() < n->second.root.size()) {
            return "";
        }
        return vroot() + "/" + e.net + "/" + e.remote.substr(n->second.root.size());
    }

    // the network path a matched one: a Saved section path converted, the trailing slash out
    std::string remoteKey(const std::string &path) {
        std::string remote = isVirtual(path) ? remoteOfVirtual(path) : path;
        return noSlash(remote);
    }

    bool escapedPaths(const std::string &path) {
        Io::DeviceType type = Io::get()->getDeviceType(path);
        return type == Io::DeviceType::TorrServe || type == Io::DeviceType::Http || type == Io::DeviceType::WebDav;
    }

    // a path part as shown
    std::string partName(const std::string &remote, const std::string &part) {
        return escapedPaths(remote) ? Http::unescape(part) : part;
    }

    // the network of a path: its root url, its name
    bool networkOf(const std::string &path, std::string *root, std::string *name) {
        Io *io = Io::get();
        *root = io != nullptr ? io->getNetworkRoot(path) : "";
        if (root->empty() || g_config == nullptr) {
            return false;
        }
        *name = "Network";
        for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
            std::string url = g_config->getOption(PPLAYConfig::networkOption(i))->getString();
            std::replace(url.begin(), url.end(), '\\', '/');
            if (!url.empty() && Paths::ensureTrailingSlash(url) == *root) {
                *name = g_config->getOption(PPLAYConfig::networkNameOption(i))->getString();
                break;
            }
        }
        return true;
    }

    // the entry of a network file (locked)
    bool place(const c2d::Io::File &file, std::string *key) {
        std::string root, network;
        if (!networkOf(file.path, &root, &network) || file.path.size() <= root.size()) {
            return false;
        }
        std::string net = hashOf(root).substr(0, 8);
        g_networks[net] = {network, root};
        std::string leaf = file.path.substr(0, file.path.find('?'));
        leaf = leaf.substr(leaf.rfind('/') + 1);
        std::string name = !file.name.empty() ? file.name : partName(file.path, leaf);
        auto known = g_byRemote.find(file.path);
        *key = known != g_byRemote.end() ? known->second : hashOf(file.path) + extensionOf(name);
        g_byRemote[file.path] = *key;
        Entry &entry = g_files[*key];
        entry.net = net;
        entry.name = name;
        entry.remote = file.path;
        if (file.size > 0) {
            entry.size = (int64_t) file.size;
        }
        if (entry.added == 0) {
            entry.added = entry.access = time(nullptr);
        }
        return true;
    }

    // the torrent of a TorrServer path: its first part under the network
    std::string torrentOf(const std::string &remote) {
        std::string root = Io::get() != nullptr ? Io::get()->getNetworkRoot(remote) : "";
        if (root.empty() || remote.size() <= root.size()) {
            return "";
        }
        size_t slash = remote.find('/', root.size());
        return slash == std::string::npos ? "" : remote.substr(0, slash);
    }

    void readIndex();

    void writeIndex() {
        // an index there now, not read (the drive was missing): kept, the new entries added
        if (!g_indexFound && isFile(g_root + ".index")) {
            auto files = g_files;
            auto networks = g_networks;
            auto torrents = g_torrents;
            auto whole = g_wholeDirs;
            readIndex();
            for (auto &f: files) {
                if (g_files.emplace(f.first, f.second).second) {
                    g_byRemote[f.second.remote] = f.first;
                }
            }
            g_networks.insert(networks.begin(), networks.end());
            g_torrents.insert(torrents.begin(), torrents.end());
            g_wholeDirs.insert(whole.begin(), whole.end());
        }
        g_indexFound = true;
        std::string text = INDEX_VERSION "\n";
        for (auto &n: g_networks) {
            text += "N\t" + escape(n.first) + "\t" + escape(n.second.name) + "\t" + escape(n.second.root) + "\n";
        }
        for (auto &t: g_torrents) {
            auto &i = t.second.info;
            text += "T\t" + escape(t.first) + "\t" + escape(i.hash) + "\t" + escape(i.title) + "\t" + escape(i.poster)
                    + "\t" + escape(i.category) + "\t" + std::to_string((long long) i.torrentSize) + "\t"
                    + std::to_string((long long) i.filesSize) + "\t" + std::to_string(i.fileCount) + "\t"
                    + (t.second.poster ? "1" : "0") + "\n";
        }
        for (auto &d: g_wholeDirs) {
            text += "S\t" + escape(d) + "\n";
        }
        for (auto &f: g_files) {
            auto &e = f.second;
            text += "F\t" + escape(f.first) + "\t" + escape(e.net) + "\t" + escape(e.name) + "\t" + escape(e.remote)
                    + "\t" + std::to_string((long long) e.size) + "\t" + (e.saved ? "1" : "0") + "\t"
                    + std::to_string((long long) e.added) + "\t" + std::to_string((long long) e.access) + "\n";
        }
        if (Io::get() != nullptr && makeDirs(vroot()) && !Io::get()->writeText(g_root + ".index", text)) {
            Utility::log(Utility::LogLevel::Error, "Saved: can't write " + g_root + ".index");
        }
        g_dirty = g_accessDirty = false;
        g_writtenAt = time(nullptr);
    }

    void readTorrent(const std::vector<std::string> &f, Torrent &t) {
        t.info.hash = f[2];
        t.info.title = f[3];
        t.info.poster = f[4];
        t.info.category = f[5];
        t.info.torrentSize = std::atoll(f[6].c_str());
        t.info.filesSize = std::atoll(f[7].c_str());
        t.info.fileCount = (size_t) std::atoll(f[8].c_str());
        t.poster = f[9] == "1";
    }

    // an old index path: in the root only
    bool oldPath(const std::string &rel) {
        if (rel.empty() || rel[0] == '/' || rel.find_first_of("\\:") != std::string::npos) {
            return false;
        }
        for (size_t start = 0;;) {
            size_t end = rel.find('/', start);
            std::string part = rel.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (part.empty() || part == "." || part == "..") {
                return false;
            }
            if (end == std::string::npos) {
                return true;
            }
            start = end + 1;
        }
    }

    // the first versions: a tree of names (unreadable on some cards) moved to the flat files
    void migrate(const std::vector<std::vector<std::string>> &lines) {
        std::map<std::string, std::string> dirRemotes;
        std::vector<std::string> dirs;
        for (auto &f: lines) {
            if (f[0] == "D" && f.size() >= 4 && oldPath(f[1])) {
                dirRemotes[f[1]] = f[3];
                dirs.push_back(f[1]);
                if (f[1].find('/') == std::string::npos) {
                    g_networks[hashOf(f[3]).substr(0, 8)] = {f[2], f[3]};
                }
            }
        }
        int moved = 0, lost = 0;
        for (auto &f: lines) {
            if (f[0] == "T" && f.size() >= 10 && dirRemotes.count(f[1]) > 0) {
                std::string torrent = dirRemotes[f[1]];
                Torrent &t = g_torrents[torrent];
                readTorrent(f, t);
                if (t.poster && rename((g_root + f[1] + "/.poster").c_str(), posterPath(torrent).c_str()) != 0) {
                    t.poster = false;
                }
            } else if (f[0] == "F" && f.size() >= 8 && oldPath(f[1])) {
                std::string root = Io::get()->getNetworkRoot(f[3]);
                std::string net = hashOf(root).substr(0, 8);
                if (root.empty() || g_networks.count(net) == 0) {
                    lost++;
                    continue;
                }
                std::string key = hashOf(f[3]) + extensionOf(f[2]);
                bool saved = f[5] == "1";
                std::string from = g_root + f[1] + (saved ? "" : ".part");
                std::string to = g_root + key + (saved ? "" : ".part");
                if (isFile(from) && rename(from.c_str(), to.c_str()) != 0) {
                    lost++;
                    continue;
                }
                if (saved && !isFile(to)) {
                    lost++;
                    continue;
                }
                Entry &e = g_files[key];
                e.net = net;
                e.name = f[2];
                e.remote = f[3];
                e.size = std::atoll(f[4].c_str());
                e.saved = saved;
                e.added = (time_t) std::atoll(f[6].c_str());
                e.access = (time_t) std::atoll(f[7].c_str());
                g_byRemote[e.remote] = key;
                moved++;
            }
        }
        // the emptied directories, the deepest first (rmdir: only empty ones)
        std::sort(dirs.begin(), dirs.end(), [](const std::string &a, const std::string &b) { return a > b; });
        for (auto &dir: dirs) {
            rmdir((g_root + dir).c_str());
        }
        g_dirty = true;
        Utility::log(Utility::LogLevel::Info, "Saved: index 1 -> 2, " + std::to_string(moved) + " moved, "
                                                      + std::to_string(lost) + " left (unreadable names)");
    }

    void readIndex() {
        g_files.clear();
        g_networks.clear();
        g_torrents.clear();
        g_byRemote.clear();
        g_wholeDirs.clear();
        std::string text;
        // no index in an existing directory: none yet; no directory: maybe not mounted
        struct stat st {};
        g_indexFound = stat(vroot().c_str(), &st) == 0 && !isFile(g_root + ".index");
        if (Io::get() == nullptr || !Io::get()->readText(g_root + ".index", text)) {
            return;
        }
        g_indexFound = true;
        std::vector<std::vector<std::string>> lines;
        std::string version;
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            start = end == std::string::npos ? text.size() : end + 1;
            if (version.empty()) {
                version = line;
                continue;
            }
            std::vector<std::string> f = fields(line);
            if (f.size() >= 2) {
                lines.push_back(f);
            }
        }
        if (version == INDEX_VERSION_1) {
            migrate(lines);
            return;
        }
        if (version != INDEX_VERSION) {
            return;
        }
        for (auto &f: lines) {
            if (f[0] == "S") {
                g_wholeDirs.insert(f[1]);
            } else if (f[0] == "N" && f.size() >= 4) {
                g_networks[f[1]] = {f[2], f[3]};
            } else if (f[0] == "T" && f.size() >= 10) {
                readTorrent(f, g_torrents[f[1]]);
            } else if (f[0] == "F" && f.size() >= 9 && ownName(f[1])) {
                Entry &e = g_files[f[1]];
                e.net = f[2];
                e.name = f[3];
                e.remote = f[4];
                e.size = std::atoll(f[5].c_str());
                e.saved = f[6] == "1";
                e.added = (time_t) std::atoll(f[7].c_str());
                e.access = (time_t) std::atoll(f[8].c_str());
                g_byRemote[e.remote] = f[1];
            }
        }
    }

    // the networks, the torrents (their posters) nothing is saved from removed
    void prune() {
        std::set<std::string> nets, torrents;
        for (auto &f: g_files) {
            nets.insert(f.second.net);
            torrents.insert(torrentOf(f.second.remote));
        }
        for (auto it = g_torrents.begin(); it != g_torrents.end();) {
            if (torrents.count(it->first) == 0) {
                Utility::deleteFile(posterPath(it->first));
                it = g_torrents.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = g_networks.begin(); it != g_networks.end();) {
            it = nets.count(it->first) == 0 ? g_networks.erase(it) : std::next(it);
        }
    }

    // locked
    void removeEntry(const std::string &key) {
        auto it = g_files.find(key);
        if (it == g_files.end() || !ownName(key)) {
            return;
        }
        if (it->second.active) {
            // the thread removes the part
            it->second.cancel->store(true);
        }
        Utility::deleteFile(g_root + key);
        Utility::deleteFile(g_root + key + ".part");
        // its directories not whole any more
        for (auto d = g_wholeDirs.begin(); d != g_wholeDirs.end();) {
            d = under(it->second.remote, *d) ? g_wholeDirs.erase(d) : std::next(d);
        }
        g_byRemote.erase(it->second.remote);
        g_files.erase(it);
        g_jobs.erase(std::remove_if(g_jobs.begin(), g_jobs.end(),
                                    [&key](const Job &job) { return !job.dir && job.key == key; }),
                     g_jobs.end());
    }

    // the oldest last access first: older than the days, over the size (locked)
    int cleanup() {
        if (g_config == nullptr) {
            return 0;
        }
        int days = g_config->getOption(OPT_SAVE_KEEP_DAYS)->getInteger();
        int64_t limit = (int64_t) g_config->getOption(OPT_SAVE_MAX_GB)->getInteger() * 1024 * 1024 * 1024;
        std::vector<std::pair<time_t, std::string>> saved;
        int64_t total = 0;
        for (auto &f: g_files) {
            if (f.second.saved) {
                saved.emplace_back(f.second.access, f.first);
                total += f.second.size;
            } else if (f.second.active) {
                total += f.second.done;
            }
        }
        std::sort(saved.begin(), saved.end());
        time_t old = time(nullptr) - (time_t) days * DAY;
        int removed = 0;
        for (auto &s: saved) {
            bool expired = days > 0 && s.first < old;
            bool over = limit > 0 && total > limit;
            if (!expired && !over) {
                break;
            }
            total -= g_files[s.second].size;
            removeEntry(s.second);
            removed++;
        }
        if (removed > 0) {
            prune();
            g_dirty = true;
            changed();
            Utility::log(Utility::LogLevel::Info, "Saved: cleanup, " + std::to_string(removed) + " removed");
        }
        return removed;
    }

    // the torrent of a TorrServer file kept, its poster saved (a thread)
    void keepTorrent(const std::string &remote) {
        if (Io::get()->getDeviceType(remote) != Io::DeviceType::TorrServe) {
            return;
        }
        TorrServe::Info info;
        std::string torrent = torrentOf(remote);
        if (torrent.empty() || !TorrServe::getInfo(remote, info)) {
            return;
        }
        std::string poster;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            Torrent &t = g_torrents[torrent];
            bool posterKept = t.poster && t.info.poster == info.poster;
            info.fileIndex = -1;
            info.fileName.clear();
            info.fileSize = 0;
            t.info = info;
            t.poster = posterKept;
            g_dirty = true;
            if (posterKept || info.poster.empty()) {
                return;
            }
            poster = info.poster;
        }
        std::string data;
        std::string cached = PosterCache::find(poster);
        if (cached.empty() || !Io::get()->readText(cached, data)) {
            Http::Request request;
            request.url = poster;
            request.timeout = 15;
            request.maxSize = 8 * 1024 * 1024;
            Http::Response response = Http::perform(request);
            if (!response.ok()) {
                return;
            }
            data = std::move(response.body);
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_torrents.find(torrent);
        if (it != g_torrents.end() && Io::get()->writeText(posterPath(torrent), data)) {
            it->second.poster = true;
            g_dirty = true;
        }
    }

    // a directory listed: its files queued (a thread); false on a listing error
    bool expand(const c2d::Io::File &dir, int depth) {
        Io *io = Io::get();
        int timeout = g_config->getOption(OPT_NETWORK_TIMEOUT)->getInteger();
        std::string error;
        std::vector<c2d::Io::File> files = io->list(io->getDeviceType(dir.path), dir.path, timeout, &error);
        if (files.empty() && !error.empty()) {
            std::lock_guard<std::mutex> lock(g_mutex);
            message("Error", tr("Could not save") + " " + dir.name + ": " + error);
            return false;
        }
        bool listed = true;
        for (auto &file: files) {
            if (g_stopping || file.name == "." || file.name == "..") {
                continue;
            }
            if (file.type == c2d::Io::Type::Directory) {
                if (depth < 16) {
                    listed = expand(file, depth + 1) && listed;
                }
            } else if (file.type == c2d::Io::Type::File) {
                std::lock_guard<std::mutex> lock(g_mutex);
                std::string key;
                if (place(file, &key) && !g_files[key].saved && !g_files[key].active) {
                    g_files[key].failed = false;
                    g_jobs.push_back({false, file, key});
                    g_dirty = true;
                }
            }
        }
        if (depth == 0 && listed && !g_stopping) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_wholeDirs.insert(noSlash(dir.path));
            g_dirty = true;
        }
        changed();
        return listed;
    }

    struct Source {
        AvOpen::Stream *av = nullptr;
        void *smb = nullptr;
        int64_t size = -1;
        int64_t offset = 0;

        bool open(const std::string &remote, int64_t from, const std::atomic<bool> *cancel, std::string *error) {
            std::string url = TorrServe::toStreamUrl(remote);
#ifdef __SMB2__
            if (c2d::Utility::startWith(url, "smb://")) {
                smb = Smb::openFile(url, &size);
                if (smb == nullptr) {
                    *error = "can't open";
                    return false;
                }
                offset = from > 0 && Smb::seekFile(smb, from) ? from : 0;
                return true;
            }
#endif
            av = AvOpen::openStream(url, from, cancel, error);
            if (av == nullptr) {
                return false;
            }
            size = AvOpen::size(av);
            offset = AvOpen::offset(av);
            return true;
        }

        int read(uint8_t *buffer, int length, std::string *error) {
#ifdef __SMB2__
            if (smb != nullptr) {
                int64_t res = Smb::readFile(smb, (char *) buffer, (uint64_t) length);
                if (res < 0) {
                    *error = "read error";
                }
                return (int) res;
            }
#endif
            return AvOpen::read(av, buffer, length, error);
        }

        void close() {
#ifdef __SMB2__
            if (smb != nullptr) {
                Smb::closeFile(smb);
                smb = nullptr;
            }
#endif
            AvOpen::close(&av);
        }
    };

    enum class Outcome {
        Done,
        Retry,
        Failed,
        Cancelled
    };

    Outcome download(const std::string &key, const std::string &remote, std::atomic<bool> *cancel,
                     std::string *error) {
        std::string path = g_root + key;
        std::string part = path + ".part";
        if (!makeDirs(vroot())) {
            *error = tr("Can't write to") + " " + g_root;
            return Outcome::Failed;
        }
        int64_t from = 0;
        isFile(part, &from);
        Source source;
        if (!source.open(remote, from, cancel, error)) {
            return *cancel ? Outcome::Cancelled : Outcome::Retry;
        }
        if (source.offset != from) {
            from = 0;
        }
        int64_t size = source.size;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_files.find(key);
            if (it == g_files.end()) {
                source.close();
                return Outcome::Cancelled;
            }
            if (size > 0) {
                it->second.size = size;
            }
            size = it->second.size;
            it->second.done = from;
        }
        int64_t space = freeSpace(vroot());
        if (size > 0 && space >= 0 && space < size - from + FREE_MARGIN) {
            source.close();
            *error = tr("Not enough free space") + " (" + Utility::formatSize((size_t) (size - from)) + ")";
            return Outcome::Failed;
        }
        Utility::log(Utility::LogLevel::Info, "Saved: start " + part + " (" + remote + ") from "
                                              + std::to_string(from) + " of " + std::to_string(size));
        FILE *out = fopen(part.c_str(), from > 0 ? "ab" : "wb");
        if (out == nullptr) {
            source.close();
            *error = tr("Can't write to") + " " + part + ": " + strerror(errno);
            return Outcome::Failed;
        }
        std::vector<uint8_t> buffer(CHUNK);
        int64_t done = from;
        int64_t shown = done;
        Outcome result = Outcome::Done;
        while (true) {
            if (*cancel || g_stopping) {
                result = Outcome::Cancelled;
                break;
            }
            int n = source.read(buffer.data(), CHUNK, error);
            if (n == 0) {
                break;
            }
            if (n < 0) {
                result = *cancel || g_stopping ? Outcome::Cancelled : Outcome::Retry;
                break;
            }
            if (fwrite(buffer.data(), 1, (size_t) n, out) != (size_t) n) {
                int code = errno;
                *error = code == EFBIG || (done + n > 0xFFFFFFFFLL && code != ENOSPC)
                                 ? tr("The file is too big for the file system (FAT32: 4 GB)")
                                 : code == ENOSPC ? tr("Not enough free space") : strerror(code);
                result = Outcome::Failed;
                break;
            }
            done += n;
            // the shown progress: each percent
            if (size <= 0 || done - shown >= size / 100) {
                shown = done;
                std::lock_guard<std::mutex> lock(g_mutex);
                auto it = g_files.find(key);
                if (it != g_files.end()) {
                    it->second.done = done;
                }
                changed();
            }
        }
        source.close();
        if (fclose(out) != 0 && result == Outcome::Done) {
            *error = strerror(errno);
            result = Outcome::Failed;
        }
        if (result == Outcome::Done && size > 0 && done != size) {
            *error = "incomplete (" + std::to_string(done) + "/" + std::to_string(size) + ")";
            result = Outcome::Retry;
        }
        if (result != Outcome::Done) {
            // removed meanwhile, else stopped: kept for the next start
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_files.find(key);
            if (*cancel && (it == g_files.end() || it->second.cancel.get() != cancel)) {
                Utility::deleteFile(part);
            }
            return result;
        }
        Utility::deleteFile(path);
        if (rename(part.c_str(), path.c_str()) != 0) {
            *error = "rename " + part + ": " + strerror(errno);
            return Outcome::Failed;
        }
        // there for real (a size as received)
        int64_t written = -1;
        if (!isFile(path, &written) || (size > 0 && written != size)) {
            *error = "not on disk after writing: " + path + " (" + std::to_string(written) + " bytes)";
            return Outcome::Failed;
        }
        Utility::log(Utility::LogLevel::Info, "Saved: written " + path + " (" + std::to_string(written) + " bytes)");
        return Outcome::Done;
    }

    void saveFile(const Job &job) {
        std::shared_ptr<std::atomic<bool>> cancel;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_files.find(job.key);
            if (it == g_files.end() || it->second.saved || it->second.active) {
                return;
            }
            it->second.active = true;
            it->second.failed = false;
            it->second.cancel = cancel = std::make_shared<std::atomic<bool>>(false);
        }
        changed();
        keepTorrent(job.file.path);

        // the stream marks the file viewed on the server
        bool torrent = Io::get()->getDeviceType(job.file.path) == Io::DeviceType::TorrServe;
        bool viewed = torrent && TorrServe::isFileViewed(job.file.path, true);
        int retries = std::max(1, (int) g_config->getOption(OPT_NETWORK_RETRIES)->getInteger());
        std::string error;
        Outcome result = Outcome::Retry;
        for (int attempt = 0; attempt <= retries && result == Outcome::Retry; attempt++) {
            if (attempt > 0) {
                Utility::log(Utility::LogLevel::Info, "Saved: retry " + job.key + ": " + error);
                for (int i = 0; i < 30 && !*cancel && !g_stopping; i++) {
                    usleep(100000);
                }
            }
            error.clear();
            result = download(job.key, job.file.path, cancel.get(), &error);
        }
        if (torrent && !viewed) {
            TorrServe::remFileViewed(job.file.path);
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_files.find(job.key);
        if (it == g_files.end() || it->second.cancel != cancel) {
            changed();
            return;
        }
        Entry &entry = it->second;
        entry.active = false;
        entry.cancel.reset();
        if (result == Outcome::Done) {
            entry.saved = true;
            isFile(g_root + job.key, &entry.size);
            entry.done = entry.size;
            entry.access = time(nullptr);
            // the network file information, for its Saved section path
            MediaInfo info(job.file);
            if (info.known) {
                info.save(c2d::Io::File(entry.name, virtualOf(entry), c2d::Io::Type::File, (size_t) entry.size));
            }
            g_cleanup = true;
        } else if (result != Outcome::Cancelled) {
            entry.failed = true;
            Utility::log(Utility::LogLevel::Error, "Saved: failed " + job.key + ": " + error);
            message("Error", tr("Could not save") + " " + entry.name + ": " + error);
        }
        g_dirty = true;
        changed();
    }

    int threadMain(void *data) {
        auto *thread = (SaveThread *) data;
        while (!g_stopping) {
            Job job;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                // the listings first: their files then shared by the threads
                auto it = std::find_if(g_jobs.begin(), g_jobs.end(), [](const Job &j) { return j.dir; });
                if (it == g_jobs.end()) {
                    it = g_jobs.begin();
                }
                if (it == g_jobs.end()) {
                    break;
                }
                job = *it;
                g_jobs.erase(it);
            }
            if (job.dir) {
                expand(job.file, 0);
            } else {
                saveFile(job);
            }
        }
        thread->finished = true;
        return 0;
    }

    // locked
    void spawn() {
        int wanted = g_config != nullptr ? g_config->getOption(OPT_SAVE_THREADS)->getInteger() : 1;
        wanted = std::max(1, std::min(4, wanted));
        int running = 0;
        for (auto *t: g_threads) {
            running += t->finished ? 0 : 1;
        }
        for (int i = running; i < wanted && i < (int) g_jobs.size() && !g_stopping; i++) {
            auto *t = new SaveThread();
            g_threads.push_back(t);
            t->worker = new Worker(threadMain, t);
        }
    }

    std::string rootFromConfig() {
#ifdef __ANDROID__
        // in the data directory (<storage>/.pplay, hidden already)
        return Utility::getDataPath() + "saved/";
#else
        std::string home = g_config != nullptr ? g_config->getOption(OPT_HOME_PATH)->getString() : "";
        return Paths::ensureTrailingSlash(home.empty() ? Utility::getDataPath() : home) + SAVE_DIR "/";
#endif
    }

    // entries matching a network path (a file, or all under a directory; "" : all)
    template<typename F>
    void each(const std::string &key, F fn) {
        for (auto &f: g_files) {
            const std::string &r = f.second.remote;
            if (key.empty() || r == key || (r.size() > key.size() && r.compare(0, key.size(), key) == 0
                                            && r[key.size()] == '/')) {
                fn(f.first, f.second);
            }
        }
    }
}

namespace pplay::Saved {

    void start(PPLAYConfig *config) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_config = config;
        g_root = rootFromConfig();
        g_stopping = false;
        // the first versions: a visible directory
        std::string old = vroot();
        old = old.substr(0, old.rfind('/') + 1) + "pplay_save";
        struct stat st {};
        if (stat((old + "/.index").c_str(), &st) == 0 && stat(g_root.c_str(), &st) != 0) {
            rename(old.c_str(), vroot().c_str());
        }
#ifdef __ANDROID__
        // the first Android builds: <home>/.pplay_save
        std::string home = g_config->getOption(OPT_HOME_PATH)->getString();
        std::string first = Paths::ensureTrailingSlash(home) + SAVE_DIR;
        if (!home.empty() && stat((first + "/.index").c_str(), &st) == 0 && stat(g_root.c_str(), &st) != 0) {
            rename(first.c_str(), vroot().c_str());
        }
#endif
        readIndex();
        int resumed = 0;
        for (auto it = g_files.begin(); it != g_files.end();) {
            if (it->second.saved && !isFile(g_root + it->first)) {
                // removed by hand
                g_byRemote.erase(it->second.remote);
                it = g_files.erase(it);
                g_dirty = true;
                continue;
            }
            if (!it->second.saved) {
                c2d::Io::File file(it->second.name, it->second.remote, c2d::Io::Type::File,
                                   (size_t) std::max<int64_t>(0, it->second.size));
                g_jobs.push_back({false, file, it->first});
                resumed++;
            }
            ++it;
        }
        prune();
        cleanup();
        Utility::log(Utility::LogLevel::Info, "Saved: " + g_root + ", " + std::to_string(g_files.size())
                                                      + " files, resumed " + std::to_string(resumed));
        spawn();
    }

    void stop() {
        g_stopping = true;
        std::vector<SaveThread *> threads;
        {
            // stopped, not removed: the parts kept, resumed at the next start
            std::lock_guard<std::mutex> lock(g_mutex);
            for (auto &f: g_files) {
                if (f.second.cancel) {
                    f.second.cancel->store(true);
                }
            }
            threads.swap(g_threads);
        }
        for (auto *t: threads) {
            delete (t->worker);
            delete (t);
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        g_jobs.clear();
        for (auto &f: g_files) {
            f.second.active = false;
            f.second.cancel.reset();
        }
        if (g_dirty || g_accessDirty) {
            writeIndex();
        }
    }

    void update() {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto it = g_threads.begin(); it != g_threads.end();) {
            if ((*it)->finished) {
                delete ((*it)->worker);
                delete (*it);
                it = g_threads.erase(it);
            } else {
                ++it;
            }
        }
        // the home changed: the copies there
        if (g_threads.empty() && g_jobs.empty() && rootFromConfig() != g_root) {
            if (g_dirty || g_accessDirty) {
                writeIndex();
            }
            g_root = rootFromConfig();
            readIndex();
            changed();
        }
        if (g_cleanup) {
            g_cleanup = false;
            cleanup();
        }
        time_t now = time(nullptr);
        if ((g_dirty && now - g_writtenAt >= 2) || (g_accessDirty && now - g_writtenAt >= 60)) {
            writeIndex();
        }
        if (!g_jobs.empty()) {
            spawn();
        }
    }

    void configChanged() {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_cleanup = true;
        spawn();
    }

    bool takeMessage(std::string *type, std::string *text) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_messages.empty()) {
            return false;
        }
        *type = g_messages.front().first;
        *text = g_messages.front().second;
        g_messages.pop_front();
        return true;
    }

    unsigned int generation() {
        return g_generation;
    }

    std::string root() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_root;
    }

    bool busy() {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_jobs.empty()) {
            return true;
        }
        for (auto &f: g_files) {
            if (f.second.active) {
                return true;
            }
        }
        return false;
    }

    bool any() {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto &f: g_files) {
            if (f.second.saved) {
                return true;
            }
        }
        return false;
    }

    bool isInside(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        return isVirtual(path);
    }

    bool isRoot(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        return noSlash(path) == vroot();
    }

    bool canSave(const c2d::Io::File &file) {
        if (file.path.empty() || file.name == ".." || file.path.find("://") == std::string::npos
            || (file.type != c2d::Io::Type::File && file.type != c2d::Io::Type::Directory)) {
            return false;
        }
        return Io::get() != nullptr && !Io::get()->getNetworkRoot(file.path).empty();
    }

    void save(const c2d::Io::File &file) {
        if (!canSave(file)) {
            return;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        // a writable drive (an unplugged one, a read only card)
        std::string probe = g_root + ".write";
        if (!makeDirs(vroot()) || !Io::get()->writeText(probe, "")) {
            message("Error", tr("Can't write to") + " " + g_root);
            return;
        }
        Utility::deleteFile(probe);
        if (file.type == c2d::Io::Type::Directory) {
            g_jobs.push_back({true, file, ""});
        } else {
            std::string key;
            if (!place(file, &key)) {
                return;
            }
            Entry &entry = g_files[key];
            if (entry.saved || entry.active) {
                return;
            }
            entry.failed = false;
            g_jobs.push_back({false, file, key});
            g_dirty = true;
        }
        message("Info", tr("Saving") + ": " + file.name);
        changed();
        spawn();
    }

    State stateOf(const std::string &path) {
        State state;
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string key = remoteKey(path);
        int64_t total = 0, done = 0;
        int known = 0, unknown = 0;
        each(key, [&](const std::string &, const Entry &e) {
            bool pending = !e.saved && (e.active || !e.failed);
            if (e.saved) {
                state.saved++;
            } else if (pending) {
                state.pending++;
            } else {
                state.failed = true;
                return;
            }
            // the progress of the whole directory: the saved ones in it too
            if (e.size > 0) {
                known++;
                total += e.size;
                done += e.saved ? e.size : e.done;
            } else {
                unknown++;
            }
        });
        // the sizes not known yet (listings without them): as the others
        if (unknown > 0 && known > 0) {
            total += total / known * unknown;
        }
        // a directory being listed
        for (auto &job: g_jobs) {
            if (job.dir && !key.empty() && under(noSlash(job.file.path), key)) {
                state.pending++;
            }
        }
        state.progress = state.pending > 0 && total > 0 ? (float) ((double) done / (double) total) : 0;
        // saved whole: it or a directory above
        if (state.pending == 0 && !state.failed && state.saved > 0) {
            for (const std::string &dir: g_wholeDirs) {
                if (under(key, dir)) {
                    state.complete = true;
                    break;
                }
            }
        }
        return state;
    }

    int remove(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string key = remoteKey(path);
        // never all of them (the root, a path of nothing)
        if (key.empty() || key.find("://") == std::string::npos) {
            return 0;
        }
        std::vector<std::string> keys;
        each(key, [&](const std::string &file, const Entry &) { keys.push_back(file); });
        // not whole any more: the directories above, the ones in it
        for (auto it = g_wholeDirs.begin(); it != g_wholeDirs.end();) {
            it = under(key, *it) || under(*it, key) ? g_wholeDirs.erase(it) : std::next(it);
        }
        for (auto &file: keys) {
            removeEntry(file);
        }
        g_jobs.erase(std::remove_if(g_jobs.begin(), g_jobs.end(),
                                    [&key](const Job &job) {
                                        return job.dir && under(noSlash(job.file.path), key);
                                    }),
                     g_jobs.end());
        prune();
        g_dirty = true;
        changed();
        return (int) keys.size();
    }

    std::vector<c2d::Io::File> list(const std::string &dir) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<c2d::Io::File> files;
        std::string v = noSlash(dir);
        std::set<std::string> dirs;
        if (v == vroot()) {
            for (auto &n: g_networks) {
                bool used = false;
                for (auto &f: g_files) {
                    used |= f.second.saved && f.second.net == n.first;
                }
                if (used) {
                    files.emplace_back(n.second.name, v + "/" + n.first, c2d::Io::Type::Directory, 0);
                }
            }
            return files;
        }
        for (auto &f: g_files) {
            if (!f.second.saved) {
                continue;
            }
            std::string path = virtualOf(f.second);
            if (path.size() <= v.size() + 1 || path.compare(0, v.size() + 1, v + "/") != 0) {
                continue;
            }
            std::string rest = path.substr(v.size() + 1);
            size_t slash = rest.substr(0, rest.find('?')).find('/');
            if (slash == std::string::npos) {
                files.emplace_back(f.second.name, path, c2d::Io::Type::File, (size_t) f.second.size);
                continue;
            }
            std::string part = rest.substr(0, slash);
            if (!dirs.insert(part).second) {
                continue;
            }
            std::string remote = remoteOfVirtual(v + "/" + part);
            auto t = g_torrents.find(remote);
            std::string name = t != g_torrents.end() && !t->second.info.title.empty() ? t->second.info.title
                                                                                     : partName(f.second.remote, part);
            files.emplace_back(name, v + "/" + part, c2d::Io::Type::Directory, 0);
        }
        return files;
    }

    bool hasDir(const std::string &dir) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string v = noSlash(dir);
        if (v == vroot()) {
            return true;
        }
        for (auto &f: g_files) {
            if (f.second.saved && c2d::Utility::startWith(virtualOf(f.second), v + "/")) {
                return true;
            }
        }
        return false;
    }

    bool listNetwork(const std::string &dir, std::vector<c2d::Io::File> *files) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string key = noSlash(dir);
        std::set<std::string> dirs;
        bool any = false;
        for (auto &f: g_files) {
            const std::string &r = f.second.remote;
            if (!f.second.saved || r.size() <= key.size() + 1 || r.compare(0, key.size() + 1, key + "/") != 0) {
                continue;
            }
            any = true;
            std::string rest = r.substr(key.size() + 1);
            size_t slash = rest.substr(0, rest.find('?')).find('/');
            if (slash == std::string::npos) {
                files->emplace_back(f.second.name, r, c2d::Io::Type::File, (size_t) f.second.size);
            } else if (dirs.insert(rest.substr(0, slash)).second) {
                files->emplace_back(partName(r, rest.substr(0, slash)), key + "/" + rest.substr(0, slash),
                                    c2d::Io::Type::Directory, 0);
            }
        }
        return any;
    }

    std::vector<std::string> displayParts(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<std::string> parts;
        std::string root = vroot();
        if (!isVirtual(path) || path.size() <= root.size() + 1) {
            return parts;
        }
        std::string rel = path.substr(root.size() + 1);
        size_t slash = rel.find('/');
        auto n = g_networks.find(rel.substr(0, slash));
        if (n == g_networks.end()) {
            return parts;
        }
        parts.push_back(n->second.name);
        std::string remote = n->second.root;
        auto known = g_byRemote.find(remoteOfVirtual(path));
        for (size_t start = slash; start != std::string::npos;) {
            size_t end = rel.find('/', start + 1);
            std::string part = rel.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
            remote = remote.back() == '/' ? remote + part : remote + "/" + part;
            auto t = g_torrents.find(remote);
            if (end == std::string::npos && known != g_byRemote.end()) {
                parts.push_back(g_files[known->second].name);
            } else {
                parts.push_back(t != g_torrents.end() && !t->second.info.title.empty() ? t->second.info.title
                                                                                      : partName(remote, part));
            }
            start = end;
        }
        return parts;
    }

    bool getInfo(const std::string &path, TorrServe::Info &info, std::string *poster) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string remote = remoteOfVirtual(path);
        auto t = g_torrents.find(torrentOf(remote + "/"));
        if (remote.empty() || t == g_torrents.end()) {
            return false;
        }
        info = t->second.info;
        *poster = t->second.poster ? "dir:" + posterPath(t->first) : "";
        auto known = g_byRemote.find(remote);
        if (known != g_byRemote.end()) {
            const Entry &e = g_files[known->second];
            info.fileName = e.name;
            info.fileSize = e.size;
            size_t index = e.remote.find("index=");
            info.fileIndex = index == std::string::npos ? 0 : std::atoi(e.remote.c_str() + index + 6);
        }
        return true;
    }

    std::string originalOf(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!isVirtual(path)) {
            return path;
        }
        std::string remote = remoteOfVirtual(path);
        return remote.empty() ? path : remote;
    }

    std::string localCopy(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto known = g_byRemote.find(isVirtual(path) ? remoteOfVirtual(path) : path);
        if (known == g_byRemote.end()) {
            return "";
        }
        auto f = g_files.find(known->second);
        return f != g_files.end() && f->second.saved ? g_root + known->second : "";
    }

    std::string openUrl(const std::string &path) {
        std::string local = localCopy(path);
        return local.empty() ? TorrServe::toStreamUrl(path) : local;
    }

    void touch(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto known = g_byRemote.find(isVirtual(path) ? remoteOfVirtual(path) : path);
        if (known == g_byRemote.end()) {
            return;
        }
        auto f = g_files.find(known->second);
        if (f == g_files.end() || !f->second.saved) {
            return;
        }
        // a minute precision is enough
        time_t now = time(nullptr);
        if (now - f->second.access >= 60) {
            f->second.access = now;
            g_accessDirty = true;
        }
    }
}
