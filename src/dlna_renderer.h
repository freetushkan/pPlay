#ifndef PPLAY_DLNA_RENDERER_H
#define PPLAY_DLNA_RENDERER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

class Main;

namespace pplay {

class DlnaRenderer {
public:
    enum class CommandType {
        Play,
        Pause,
        Stop,
        SeekRelative,
        SeekAbsolute,
        SetVolume,
        LoadUri
    };

    struct Command {
        CommandType type;
        std::string text;
        double value = 0.0;
    };

    struct State {
        bool stopped = true;
        bool paused = true;
        long position = 0;
        long duration = 0;
        int volume = 100;
        std::string title;
        std::string uri;
    };

    DlnaRenderer(const std::string &friendlyName, uint16_t requestedPort);
    ~DlnaRenderer();

    bool start();
    void stop();
    bool isRunning() const;
    uint16_t getHttpPort() const;

    void updateState(const State &state);
    bool popCommand(Command &command);

private:
    void enqueue(const Command &command);
    void ssdpLoop();
    void httpLoop();
    void sendNotify(const std::string &nts);
    void respondToSearch(const std::string &request, const std::string &remoteIp, uint16_t remotePort);
    void handleHttpClient(int clientFd);
    std::string buildDescriptionXml() const;
    std::string buildAvTransportScpdXml() const;
    std::string buildRenderingControlScpdXml() const;
    std::string handleSoap(const std::string &path, const std::string &request);
    std::string buildSoapResponse(const std::string &service, const std::string &action, const std::string &body) const;
    std::string buildSoapError(int code, const std::string &description) const;
    std::string locationUrl() const;
    std::string localIp() const;

    std::string friendlyName;
    std::string uuid;
    uint16_t requestedPort = 0;
    std::atomic<bool> running{false};
    int httpSocket = -1;
    int ssdpSocket = -1;
    uint16_t httpPort = 0;
    std::thread ssdpThread;
    std::thread httpThread;
    mutable std::mutex stateMutex;
    State state;
    std::mutex commandMutex;
    std::queue<Command> commands;
};

} // namespace pplay

#endif // PPLAY_DLNA_RENDERER_H
