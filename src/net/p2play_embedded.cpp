//
// Embedded P2Play core lifecycle. No TorrServer adapter or state is used here.
//
#include "p2play_embedded.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

#include "../../third_party/P2Play/src/core/p2play.h"
#include "utility.h"

namespace pplay::P2PlayEmbedded {
    namespace {
        enum class WorkerMode { None, Starting, Stopping };

        std::mutex g_mutex;
        std::thread g_worker;
        WorkerMode g_workerMode = WorkerMode::None;
        bool g_workerDone = false;
        bool g_workerSuccess = false;
        bool g_running = false;
        bool g_wanted = false;
        bool g_draining = false;
        bool g_failed = false;
        bool g_shuttingDown = false;
        std::string g_dataDir;
        std::string g_ownerPassword;

        void logLine(const char *line, void *) {
            Utility::log(Utility::LogLevel::Info, std::string("P2Play: ") + (line != nullptr ? line : ""));
        }

        const char *platformName() {
#if defined(__SWITCH__)
            return "Nintendo Switch";
#elif defined(__PS4__)
            return "PlayStation 4";
#elif defined(__PS5__)
            return "PlayStation 5";
#elif defined(__ANDROID__)
            return "Android";
#elif defined(__LINUX__)
            return "Linux";
#else
            return "pPlay";
#endif
        }

        bool consoleBuild() {
#if defined(__SWITCH__) || defined(__PS4__) || defined(__PS5__)
            return true;
#else
            return false;
#endif
        }

        void startWorkerLocked() {
            const std::string dataDir = g_dataDir;
            const std::string password = g_ownerPassword;
            g_workerMode = WorkerMode::Starting;
            g_workerDone = false;
            g_workerSuccess = false;
            g_worker = std::thread([dataDir, password]() {
                const std::string platform = platformName();
                p2play_config config{};
                config.data_dir = dataDir.c_str();
                config.http_port = 8090;
                config.console = consoleBuild() ? 1 : 0;
                config.platform = platform.c_str();
                config.log = logLine;
                config.owner_password = password.c_str();
                const auto startedAt = std::chrono::steady_clock::now();
                Utility::log(Utility::LogLevel::Info, "P2Play: embedded core start requested");
                const int result = p2play_start(&config);
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - startedAt).count();
                Utility::log(Utility::LogLevel::Info, "P2Play: embedded core start completed in " +
                        std::to_string(elapsed) + " ms, result=" + std::to_string(result));
                std::lock_guard<std::mutex> lock(g_mutex);
                g_workerSuccess = result == 0;
                g_workerDone = true;
            });
        }

