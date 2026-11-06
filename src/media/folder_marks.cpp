//
// Watched marks of a whole directory (see folder_marks.h).
//

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>

#include "folder_marks.h"
#include "io.h"
#include "playlists.h"
#include "pplay_config.h"
#include "saved.h"
#include "torrserve.h"
#include "utility.h"
#include "worker.h"

using namespace pplay;

namespace {

    const int MAX_DEPTH = 8;
    const size_t MAX_FILES = 5000;

    std::mutex g_mutex;
    PPLAYConfig *g_config = nullptr;
    Worker *g_worker = nullptr;
    std::condition_variable g_wake;
    // a newer request (or the end): the listing stops
    std::atomic<bool> g_cancel{false};
    bool g_quit = false;
    bool g_pending = false;
    std::atomic<unsigned int> g_generation{1};
    c2d::Io::File g_dir;
    FolderMarks::State g_state;
    std::vector<c2d::Io::File> g_files;

    // the rows: their directories counted one by one, kept a while
    const float ROW_KEEP_SECS = 60;
    struct Cached {
        FolderMarks::State state;
        c2d::C2DClock age;
        bool stale = false;
    };
    std::map<std::string, Cached> g_cache;
    std::deque<c2d::Io::File> g_queue;
    std::set<std::string> g_queued;
    std::atomic<unsigned int> g_rowsGeneration{1};

    void remember(const std::string &path, const FolderMarks::State &state) {
        auto it = g_cache.find(path);
        bool changed = it == g_cache.end() || it->second.state.files != state.files
                       || it->second.state.marked != state.marked || it->second.state.unwatched != state.unwatched;
        g_cache[path].state = state;
        g_cache[path].age.restart();
        g_cache[path].stale = false;
        if (changed) {
            g_rowsGeneration++;
        }
    }

    void collect(const c2d::Io::File &dir, int depth, std::vector<c2d::Io::File> &out) {
        if (g_cancel || out.size() >= MAX_FILES) {
            return;
        }
        std::vector<c2d::Io::File> list;
        if (Playlists::isPlaylist(dir.path)) {
            list = Playlists::entries(dir.path);
        } else if (Saved::isInside(dir.path)) {
            list = Saved::list(dir.path);
        } else {
            Io *io = Io::get();
            int timeout = g_config->getOption(OPT_NETWORK_TIMEOUT)->getInteger();
            bool hidden = g_config->getOption(OPT_SHOW_HIDDEN)->getInteger() != 0;
            std::string error;
            list = io->list(io->getDeviceType(dir.path), dir.path, timeout, &error, false, hidden);
        }
        for (auto &file: list) {
            if (g_cancel || file.name == "." || file.name == "..") {
                continue;
            }
            if (file.type == c2d::Io::Type::Directory) {
                if (depth < MAX_DEPTH) {
                    collect(file, depth + 1, out);
                }
            } else if (Utility::isMedia(file) && out.size() < MAX_FILES) {
                out.push_back(file);
            }
        }
    }

    // false: stopped by a newer panel request
    bool scan(const c2d::Io::File &dir, bool panel) {
        std::vector<c2d::Io::File> files;
        collect(dir, 0, files);
        FolderMarks::State state;
        state.files = (int) files.size();
        for (auto &file: files) {
            if (g_cancel) {
                return false;
            }
            std::string path = Saved::originalOf(file.path);
            std::string url = TorrServe::toStreamUrl(path);
            bool local = Utility::isWatchLaterExist(url);
            bool server = TorrServe::isFileViewed(path, true);
            if (local || server) {
                state.marked++;
            }
            // the server mark alone: watched too
            if (local ? !Utility::isWatchLaterWatched(url) : !server) {
                state.unwatched++;
            }
        }
        if (g_cancel) {
            return false;
        }
        state.ready = true;
        std::lock_guard<std::mutex> lock(g_mutex);
        remember(dir.path, state);
        if (panel && g_dir.path == dir.path) {
            Utility::log(Utility::LogLevel::Debug, "FolderMarks: " + dir.path + ": " + std::to_string(files.size())
                                                           + " files, marked " + std::to_string(state.marked)
                                                           + ", unwatched " + std::to_string(state.unwatched));
            g_state = state;
            g_files.swap(files);
            g_generation++;
        }
        return true;
    }

    int threadMain(void *) {
        while (true) {
            c2d::Io::File dir;
            bool panel;
            {
                std::unique_lock<std::mutex> lock(g_mutex);
                g_wake.wait(lock, [] { return g_quit || g_pending || !g_queue.empty(); });
                if (g_quit) {
                    return 0;
                }
                // the panel first
                panel = g_pending;
                g_cancel = false;
                if (panel) {
                    g_pending = false;
                    dir = g_dir;
                } else {
                    dir = g_queue.front();
                    g_queue.pop_front();
                }
            }
            bool done = scan(dir, panel);
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!panel) {
                // stopped: again after the panel
                if (done || g_quit) {
                    g_queued.erase(dir.path);
                } else {
                    g_queue.push_front(dir);
                }
            }
        }
    }
}

namespace pplay::FolderMarks {

    void start(PPLAYConfig *config) {
        g_config = config;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_quit = true;
            g_cancel = true;
        }
        g_wake.notify_all();
        delete (g_worker);
        g_worker = nullptr;
    }

    void wake() {
        if (g_worker == nullptr) {
            g_worker = new Worker(threadMain, nullptr);
        }
        g_wake.notify_all();
    }

    State cached(const c2d::Io::File &dir) {
        State state;
        bool queued = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_cache.find(dir.path);
            bool fresh = it != g_cache.end() && !it->second.stale
                         && it->second.age.getElapsedTime().asSeconds() < ROW_KEEP_SECS;
            if (!fresh && g_queued.insert(dir.path).second) {
                g_queue.push_back(dir);
                queued = true;
            }
            if (it != g_cache.end()) {
                state = it->second.state;
            }
        }
        if (queued) {
            wake();
        }
        return state;
    }

    void forget() {
        std::lock_guard<std::mutex> lock(g_mutex);
        // shown until counted again
        for (auto &entry: g_cache) {
            entry.second.stale = true;
        }
        g_rowsGeneration++;
    }

    unsigned int rowsGeneration() {
        return g_rowsGeneration;
    }

    void request(const c2d::Io::File &dir, bool force) {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!force && dir.path == g_dir.path) {
                return;
            }
            g_dir = dir;
            g_state = State();
            g_files.clear();
            g_generation++;
            g_pending = true;
            g_cancel = true;
        }
        wake();
    }

    State stateOf(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        return path == g_dir.path ? g_state : State();
    }

    unsigned int generation() {
        return g_generation;
    }

    std::vector<c2d::Io::File> files(const std::string &path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        return path == g_dir.path && g_state.ready ? g_files : std::vector<c2d::Io::File>();
    }
}
