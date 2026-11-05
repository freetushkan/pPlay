//
// Saved copies (see saved.h). Index lines, tab separated (escaped as the media information):
// "D <dir> <name> <remote>", "T <dir> <hash> <title> <poster url> <category> <sizes...> <poster saved>",
// "F <file> <name> <remote> <size> <done 1/0> <added> <last access>".
//

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <dirent.h>
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

#define INDEX_VERSION "pplay-saved 1"
#define SAVE_DIR "pplay_save"
#define DAY (24 * 3600)
// a name part longer is cut (some file systems: 255 bytes for the name, 260 for the path)
#define NAME_MAX_BYTES 80
// kept free on the drive
#define FREE_MARGIN (64LL * 1024 * 1024)
#define CHUNK (512 * 1024)

using namespace pplay;

namespace {

    struct Entry {
        std::string name;
        std::string remote;
        int64_t size = 0;
        int64_t done = 0;
        time_t added = 0;
        time_t access = 0;
        bool saved = false;
        bool active = false;
        bool failed = false;
        // set: the download stops (the part removed)
        std::shared_ptr<std::atomic<bool>> cancel;
    };

    struct Dir {
        std::string name;
        std::string remote;
    };

    struct Torrent {
        TorrServe::Info info;
        bool poster = false;
    };

    struct Job {
        // a directory listed, else a file (its entry)
        bool dir = false;
        c2d::Io::File file;
        std::string local;
    };

    struct SaveThread {
        Worker *worker = nullptr;
        std::atomic<bool> finished{false};
    };

    std::mutex g_mutex;
    PPLAYConfig *g_config = nullptr;
    std::string g_root;
    // by the relative local path
    std::map<std::string, Entry> g_files;
    std::map<std::string, Dir> g_dirs;
    std::map<std::string, Torrent> g_torrents;
    // lower case local path -> remote (case insensitive file systems)
    std::map<std::string, std::string> g_names;
    std::deque<Job> g_jobs;
    std::vector<SaveThread *> g_threads;
    std::atomic<bool> g_stopping{false};
    std::atomic<unsigned int> g_generation{1};
    bool g_dirty = false;
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

    std::string lower(const std::string &s) {
        return c2d::Utility::toLower(s);
    }

    std::string parentOf(const std::string &rel) {
        size_t slash = rel.rfind('/');
        return slash == std::string::npos ? "" : rel.substr(0, slash);
    }

    bool under(const std::string &rel, const std::string &dir) {
        return dir.empty() || rel == dir || (rel.size() > dir.size() && rel.compare(0, dir.size(), dir) == 0
                                             && rel[dir.size()] == '/');
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

    // UTF-8 safe cut
    std::string cut(const std::string &s, size_t max) {
        if (s.size() <= max) {
            return s;
        }
        size_t end = max;
        while (end > 0 && ((unsigned char) s[end] & 0xC0) == 0x80) {
            end--;
        }
        return s.substr(0, end);
    }

    // a name any file system takes, the same for the same name (subtitles found by the
    // video name); the collisions: see uniqueName()
    std::string safeName(const std::string &name, bool file) {
        std::string out;
        for (char c: name) {
            auto u = (unsigned char) c;
            out += u < 0x20 || std::strchr("\\/:*?\"<>|", c) != nullptr ? '_' : c;
        }
        while (!out.empty() && out.front() == ' ') {
            out.erase(0, 1);
        }
        while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
            out.pop_back();
        }
        if (!out.empty() && out.front() == '.') {
            // hidden, as the index
            out[0] = '_';
        }
        std::string ext;
        size_t dot = out.rfind('.');
        if (file && dot != std::string::npos && dot > 0 && out.size() - dot <= 6) {
            ext = out.substr(dot);
            out.erase(dot);
        }
        std::string stem = cut(out, NAME_MAX_BYTES);
        return (stem.empty() ? "_" : stem) + ext;
    }

