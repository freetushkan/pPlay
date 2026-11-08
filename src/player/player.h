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

    // before a close: the position saved now (the periodic save may be seconds old),
    // TorrServer included (waited for)
    void keepProgressNow();

    // the next load stays paused (a restored session)
    void loadPausedOnce() { loadPaused = true; }

    const MediaFile &getFile() const { return file; }

    // comparable key of a path (as given to mpv): playing mark, history
    static std::string keyOf(const std::string &path);

    void pause();

    void resume();

    void stop();

    // the previous / next file (or playlist entry), false if none
    bool playSibling(int direction);

    void applyConfig();

    // the decoder and the video quality of the current file (the player menu), started
    // from the settings; Auto: changed by the playback (a stuck hardware decoder, drops)
    static bool hasHardwareDecoding();

    bool isHardwareDecoding() const { return decoderHardware; }

    bool isDecoderAuto() const { return decoderAuto; }

    // 0 best, 1 balanced, 2 fast
    int getVideoQuality() const { return videoQuality; }

    bool isQualityAuto() const { return qualityAuto; }

    // chosen in the player menu: no more Auto for it (this file)
    void setHardwareDecoding(bool hardware);

    void setVideoQuality(int quality);

    // pplay::InputMap media buttons
    void onMediaKeys(unsigned int keys);

    void setSpeed(double speed);

    void changeVolume(double delta);

    void changeBrightness(double delta);

    // the minimized player frame (each frame, by Main)
    void updateFrame();

    // false: no backlight here
    bool changeBacklight(int direction);

    void resetBrightness();

    // playback information overlay (Start)
    bool isInfoVisible();

    void setInfoVisible(bool visible);

    bool isFullscreen();

    // the saved copy of the file played (no network)
    bool isSavedCopy() const { return !copyPath.empty(); }

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

    // void showMessage(const std::string &message);

    // the player menu or one of its submenus
    bool isMenuVisible() const;

    // Called after Main has finished traversing its UI children.
    void processDeferredMenus();

    Mpv *getMpv();

    MenuVideoSubmenu *getMenuVideoStreams();

    MenuVideoSubmenu *getMenuAudioStreams();

    MenuVideoSubmenu *getMenuSubtitlesStreams();

    MenuVideoSubmenu *getMenuPlaylist();

    MenuVideoSubmenu *getMenuPlaybackMode();

    MenuVideoSubmenu *getMenuNormalize();

    MenuVideoSubmenu *getMenuVisualizer();

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

    // the first frame (or the audio screen) shown: the panel may appear
    bool isPlaybackStarted() const { return playbackStarted; }

    bool isPlaylistFile() const;

    // the file the playback mode goes to at the end, "" if none
    std::string nextTitle() const;

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
    // wait: false for the periodic one (mpv writes it in its thread)
    void saveProgress(bool wait = true);

    bool syncsProgress() const;

    // the position is worth keeping (settings: file length, the last minute)
    bool keepsProgress(long position, long duration) const;

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

    // player_decoding.cpp: the decoder and the quality of a new file (the settings)
    void startDecoding();

    // decoder (hardware: false, its reinit) and quality changed, paused meanwhile
    void switchDecoding(bool hardware, int quality, const std::string &reason);

    void applyQuality(int quality);

    // each frame: the end of a switch, a hardware decoder given up by mpv, drops, a retry
    void updateDecoding();

    // player_controls.cpp: playback information (Start toggles it)
    void updateInfo();

    void updateKeysInfo();

    // current track ("audio", "sub"): "#id lang «title» (external)", or "Off"
    // playing, paused, buffering... (the information overlay)
    std::string stateText();

    std::string trackInfo(const std::string &type);

    Main *main = nullptr;
    Mpv *mpv = nullptr;
    c2d::RectangleShape *frame = nullptr;
    VideoTexture *texture = nullptr;
    PlayerOSD *osd = nullptr;
    bool fullscreen = false;
    c2d::TweenScale *tweenScale = nullptr;
    c2d::TweenPosition *tweenPosition = nullptr;

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

    // Main's child vector must not be mutated from Player::onUpdate().
    bool menusBuildPending = false;
    bool menusTrackBuildPending = false;
    bool menusDeletePending = false;
    bool menusDeleteAllPending = false;

    // the file
    MediaFile file;
    std::string playingKey;
    std::vector<MediaFile> autoplayFiles;
    mutable std::string displayTitle;
    // decoding (player_decoding.cpp)
    bool decoderHardware = false;
    bool decoderAuto = false;
    int videoQuality = 0;
    bool qualityAuto = false;
    // the quality before software decoding (back with the hardware decoder)
    int qualityBeforeSoftware = 0;
    // the hardware decoder worked for this file (else: not for this codec, no retry)
    bool hardwareSeen = false;
    int softwareChecks = 0;
    int hardwareFallbacks = 0;
    bool hardwareRetryPending = false;
    c2d::C2DClock hardwareRetryClock;
    bool switching = false;
    bool switchPaused = false;
    c2d::C2DClock switchClock;
    c2d::C2DClock decodingClock;
    // the Auto settings (at the start of a file)
    double dropsShare = 0.1;
    double dropsWindow = 10;
    float qualityDelay = 30;
    float hardwareRetrySecs = 60;
    int hardwareRetries = 3;
    // after a decoder change: no quality check for a while
    bool qualityHold = false;
    c2d::C2DClock qualityHoldClock;
    // drops: counted over a window of playback
    long dropsStart = -1;
    long framesStart = -1;
    double dropsTime = -1;
    double dropsLastTime = -1;
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
    bool loadPaused = false;
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
    // the saved copy mpv opens for this url (the progress, the history: the network file)
    std::string copyUrl;
    std::string copyPath;

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
