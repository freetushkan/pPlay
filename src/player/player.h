//
// Created by cpasjuste on 03/10/18.
//

#ifndef PPLAY_PLAYER_H
#define PPLAY_PLAYER_H

#include "menus/menu_video_submenu.h"
#include "media_file.h"
#include "mpv.h"

class Main;

class PlayerOSD;

class VideoTexture;

class Player : public c2d::Rectangle {

public:

    explicit Player(Main *main);

    ~Player() override;

    bool load(const MediaFile &file, bool firstTry = true, const std::string &options = "pause=yes");

    // file opened in the player? (paths may be spelled differently)
    bool isPlaying(const std::string &path);

    void pause();

    void resume();

    void stop();

    // settings changed
    void applyConfig();

    // pplay::InputMap media buttons
    void onMediaKeys(unsigned int keys);

    void setSpeed(double speed);

    void changeVolume(double delta);

    void changeBrightness(double delta);

    bool isFullscreen();

    void setFullscreen(bool maximize, bool hide = false);

    void setVideoStream(int streamId);

    void setAudioStream(int streamId);

    void setSubtitleStream(int streamId);

    int getVideoStream();

    int getAudioStream();

    int getSubtitleStream();

    void showMessage(const std::string &message);

    PlayerOSD *getOSD();

    Mpv *getMpv();

    MenuVideoSubmenu *getMenuVideoStreams();

    MenuVideoSubmenu *getMenuAudioStreams();

    MenuVideoSubmenu *getMenuSubtitlesStreams();

    MenuVideoSubmenu *getMenuPlaylist();

    MenuVideoSubmenu *getMenuPlaybackMode();

    const std::string &getTitle() const;
    bool hasVideo() const;
    bool isPlaylistFile() const;
    const std::vector<MediaFile> &getAutoplayFiles() const;

    bool onInput(c2d::Input::Player *players) override;

private:

    void onUpdate() override;

    void onLoadEvent();

    void onStopEvent(int reason);

    bool scheduleReload(const std::string &reason, bool counted);

    void updateWatchdog();

    // ui
    Main *main = nullptr;
    PlayerOSD *osd = nullptr;
    c2d::Text *message_text = nullptr;
    c2d::TweenScale *tweenScale = nullptr;
    c2d::TweenPosition *tweenPosition = nullptr;
    MenuVideoSubmenu *menuVideoStreams = nullptr;
    MenuVideoSubmenu *menuAudioStreams = nullptr;
    MenuVideoSubmenu *menuSubtitlesStreams = nullptr;
    MenuVideoSubmenu *menuPlaylist = nullptr;
    MenuVideoSubmenu *menuPlaybackMode = nullptr;
    MediaFile file;
    std::string playingKey;
    std::vector<MediaFile> autoplayFiles;
    int retryCount = 0;
    long lastProgressSave = 0;
    long lastKnownDuration = 0;
    long lastKnownPosition = 0;
    long startPlaybackPosition = -1;
    c2d::C2DClock messageClock;
    bool frameDrawn = false;

    bool reloadPending = false;
    int reloadDelayMs = 0;
    double reloadPosition = 0;
    bool reloadPaused = false;
    bool skipStopEvent = false;
    c2d::C2DClock reloadClock;
    double watchTime = -1;
    double lastTime = 0;
    bool userPaused = false;
    c2d::C2DClock stallClock;
    c2d::C2DClock progressClock;

    // player
    VideoTexture *texture = nullptr;
    Mpv *mpv;

    bool fullscreen = false;
    mutable std::string displayTitle;
};

#endif //PPLAY_PLAYER_H
