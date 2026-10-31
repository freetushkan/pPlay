//
// Created by cpasjuste on 03/10/18.
//

#ifndef PPLAY_PLAYER_H
#define PPLAY_PLAYER_H

#include <atomic>
#include "menu_video_submenu.h"
#include "media_file.h"
#include "mpv.h"
#include "external_tracks.h"

class Main;

class PlayerOSD;

class VideoTexture;

class AudioView;

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

    // the previous / next file (or playlist entry), false if none
    bool playSibling(int direction);

    // settings changed
    void applyConfig();

    // pplay::InputMap media buttons
    void onMediaKeys(unsigned int keys);

    void setSpeed(double speed);

    void changeVolume(double delta);

    void changeBrightness(double delta);

    void resetBrightness();

    // playback information overlay (Start)
    bool isInfoVisible();

    void setInfoVisible(bool visible);

    bool isFullscreen();

    void setFullscreen(bool maximize, bool hide = false);

    void setVideoStream(int streamId);

    void setAudioStream(int streamId);

    void setSubtitleStream(int streamId);

    int getVideoStream();

    int getAudioStream();

    int getSubtitleStream();

    // volume normalization changed (settings or player menu)
    void setAudioNormalize(int mode);

    // "Add file..." of the audio / subtitles menus: pick a file in the filer
    void pickExternalTrack(bool audio);

    void showMessage(const std::string &message);

    PlayerOSD *getOSD();

    // the player menu or one of its submenus
    bool isMenuVisible() const;

    Mpv *getMpv();

    MenuVideoSubmenu *getMenuVideoStreams();

    MenuVideoSubmenu *getMenuAudioStreams();

    MenuVideoSubmenu *getMenuSubtitlesStreams();

    MenuVideoSubmenu *getMenuPlaylist();

    MenuVideoSubmenu *getMenuPlaybackMode();

    MenuVideoSubmenu *getMenuNormalize();

    MenuVideoSubmenu *getMenuVisualizer();

    MenuVideoSubmenu *getMenuSpeed();

    MenuVideoSubmenu *getMenuBrightness();

    // what an audio file shows (None: a video file, or stopped)
    enum class AudioScreen {
        None,
        Picture,
        Visualizer,
        Note
    };

    AudioScreen getAudioScreen() const { return audioScreen; }

    // audio screen setting changed (settings or player menu): 0 cover > visualizer > note,
    // 1 visualizer > note, 2 note
    void setAudioScreenMode(int mode);

    // the cover candidates of the audio file (PosterLoader urls), in order
    const std::vector<std::string> &getPictureUrls() const { return pictureUrls; }

    // none of them could be shown
    void onPicturesFailed();

    const std::string &getTitle() const;

    bool hasVideo() const;

    bool isPlaylistFile() const;

    const std::vector<MediaFile> &getAutoplayFiles() const;

    bool onInput(c2d::Input::Player *players) override;

