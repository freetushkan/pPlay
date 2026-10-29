//
// Created by cpasjuste on 03/10/18.
//

#ifndef PPLAY_PLAYER_H
#define PPLAY_PLAYER_H

#include "menus/menu_video_submenu.h"
#include <atomic>
#include "media_file.h"
#include "mpv.h"
#include "external_tracks.h"

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

    // progress reset by the user: don't write it back for the current playback
    void forgetProgress();

    // comparable key of a path (as given to mpv): playing mark, history
    static std::string keyOf(const std::string &path);

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

    MenuVideoSubmenu *getMenuNormalize();

    // volume normalization changed (settings or player menu)
    void setAudioNormalize(int mode);

    const std::string &getTitle() const;
    bool hasVideo() const;
    bool isPlaylistFile() const;
    const std::vector<MediaFile> &getAutoplayFiles() const;

    bool onInput(c2d::Input::Player *players) override;

    // "Add file..." of the audio / subtitles menus: pick a file in the filer
    void pickExternalTrack(bool audio);

private:

    void onUpdate() override;

    void onLoadEvent();

    void onStopEvent(int reason);

    bool scheduleReload(const std::string &reason, bool counted);

    void updateWatchdog();

    void updateMpvLog();

    void onMpvLog(const mpv_event_log_message *message);

    // preferred track languages (settings, or mpv.conf)
    void applyLanguages();

    // audio / subtitles selection menus
    void buildTrackMenus();

    // external tracks: user bindings, then the files found next to the video
    void startExternalTracks();

    static int externalFindThread(void *data);

    void updateExternalTracks();

    void addExternalTrack(const pplay::ExternalTracks::Track &track);

    void onExternalTracksAdded();

    // a track chosen in a menu: remembered if external
    void rememberSelection(bool audio, int trackId);

    // watch later save (aid / sid left out while an external track is selected)
    void saveProgress();

    void selectAudioIfNone();

    // tracks chosen in the menus, applied to the next files with the same tracks
    struct TrackChoice {
        bool set = false;
        int id = -1; // -1: off
        std::string layout;
    };
    TrackChoice trackChoice[2];
    bool trackChoiceApplied[2] = {false, false};

    // internal tracks of a type ("audio", "sub"): ids and languages
    std::string trackLayout(const std::string &type);

    void rememberTrackChoice(bool audio, int trackId);

    void applyTrackChoices();
    bool watchLaterSkip[2] = {false, false};

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
    MenuVideoSubmenu *menuNormalize = nullptr;
    int appliedQuality = -1;
    MediaFile file;
    std::string playingKey;
    std::vector<MediaFile> autoplayFiles;
    int retryCount = 0;
    long lastProgressSave = 0;
    bool progressForgotten = false;
    long lastKnownDuration = 0;
    long lastKnownPosition = 0;
    long startPlaybackPosition = -1;
    c2d::C2DClock messageClock;
    bool frameDrawn = false;
    // the file playback restart event seen (first frame / position reached)
    bool playbackStarted = false;
    // stopped by the watchdog, retries exhausted
    bool stalledStop = false;

    bool reloadPending = false;
    int reloadDelayMs = 0;
    double reloadPosition = 0;
    bool reloadPaused = false;
    bool skipStopEvent = false;
    c2d::C2DClock reloadClock;
    int mpvLogLevel = -1;
    double watchTime = -1;
    long watchFrame = -1;
    double watchCache = -1;
    c2d::C2DClock watchLogClock;
    double lastTime = 0;
    bool userPaused = false;
    c2d::C2DClock stallClock;
    c2d::C2DClock progressClock;

    // playback information (Start toggles it), updated while shown
    c2d::Text *info_text = nullptr;
    c2d::Text *info_keys = nullptr;

    // current track ("audio", "sub"): "#id lang «title» (external)", or "Off"
    std::string trackInfo(const std::string &type);

    void updateKeysInfo();
    c2d::C2DClock infoClock;

    void updateInfo();

    // volume normalization (lavfi, labelled @pplaynorm), by the decoded channels;
    // AUDIO_FILTER_UNKNOWN: a new file (an old watch_later may have restored one)
    std::string appliedAudioFilter;

    void applyAudioFilter();

    // volume saved in the settings, a bit after the last change
    bool volumeDirty = false;
    c2d::C2DClock volumeClock;

    void saveVolume();

    // external tracks
    std::string confAudioLang;
    std::string confSubLang;
    std::string appliedAudioLang = "\x01";
    std::string appliedSubLang = "\x01";
    bool hadWatchLater = false;
    int externalGeneration = 0;
    int externalPending = 0;
    std::vector<std::string> externalAdded;
    std::vector<pplay::ExternalTracks::Track> externalBindings;
    std::vector<pplay::ExternalTracks::Track> externalAll;
    std::string externalSelect[2];  // url to select once added (audio, subtitles)
    c2d::C2DThread *externalThread = nullptr;
    std::atomic<bool> externalDone{false};
    bool externalRestart = false;
    int externalJobGeneration = 0;
    std::string externalJobPath;
    std::string externalJobName;
    std::vector<pplay::ExternalTracks::Track> externalFound;

    // player
    VideoTexture *texture = nullptr;
    Mpv *mpv;

    bool fullscreen = false;
    mutable std::string displayTitle;
};

#endif //PPLAY_PLAYER_H