        void stopWorkerLocked() {
            g_workerMode = WorkerMode::Stopping;
            g_workerDone = false;
            g_workerSuccess = false;
            g_worker = std::thread([]() {
                const auto stoppedAt = std::chrono::steady_clock::now();
                Utility::log(Utility::LogLevel::Info, "P2Play: embedded core stop requested after drain");
                p2play_stop();
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - stoppedAt).count();
                Utility::log(Utility::LogLevel::Info, "P2Play: embedded core stop completed in " +
                        std::to_string(elapsed) + " ms");
                std::lock_guard<std::mutex> lock(g_mutex);
                g_workerSuccess = true;
                g_workerDone = true;
            });
        }
    }

    bool activate(const std::string &dataDir, const std::string &ownerPassword) {
        bool stopAccepting = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_shuttingDown) return false;
            const bool changed = g_dataDir != dataDir || g_ownerPassword != ownerPassword;
            const bool wasWanted = g_wanted;
            g_wanted = true;

            if (changed) {
                g_dataDir = dataDir;
                g_ownerPassword = ownerPassword;
                g_failed = false;
                if (g_running && !g_draining) {
                    g_draining = true;
                    stopAccepting = true;
                    Utility::log(Utility::LogLevel::Info, "P2Play: embedded settings changed; draining active streams");
                } else if (g_workerMode == WorkerMode::Starting) {
                    g_draining = true;
                }
            } else if (!wasWanted && g_failed) {
                g_failed = false;
            }

            if (ownerPassword.empty()) {
                g_failed = true;
                if (g_running && !g_draining) {
                    g_draining = true;
                    stopAccepting = true;
                }
            } else if (!g_running && g_workerMode == WorkerMode::None && !g_draining && !g_failed) {
                startWorkerLocked();
            }
        }
        if (stopAccepting) {
            p2play_stop_accepting();
        }
        return isReady();
    }

    void deactivate() {
        bool stopAccepting = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_wanted = false;
            if (g_running && !g_draining) {
                g_draining = true;
                stopAccepting = true;
                Utility::log(Utility::LogLevel::Info, "P2Play: embedded network deactivated; draining active streams");
            }
        }
        if (stopAccepting) {
            p2play_stop_accepting();
        }
    }

    void update() {
        std::thread completedWorker;
        bool stopAccepting = false;
        bool checkDrain = false;
        bool forceStop = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_workerMode != WorkerMode::None && g_workerDone) {
                const WorkerMode completedMode = g_workerMode;
                completedWorker = std::move(g_worker);
                g_workerMode = WorkerMode::None;
                g_workerDone = false;
                if (completedMode == WorkerMode::Starting) {
                    g_running = g_workerSuccess;
                    if (!g_workerSuccess) {
                        g_failed = true;
                        g_draining = false;
                    } else if (!g_wanted || g_draining) {
                        g_draining = true;
                        stopAccepting = true;
                    }
                } else {
                    g_running = false;
                    g_draining = false;
                }
            }

            if (g_running && !g_wanted && !g_draining) {
                g_draining = true;
                stopAccepting = true;
            }
            if (!g_running && !g_wanted && g_workerMode == WorkerMode::None) {
                g_draining = false;
            }
            if (!g_running && g_wanted && !g_draining && g_workerMode == WorkerMode::None && !g_failed) {
                if (!g_ownerPassword.empty()) {
                    startWorkerLocked();
                } else {
                    g_failed = true;
                }
            }
            checkDrain = g_running && g_draining && g_workerMode == WorkerMode::None;
            forceStop = g_shuttingDown;
        }

        if (completedWorker.joinable()) {
            completedWorker.join();
        }
        if (stopAccepting) {
            p2play_stop_accepting();
        }
        // At process exit Core::stop cancels active sockets and torrent waits.
        // Waiting for zero clients first deadlocks on an incomplete HTTP request
        // or a client still streaming. Source switching keeps graceful draining.
        if (checkDrain) {
            const int activeRequests = p2play_active_requests();
            const int activeStreams = p2play_active_streams();
            if (forceStop || (activeRequests == 0 && activeStreams == 0)) {
                Utility::log(Utility::LogLevel::Info, "P2Play: embedded drain complete, active_requests=" +
                        std::to_string(activeRequests) + " active_streams=" + std::to_string(activeStreams));
            }
        }
        if (checkDrain && (forceStop || (p2play_active_requests() == 0 && p2play_active_streams() == 0))) {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_running && g_draining && g_workerMode == WorkerMode::None) {
                stopWorkerLocked();
            }
        }
    }

    bool isReady() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_running && g_wanted && !g_draining && g_workerMode == WorkerMode::None &&
               p2play_http_port() == 8090;
    }

    bool waitUntilReady(int timeoutSeconds) {
        const auto deadline = std::chrono::steady_clock::now()
                              + std::chrono::seconds(std::max(0, timeoutSeconds));
        for (;;) {
            update();
            if (isReady()) {
                return true;
            }
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (!g_wanted || g_failed) {
                    return false;
                }
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_shuttingDown = true;
        }
        Utility::shutdownStage("P2Play: shutdown begin");
        deactivate();
        for (;;) {
            update();
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (!g_running && g_workerMode == WorkerMode::None && !g_worker.joinable()) {
                    Utility::shutdownStage("P2Play: shutdown complete");
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
