//
// Playlists (see playlists.h): "#EXTINF:<seconds>,<name>" then the path, per entry.
//

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <sys/stat.h>

#include "i18n.h"
#include "io.h"
#include "media_info.h"
#include "paths.h"
#include "playlists.h"
#include "pplay_config.h"
#include "saved.h"
#include "utility.h"
#include "worker.h"

#define PLAYLISTS_DIR "pPlay playlists"
#define PLAYLIST_EXT ".m3u8"

using namespace pplay;

namespace {

    struct Job {
        std::string name;
        c2d::Io::File file;
    };

    std::mutex g_mutex;
    PPLAYConfig *g_config = nullptr;
    std::deque<Job> g_jobs;
    Worker *g_worker = nullptr;
    std::atomic<bool> g_running{false};
    std::atomic<bool> g_stopping{false};
    std::atomic<unsigned int> g_generation{1};
    std::deque<std::pair<std::string, std::string>> g_messages;

    std::string pathOf(const std::string &name) {
        return Playlists::root() + "/" + name + PLAYLIST_EXT;
    }

    bool validName(const std::string &name) {
        if (name.empty() || name[0] == '.' || name.size() > 120) {
            return false;
        }
        for (char c: name) {
            if ((unsigned char) c < 0x20 || std::string("\\/:*?\"<>|").find(c) != std::string::npos) {
                return false;
            }
        }
        return true;
    }

    // as the filer: directories first, the names without case
    bool before(const c2d::Io::File &a, const c2d::Io::File &b) {
        bool da = a.type == c2d::Io::Type::Directory, db = b.type == c2d::Io::Type::Directory;
        if (da != db) {
            return da;
        }
        return c2d::Utility::toLower(a.name) < c2d::Utility::toLower(b.name);
    }

    bool listable(const c2d::Io::File &file) {
        return Utility::isMedia(file) && !c2d::Utility::endsWith(file.name, ".m3u", false)
               && !c2d::Utility::endsWith(file.name, ".m3u8", false);
    }

    void collect(const c2d::Io::File &dir, int depth, std::vector<c2d::Io::File> &out) {
        Io *io = Io::get();
        int timeout = g_config->getOption(OPT_NETWORK_TIMEOUT)->getInteger();
        bool hidden = g_config->getOption(OPT_SHOW_HIDDEN)->getInteger() != 0;
        std::string error;
        std::vector<c2d::Io::File> files = io->list(io->getDeviceType(dir.path), dir.path, timeout, &error, false, hidden);
        // saved copies: their names
        if (Saved::isInside(dir.path)) {
            Saved::filter(dir.path, files);
        }
        std::sort(files.begin(), files.end(), before);
        for (auto &file: files) {
            if (g_stopping || file.name == "." || file.name == ".." || (!hidden && file.name[0] == '.')) {
                continue;
            }
            if (file.type == c2d::Io::Type::Directory) {
                if (depth < 16) {
                    collect(file, depth + 1, out);
                }
            } else if (listable(file)) {
                out.push_back(file);
                // the network file of a saved copy (the copy played if there)
                out.back().path = Saved::originalOf(file.path);
            }
        }
    }

    std::string entryText(const c2d::Io::File &file) {
        long duration = MediaInfo(file).duration;
        std::string name = file.name;
        std::replace(name.begin(), name.end(), '\n', ' ');
        return "#EXTINF:" + std::to_string(duration > 0 ? duration : -1) + "," + name + "\n" + file.path + "\n";
    }

    bool append(const std::string &name, const std::vector<c2d::Io::File> &files) {
        std::string path = pathOf(name);
        std::string text;
        Io *io = Io::get();
        if (!io->exist(Playlists::root())) {
            io->create(Playlists::root());
        }
        if (!io->readText(path, text) || text.empty()) {
            text = "#EXTM3U\n";
        }
        if (text.back() != '\n') {
            text += "\n";
        }
        for (auto &file: files) {
            text += entryText(file);
        }
        bool ok = io->writeText(path, text);
        g_generation++;
        return ok;
    }