private:

    // mpv_command_async user data of the external tracks (+ generation)
    static constexpr uint64_t ExternalReply = 0x4558540000000000ULL;
    // appliedAudioFilter of a new file: a @pplaynorm filter may be there (old watch_later)
    static constexpr const char *AudioFilterUnknown = "?";

    void onUpdate() override;

    // player.cpp
    void onLoadEvent();

    // player_menus.cpp
    void buildMenus();

    void buildTrackMenus();

    std::vector<MenuVideoSubmenu *> menus() const;

    void hideMenus();

    // the file ones, all on exit
    void deleteMenus(bool all);

    // player_playback.cpp
    void onMpvEvent(mpv_event *event);

    void onStopEvent(int reason);

    bool scheduleReload(const std::string &reason, bool counted);

    void updateWatchdog();

    // watch later save (aid / sid left out while an external track is selected), the
    // TorrServer position too
    void saveProgress();

    bool syncsProgress() const;

    void updateMpvLog();

    void onMpvLog(const mpv_event_log_message *message);

    // player_tracks.cpp: preferred languages (settings, or mpv.conf)
    void applyLanguages();

    // external tracks: user bindings, then the files found next to the video
    void startExternalTracks();

    static int externalFindThread(void *data);

    void updateExternalTracks();

    void addExternalTrack(const pplay::ExternalTracks::Track &track);

    void onExternalTracksAdded();

    // a track chosen in a menu: remembered if external
    void rememberSelection(bool audio, int trackId);

    // internal tracks of a type ("audio", "sub"): ids and languages
    std::string trackLayout(const std::string &type);

    // tracks chosen in the menus, applied to the next files with the same tracks
    void rememberTrackChoice(bool audio, int trackId);

    void applyTrackChoices();

    void selectAudioIfNone();

    // player_audio.cpp: lavfi filter labelled @pplaynorm, by the decoded channels
    void applyAudioFilter();

    // volume saved in the settings, a bit after the last change
    void saveVolume();

    // player_visual.cpp: the audio screen of the file, its mpv settings
    void updateAudioScreen();

    void clearAudioScreen();

    std::vector<std::string> pictureUrlsOf(const MediaFile &file);

    std::string visualizerGraph() const;

    // player_controls.cpp: playback information (Start toggles it)
    void updateInfo();

    void updateKeysInfo();

    // current track ("audio", "sub"): "#id lang «title» (external)", or "Off"
    std::string trackInfo(const std::string &type);

    Main *main = nullptr;
    Mpv *mpv = nullptr;
    VideoTexture *texture = nullptr;
    PlayerOSD *osd = nullptr;
    bool fullscreen = false;
    c2d::TweenScale *tweenScale = nullptr;
    c2d::TweenPosition *tweenPosition = nullptr;

    c2d::Text *message_text = nullptr;
    c2d::C2DClock messageClock;
    c2d::Text *info_text = nullptr;
    c2d::Text *info_keys = nullptr;
    c2d::C2DClock infoClock;

    MenuVideoSubmenu *menuVideoStreams = nullptr;
    MenuVideoSubmenu *menuAudioStreams = nullptr;
    MenuVideoSubmenu *menuSubtitlesStreams = nullptr;
    MenuVideoSubmenu *menuPlaylist = nullptr;
    MenuVideoSubmenu *menuPlaybackMode = nullptr;
    MenuVideoSubmenu *menuNormalize = nullptr;
    MenuVideoSubmenu *menuVisualizer = nullptr;
    MenuVideoSubmenu *menuSpeed = nullptr;
    MenuVideoSubmenu *menuBrightness = nullptr;

    // the file
    MediaFile file;
    std::string playingKey;
    std::vector<MediaFile> autoplayFiles;
    mutable std::string displayTitle;
    int appliedQuality = -1;
    long startPlaybackPosition = -1;
    bool frameDrawn = false;
    // the playback restart event seen (first frame / position reached)
    bool playbackStarted = false;

    // progress
    long lastProgressSave = 0;
    bool progressForgotten = false;
    long lastKnownDuration = 0;
    long lastKnownPosition = 0;
    bool watchLaterSkip[2] = {false, false};

    // reloads and the stall watchdog
    int retryCount = 0;
    bool reloadPending = false;
    int reloadDelayMs = 0;
    double reloadPosition = 0;
    bool reloadPaused = false;
    bool skipStopEvent = false;
    // stopped by the watchdog, retries exhausted
    bool stalledStop = false;
    c2d::C2DClock reloadClock;
    double watchTime = -1;
    long watchFrame = -1;
    double watchCache = -1;
    double lastTime = 0;
    bool userPaused = false;
    c2d::C2DClock watchLogClock;
    c2d::C2DClock stallClock;
    c2d::C2DClock progressClock;
    int mpvLogLevel = -1;

    // audio
    AudioView *audioView = nullptr;
    AudioScreen audioScreen = AudioScreen::None;
    std::string appliedVisualizer;
    std::vector<std::string> pictureUrls;
    bool picturesFailed = false;
    std::string appliedAudioFilter;
    bool volumeDirty = false;
    c2d::C2DClock volumeClock;

    // tracks
    struct TrackChoice {
        bool set = false;
        int id = -1; // -1: off
        std::string layout;
    };
    TrackChoice trackChoice[2];
    bool trackChoiceApplied[2] = {false, false};
    std::string confAudioLang;
    std::string confSubLang;
    std::string appliedAudioLang = "\x01";
    std::string appliedSubLang = "\x01";
    bool hadWatchLater = false;

    // external tracks
    int externalGeneration = 0;
    int externalPending = 0;
    std::vector<std::string> externalAdded;
    std::vector<pplay::ExternalTracks::Track> externalBindings;
    std::vector<pplay::ExternalTracks::Track> externalAll;
    std::string externalSelect[2]; // url to select once added (audio, subtitles)
    c2d::C2DThread *externalThread = nullptr;
    std::atomic<bool> externalDone{false};
    bool externalRestart = false;
    int externalJobGeneration = 0;
    std::string externalJobPath;
    std::string externalJobName;
    std::vector<pplay::ExternalTracks::Track> externalFound;
};

#endif //PPLAY_PLAYER_H