    // the unique local name in a directory (a case insensitive one too)
    std::string uniqueName(const std::string &parent, const std::string &name, const std::string &remote) {
        std::string rel = parent.empty() ? name : parent + "/" + name;
        auto it = g_names.find(lower(rel));
        if (it == g_names.end() || it->second == remote) {
            return rel;
        }
        std::string hashed = name + "~" + Utility::md5hash(remote).substr(0, 6);
        size_t dot = name.rfind('.');
        if (dot != std::string::npos && dot > 0) {
            hashed = name.substr(0, dot) + "~" + Utility::md5hash(remote).substr(0, 6) + name.substr(dot);
        }
        return parent.empty() ? hashed : parent + "/" + hashed;
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

    bool escapedPaths(const std::string &path) {
        Io::DeviceType type = Io::get()->getDeviceType(path);
        return type == Io::DeviceType::TorrServe || type == Io::DeviceType::Http || type == Io::DeviceType::WebDav;
    }

    // the local file of a network one, its directories added (locked)
    bool place(const c2d::Io::File &file, std::string *local) {
        std::string root, network;
        if (!networkOf(file.path, &root, &network) || file.path.size() <= root.size()) {
            return false;
        }
        // the network directory
        std::string dir = uniqueName("", safeName(network.empty() ? "Network" : network, false), root);
        if (g_dirs.count(dir) == 0) {
            g_dirs[dir] = {network, root};
            g_names[lower(dir)] = root;
        }
        std::string rest = file.path.substr(root.size());
        rest = rest.substr(0, rest.find('?'));
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= rest.size()) {
            size_t end = rest.find('/', start);
            std::string part = rest.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!part.empty()) {
                parts.push_back(part);
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        if (parts.empty()) {
            return false;
        }
        bool escaped = escapedPaths(file.path);
        std::string remote = root;
        for (size_t i = 0; i + 1 < parts.size(); i++) {
            remote += (i > 0 ? "/" : "") + parts[i];
            std::string name = escaped ? Http::unescape(parts[i]) : parts[i];
            dir = uniqueName(dir, safeName(name, false), remote);
            if (g_dirs.count(dir) == 0) {
                g_dirs[dir] = {name, remote};
                g_names[lower(dir)] = remote;
            }
        }
        std::string name = !file.name.empty() ? file.name : escaped ? Http::unescape(parts.back()) : parts.back();
        *local = uniqueName(dir, safeName(name, true), file.path);
        g_names[lower(*local)] = file.path;
        Entry &entry = g_files[*local];
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

    // the torrent directory of a saved TorrServer file: its first part under the network
    std::string torrentDir(const std::string &local) {
        size_t a = local.find('/');
        size_t b = a == std::string::npos ? a : local.find('/', a + 1);
        return b == std::string::npos ? "" : local.substr(0, b);
    }

    void writeIndex() {
        std::string text = INDEX_VERSION "\n";
        for (auto &d: g_dirs) {
            text += "D\t" + escape(d.first) + "\t" + escape(d.second.name) + "\t" + escape(d.second.remote) + "\n";
        }
        for (auto &t: g_torrents) {
            auto &i = t.second.info;
            text += "T\t" + escape(t.first) + "\t" + escape(i.hash) + "\t" + escape(i.title) + "\t" + escape(i.poster)
                    + "\t" + escape(i.category) + "\t" + std::to_string((long long) i.torrentSize) + "\t"
                    + std::to_string((long long) i.filesSize) + "\t" + std::to_string(i.fileCount) + "\t"
                    + (t.second.poster ? "1" : "0") + "\n";
        }
        for (auto &f: g_files) {
            auto &e = f.second;
            text += "F\t" + escape(f.first) + "\t" + escape(e.name) + "\t" + escape(e.remote) + "\t"
                    + std::to_string((long long) e.size) + "\t" + (e.saved ? "1" : "0") + "\t"
                    + std::to_string((long long) e.added) + "\t" + std::to_string((long long) e.access) + "\n";
        }
        if (Io::get() != nullptr && makeDirs(c2d::Utility::removeLastSlash(g_root))
            && !Io::get()->writeText(g_root + ".index", text)) {
            Utility::log(Utility::LogLevel::Error, "Saved: can't write " + g_root + ".index");
        }
        g_dirty = g_accessDirty = false;
        g_writtenAt = time(nullptr);
    }

    void readIndex() {
        g_files.clear();
        g_dirs.clear();
        g_torrents.clear();
        g_names.clear();
        std::string text;
        if (Io::get() == nullptr || !Io::get()->readText(g_root + ".index", text)) {
            return;
        }
        size_t start = 0;
        bool first = true;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            start = end == std::string::npos ? text.size() : end + 1;
            if (first) {
                first = false;
                if (line != INDEX_VERSION) {
                    return;
                }
                continue;
            }
            std::vector<std::string> f = fields(line);
            if (f[0] == "D" && f.size() >= 4) {
                g_dirs[f[1]] = {f[2], f[3]};
                g_names[lower(f[1])] = f[3];
            } else if (f[0] == "T" && f.size() >= 10) {
                Torrent &t = g_torrents[f[1]];
                t.info.hash = f[2];
                t.info.title = f[3];
                t.info.poster = f[4];
                t.info.category = f[5];
                t.info.torrentSize = std::atoll(f[6].c_str());
                t.info.filesSize = std::atoll(f[7].c_str());
                t.info.fileCount = (size_t) std::atoll(f[8].c_str());
                t.poster = f[9] == "1";
            } else if (f[0] == "F" && f.size() >= 8) {
                Entry &e = g_files[f[1]];
                e.name = f[2];
                e.remote = f[3];
                e.size = std::atoll(f[4].c_str());
                e.saved = f[5] == "1";
                e.added = (time_t) std::atoll(f[6].c_str());
                e.access = (time_t) std::atoll(f[7].c_str());
                g_names[lower(f[1])] = f[3];
            }
        }
    }

    // the directories nothing is saved in removed (their files, the poster)
    void prune() {
        std::vector<std::string> dirs;
        for (auto &d: g_dirs) {
            dirs.push_back(d.first);
        }
        // the deepest first
        std::sort(dirs.begin(), dirs.end(), [](const std::string &a, const std::string &b) { return a > b; });
        for (auto &dir: dirs) {
            bool used = false;
            for (auto &f: g_files) {
                if (under(f.first, dir)) {
                    used = true;
                    break;
                }
            }
            if (used) {
                continue;
            }
            Utility::deleteFile(g_root + dir + "/.poster");
            rmdir((g_root + dir).c_str());
            g_names.erase(lower(dir));
            g_torrents.erase(dir);
            g_dirs.erase(dir);
        }
    }

    // locked
    void removeEntry(const std::string &rel) {
        auto it = g_files.find(rel);
        if (it == g_files.end()) {
            return;
        }
        if (it->second.active) {
            // the thread removes the part
            it->second.cancel->store(true);
        }
        Utility::deleteFile(g_root + rel);
        Utility::deleteFile(g_root + rel + ".part");
        g_names.erase(lower(rel));
        g_files.erase(it);
        g_jobs.erase(std::remove_if(g_jobs.begin(), g_jobs.end(),
                                    [&rel](const Job &job) { return !job.dir && job.local == rel; }),
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

    // the torrent of a TorrServer file kept, its poster saved in its directory (a thread)
    void keepTorrent(const std::string &local, const std::string &remote) {
        if (Io::get()->getDeviceType(remote) != Io::DeviceType::TorrServe) {
            return;
        }
        TorrServe::Info info;
        std::string dir = torrentDir(local);
        if (dir.empty() || !TorrServe::getInfo(remote, info)) {
            return;
        }
        std::string poster;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            Torrent &t = g_torrents[dir];
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
        auto it = g_torrents.find(dir);
        if (it != g_torrents.end() && makeDirs(g_root + dir) && Io::get()->writeText(g_root + dir + "/.poster", data)) {
            it->second.poster = true;
            g_dirty = true;
        }
    }

    // a directory listed: its files queued (a thread)
    void expand(const c2d::Io::File &dir, int depth) {
        Io *io = Io::get();
        int timeout = g_config->getOption(OPT_NETWORK_TIMEOUT)->getInteger();
        std::string error;
        std::vector<c2d::Io::File> files = io->list(io->getDeviceType(dir.path), dir.path, timeout, &error);
        if (files.empty() && !error.empty()) {
            std::lock_guard<std::mutex> lock(g_mutex);
            message("Error", tr("Could not save") + " " + dir.name + ": " + error);
            return;
        }
        for (auto &file: files) {
            if (g_stopping || file.name == "." || file.name == "..") {
                continue;
            }
            if (file.type == c2d::Io::Type::Directory) {
                if (depth < 16) {
                    expand(file, depth + 1);
                }
            } else if (file.type == c2d::Io::Type::File) {
                std::lock_guard<std::mutex> lock(g_mutex);
                std::string local;
                if (place(file, &local) && !g_files[local].saved && !g_files[local].active) {
                    g_files[local].failed = false;
                    g_jobs.push_back({false, file, local});
                    g_dirty = true;
                }
            }
        }
        changed();
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

    Outcome download(const std::string &local, const std::string &remote, std::atomic<bool> *cancel,
                    std::string *error) {
        std::string path = g_root + local;
        std::string part = path + ".part";
        if (!makeDirs(parentOf(path))) {
            *error = tr("Can't write to") + " " + parentOf(path);
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
            auto it = g_files.find(local);
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
        int64_t space = freeSpace(parentOf(path));
        if (size > 0 && space >= 0 && space < size - from + FREE_MARGIN) {
            source.close();
            *error = tr("Not enough free space") + " (" + Utility::formatSize((size_t) (size - from)) + ")";
            return Outcome::Failed;
        }
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
                auto it = g_files.find(local);
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
            auto it = g_files.find(local);
            if (*cancel && (it == g_files.end() || it->second.cancel.get() != cancel)) {
                Utility::deleteFile(part);
            }
            return result;
        }
        Utility::deleteFile(path);
        if (rename(part.c_str(), path.c_str()) != 0) {
            *error = strerror(errno);
            return Outcome::Failed;
        }
        return Outcome::Done;
    }

    void saveFile(const Job &job) {
        std::shared_ptr<std::atomic<bool>> cancel;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_files.find(job.local);
            if (it == g_files.end() || it->second.saved || it->second.active) {
                return;
            }
            it->second.active = true;
            it->second.failed = false;
            it->second.cancel = cancel = std::make_shared<std::atomic<bool>>(false);
        }
        changed();
        keepTorrent(job.local, job.file.path);

        // the stream marks the file viewed on the server
        bool torrent = Io::get()->getDeviceType(job.file.path) == Io::DeviceType::TorrServe;
        bool viewed = torrent && TorrServe::isFileViewed(job.file.path, true);
        int retries = std::max(1, (int) g_config->getOption(OPT_NETWORK_RETRIES)->getInteger());
        std::string error;
        Outcome result = Outcome::Retry;
        for (int attempt = 0; attempt <= retries && result == Outcome::Retry; attempt++) {
            if (attempt > 0) {
                Utility::log(Utility::LogLevel::Info, "Saved: retry " + job.local + ": " + error);
                for (int i = 0; i < 30 && !*cancel && !g_stopping; i++) {
                    usleep(100000);
                }
            }
            error.clear();
            result = download(job.local, job.file.path, cancel.get(), &error);
        }
        if (torrent && !viewed) {
            TorrServe::remFileViewed(job.file.path);
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_files.find(job.local);
        if (it == g_files.end() || it->second.cancel != cancel) {
            changed();
            return;
        }
        Entry &entry = it->second;
        entry.active = false;
        entry.cancel.reset();
        if (result == Outcome::Done) {
            entry.saved = true;
            isFile(g_root + job.local, &entry.size);
            entry.done = entry.size;
            entry.access = time(nullptr);
            // the network file information, for the copy
            MediaInfo info(job.file);
            if (info.known) {
                c2d::Io::File copy(entry.name, g_root + job.local, c2d::Io::Type::File, (size_t) entry.size);
                info.save(copy);
            }
            Utility::log(Utility::LogLevel::Info, "Saved: " + job.local);
            g_cleanup = true;
        } else if (result != Outcome::Cancelled) {
            entry.failed = true;
            Utility::log(Utility::LogLevel::Error, "Saved: failed " + job.local + ": " + error);
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

    std::string relOf(const std::string &path) {
        std::string root = c2d::Utility::removeLastSlash(g_root);
        if (path.size() <= root.size()) {
            return "";
        }
        std::string rel = path.substr(root.size() + 1);
        while (!rel.empty() && rel.back() == '/') {
            rel.pop_back();
        }
        return rel;
    }

    std::string rootFromConfig() {
        std::string home = g_config != nullptr ? g_config->getOption(OPT_HOME_PATH)->getString() : "";
        return Paths::ensureTrailingSlash(home.empty() ? Utility::getDataPath() : home) + SAVE_DIR "/";
    }
}

namespace pplay::Saved {

    void start(PPLAYConfig *config) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_config = config;
        g_root = rootFromConfig();
        g_stopping = false;
        readIndex();
        int resumed = 0;
        for (auto it = g_files.begin(); it != g_files.end();) {
            if (it->second.saved && !isFile(g_root + it->first)) {
                // removed by hand
                g_names.erase(lower(it->first));
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

    bool isInside(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string root = c2d::Utility::removeLastSlash(g_root);
        return !root.empty() && c2d::Utility::startWith(path, root)
               && (path.size() == root.size() || path[root.size()] == '/');
    }

    bool isRoot(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        return c2d::Utility::removeLastSlash(path) == c2d::Utility::removeLastSlash(g_root);
    }

    bool canSave(const c2d::Io::File &file) {
        if (file.path.empty() || file.name == ".." || file.path.find("://") == std::string::npos
            || (file.type != c2d::Io::Type::File && file.type != c2d::Io::Type::Directory)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        return Io::get() != nullptr && !Io::get()->getNetworkRoot(file.path).empty();
    }

    void save(const c2d::Io::File &file) {
        if (!canSave(file)) {
            return;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        // a writable drive (an unplugged one, a read only card)
        std::string probe = g_root + ".write";
        if (!makeDirs(c2d::Utility::removeLastSlash(g_root)) || !Io::get()->writeText(probe, "")) {
            message("Error", tr("Can't write to") + " " + g_root);
            return;
        }
        Utility::deleteFile(probe);
        if (file.type == c2d::Io::Type::Directory) {
            g_jobs.push_back({true, file, ""});
        } else {
            std::string local;
            if (!place(file, &local)) {
                return;
            }
            Entry &entry = g_files[local];
            if (entry.saved || entry.active) {
                return;
            }
            entry.failed = false;
            g_jobs.push_back({false, file, local});
            g_dirty = true;
        }
        message("Info", tr("Saving") + ": " + file.name);
        changed();
        spawn();
    }

    State stateOf(const std::string &path) {
        State state;
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string root = c2d::Utility::removeLastSlash(g_root);
        bool local = c2d::Utility::startWith(path, root) && (path.size() == root.size() || path[root.size()] == '/');
        std::string key = local ? relOf(path) : c2d::Utility::removeLastSlash(path);
        int64_t total = 0, done = 0;
        for (auto &f: g_files) {
            const std::string &target = local ? f.first : f.second.remote;
            bool match = local ? under(target, key)
                               : target == key || (target.size() > key.size() && target.compare(0, key.size(), key) == 0
                                                   && target[key.size()] == '/');
            if (!match) {
                continue;
            }
            if (f.second.saved) {
                state.saved++;
            } else if (f.second.active || !f.second.failed) {
                state.pending++;
                total += f.second.size;
                done += f.second.done;
            } else {
                state.failed = true;
            }
        }
        // a directory being listed
        if (!local) {
            for (auto &job: g_jobs) {
                if (job.dir && under(job.file.path, key)) {
                    state.pending++;
                }
            }
        }
        state.progress = total > 0 ? (float) ((double) done / (double) total) : 0;
        return state;
    }

    int remove(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string root = c2d::Utility::removeLastSlash(g_root);
        bool local = c2d::Utility::startWith(path, root) && (path.size() == root.size() || path[root.size()] == '/');
        std::string key = local ? relOf(path) : c2d::Utility::removeLastSlash(path);
        std::vector<std::string> rels;
        for (auto &f: g_files) {
            const std::string &target = local ? f.first : f.second.remote;
            bool match = local ? under(target, key)
                               : target == key || (target.size() > key.size() && target.compare(0, key.size(), key) == 0
                                                   && target[key.size()] == '/');
            if (match) {
                rels.push_back(f.first);
            }
        }
        for (auto &rel: rels) {
            removeEntry(rel);
        }
        if (!local) {
            g_jobs.erase(std::remove_if(g_jobs.begin(), g_jobs.end(),
                                        [&key](const Job &job) { return job.dir && under(job.file.path, key); }),
                         g_jobs.end());
        }
        prune();
        g_dirty = true;
        changed();
        return (int) rels.size();
    }

    void filter(const std::string &dir, std::vector<c2d::Io::File> &files) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string rel = relOf(dir);
        std::vector<c2d::Io::File> out;
        for (auto &file: files) {
            if (file.name.empty() || file.name[0] == '.' || c2d::Utility::endsWith(file.name, ".part")) {
                continue;
            }
            std::string child = rel.empty() ? file.name : rel + "/" + file.name;
            c2d::Io::File shown = file;
            if (file.type == c2d::Io::Type::Directory) {
                bool any = false;
                for (auto &f: g_files) {
                    if (f.second.saved && under(f.first, child)) {
                        any = true;
                        break;
                    }
                }
                if (!any) {
                    continue;
                }
                auto d = g_dirs.find(child);
                if (d != g_dirs.end() && !d->second.name.empty()) {
                    shown.name = d->second.name;
                }
            } else {
                auto f = g_files.find(child);
                if (f == g_files.end() || !f->second.saved) {
                    continue;
                }
                shown.name = f->second.name;
            }
            out.push_back(shown);
        }
        files.swap(out);
    }

    std::vector<std::string> displayParts(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<std::string> parts;
        std::string rel = relOf(path);
        size_t start = 0;
        while (!rel.empty() && start <= rel.size()) {
            size_t end = rel.find('/', start);
            std::string prefix = rel.substr(0, end);
            std::string name = prefix.substr(prefix.rfind('/') == std::string::npos ? 0 : prefix.rfind('/') + 1);
            auto d = g_dirs.find(prefix);
            auto f = g_files.find(prefix);
            parts.push_back(d != g_dirs.end()   ? d->second.name
                            : f != g_files.end() ? f->second.name
                                                 : name);
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return parts;
    }

    bool getInfo(const std::string &path, TorrServe::Info &info, std::string *poster) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string rel = relOf(path);
        std::string dir = torrentDir(rel + "/");
        auto t = g_torrents.find(dir);
        if (dir.empty() || t == g_torrents.end()) {
            return false;
        }
        info = t->second.info;
        *poster = t->second.poster ? "dir:" + g_root + dir + "/.poster" : "";
        auto f = g_files.find(rel);
        if (f != g_files.end()) {
            info.fileName = f->second.name;
            info.fileSize = f->second.size;
            size_t index = f->second.remote.find("index=");
            info.fileIndex = index == std::string::npos ? 0 : std::atoi(f->second.remote.c_str() + index + 6);
        }
        return true;
    }

    void touch(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto f = g_files.find(relOf(path));
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
