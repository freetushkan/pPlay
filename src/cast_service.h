#ifndef PPLAY_CAST_SERVICE_H
#define PPLAY_CAST_SERVICE_H

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class Main;

namespace pplay {

class PPLAYCast {
public:
    enum class CommandType {
        Play,
        Pause,
        TogglePause,
        Stop,
        SeekRelative,
        VolumeRelative,
        LoadUrl
    };

    struct Command {
        CommandType type;
        double value = 0.0;
        std::string text;
    };

    explicit PPLAYCast(Main *main);
    ~PPLAYCast();

    void start();
    void stop();
    void reloadFromConfig();
    bool isRunning() const;
    std::vector<Command> popCommands();

private:
    void workerLoop();
    void enqueue(const Command &command);

    std::string buildDeviceDescription() const;
    std::string buildStatusJson() const;
    std::string buildDialResponse(const std::string &appName) const;
    std::string receiverName() const;
    std::string uuid() const;

    Main *main = nullptr;
    std::atomic<bool> running{false};
    int httpPort = 8008;
    int castPort = 8009;
    std::thread httpThread;
    std::thread ssdpThread;
    std::thread mdnsThread;
    std::thread castThread;
    mutable std::mutex queueMutex;
    std::queue<Command> pendingCommands;
};

}

#endif // PPLAY_CAST_SERVICE_H