    int threadMain(void *) {
        while (!g_stopping) {
            Job job;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (g_jobs.empty()) {
                    g_running = false;
                    return 0;
                }
                job = g_jobs.front();
                g_jobs.pop_front();
            }
            std::vector<c2d::Io::File> files;
            if (job.file.type == c2d::Io::Type::Directory) {
                collect(job.file, 0, files);
            } else {
                files.push_back(job.file);
                files.back().path = Saved::originalOf(job.file.path);
            }
            bool ok = !files.empty() && append(job.name, files);
            std::lock_guard<std::mutex> lock(g_mutex);
            if (files.empty()) {
                g_messages.emplace_back("Error", tr("No media files") + ": " + job.file.name);
            } else if (!ok) {
                g_messages.emplace_back("Error", tr("Could not write") + " " + pathOf(job.name));
            } else {
                g_messages.emplace_back("Info", tr("Added to the playlist") + " " + job.name + ": "
                                                        + std::to_string(files.size()));
            }
        }
        g_running = false;
        return 0;
    }

    struct Entry {
        std::string info;
        std::string path;
    };

    // "#EXTM3U" kept out
    std::vector<Entry> read(const std::string &path) {
        std::vector<Entry> out;
        std::string text;
        if (!Io::get()->readText(path, text)) {
            return out;
        }
        std::string info;
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            start = end == std::string::npos ? text.size() : end + 1;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (c2d::Utility::startWith(line, "#EXTINF:")) {
                info = line;
            } else if (!line.empty() && line[0] != '#') {
                out.push_back({info, line});
                info.clear();
            }
        }
        return out;
    }
}

namespace pplay::Playlists {

    void start(PPLAYConfig *config) {
        g_config = config;
        g_stopping = false;
    }

    void stop() {
        g_stopping = true;
        delete (g_worker);
        g_worker = nullptr;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_jobs.clear();
    }

    bool takeMessage(std::string *type, std::string *message) {
        std::lock_guard<std::mutex> lock(g_mutex);
        // the thread done: joined
        if (!g_running && g_worker != nullptr && g_jobs.empty()) {
            delete (g_worker);
            g_worker = nullptr;
        }
        if (g_messages.empty()) {
            return false;
        }
        *type = g_messages.front().first;
        *message = g_messages.front().second;
        g_messages.pop_front();
        return true;
    }

    unsigned int generation() {
        return g_generation;
    }

    std::string root() {
        std::string home = g_config != nullptr ? g_config->getOption(OPT_HOME_PATH)->getString() : "";
        return Paths::ensureTrailingSlash(home.empty() ? Utility::getDataPath() : home) + PLAYLISTS_DIR;
    }

    bool isRoot(const std::string &path) {
        return c2d::Utility::removeLastSlash(path) == root();
    }

    bool isPlaylist(const std::string &path) {
        std::string dir = root() + "/";
        return c2d::Utility::startWith(path, dir) && path.find('/', dir.size()) == std::string::npos
               && c2d::Utility::endsWith(path, PLAYLIST_EXT, false) && path.size() > dir.size() + strlen(PLAYLIST_EXT);
    }

    std::string nameOf(const std::string &path) {
        std::string name = Paths::getLeafName(path);
        return c2d::Utility::endsWith(name, PLAYLIST_EXT, false) ? name.substr(0, name.size() - strlen(PLAYLIST_EXT))
                                                                 : name;
    }

    void add(const std::string &name, const c2d::Io::File &file) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!validName(name)) {
            g_messages.emplace_back("Error", tr("Invalid name") + ": " + name);
            return;
        }
        g_jobs.push_back({name, file});
        if (!g_running) {
            // the previous one finished
            delete (g_worker);
            g_running = true;
            g_worker = new Worker(threadMain, nullptr);
        }
    }

    void filter(std::vector<c2d::Io::File> &files) {
        std::vector<c2d::Io::File> out;
        for (auto &file: files) {
            if (file.type == c2d::Io::Type::File && isPlaylist(file.path)) {
                out.emplace_back(nameOf(file.path), file.path, c2d::Io::Type::Directory, file.size);
            }
        }
        files.swap(out);
    }

    std::vector<c2d::Io::File> entries(const std::string &path) {
        std::vector<c2d::Io::File> files;
        for (auto &entry: read(path)) {
            size_t comma = entry.info.find(',');
            std::string name = comma != std::string::npos ? entry.info.substr(comma + 1) : "";
            if (name.empty()) {
                name = Paths::getLeafName(entry.path.substr(0, entry.path.find('?')));
            }
            files.emplace_back(name, entry.path, c2d::Io::Type::File, 0);
        }
        return files;
    }

    bool removeEntry(const std::string &path, int index) {
        std::vector<Entry> list = read(path);
        if (index < 0 || index >= (int) list.size()) {
            return false;
        }
        list.erase(list.begin() + index);
        std::string text = "#EXTM3U\n";
        for (auto &entry: list) {
            text += (entry.info.empty() ? "" : entry.info + "\n") + entry.path + "\n";
        }
        bool ok = Io::get()->writeText(path, text);
        g_generation++;
        return ok;
    }

    bool rename(const std::string &path, const std::string &name) {
        std::string target = pathOf(name);
        if (!validName(name) || Io::get()->exist(target)) {
            return false;
        }
        bool ok = ::rename(path.c_str(), target.c_str()) == 0;
        g_generation++;
        return ok;
    }

    bool remove(const std::string &path) {
        bool ok = isPlaylist(path) && Utility::deleteFile(path);
        g_generation++;
        return ok;
    }
}
