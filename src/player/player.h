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

    void pause();

    void resume();

    void stop();

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
    std::vector<MediaFile> autoplayFiles;
    int retryCount = 0;
    long lastProgressSave = 0;
    long lastKnownDuration = 0;
    long lastKnownPosition = 0;
    c2d::C2DClock pauseClock;
    c2d::C2DClock messageClock;
#ifdef __PS4__
    bool pausedHttpsStream = false;
#endif

    // player
    VideoTexture *texture = nullptr;
    Mpv *mpv;

    bool fullscreen = false;
    mutable std::string displayTitle;
};

#endif //PPLAY_PLAYER_H
