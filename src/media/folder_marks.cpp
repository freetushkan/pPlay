//
// Watched marks of a whole directory (see folder_marks.h).
//

#include <atomic>
#include <condition_variable>
#include <mutex>

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

    void scan(const c2d::Io::File &dir) {
        std::vector<c2d::Io::File> files;
        collect(dir, 0, files);
        FolderMarks::State state;
        for (auto &file: files) {
            if (g_cancel) {
                return;
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
        state.ready = true;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_cancel && g_dir.path == dir.path) {
            Utility::log(Utility::LogLevel::Debug, "FolderMarks: " + dir.path + ": " + std::to_string(files.size())
                                                           + " files, marked " + std::to_string(state.marked)
                                                           + ", unwatched " + std::to_string(state.unwatched));
            g_state = state;
            g_files.swap(files);
            g_generation++;
        }
    }

    int threadMain(void *) {
        while (true) {
            c2d::Io::File dir;
            {
                std::unique_lock<std::mutex> lock(g_mutex);
                g_wake.wait(lock, [] { return g_quit || g_pending; });
                if (g_quit) {
                    return 0;
                }
                g_pending = false;
                g_cancel = false;
                dir = g_dir;
            }
            scan(dir);
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
        if (g_worker == nullptr) {
            g_worker = new Worker(threadMain, nullptr);
        }
        g_wake.notify_all();
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
