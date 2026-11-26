//
// Created by cpasjuste on 22/10/18.
//

#ifndef PPLAY_CONFIG_H
#define PPLAY_CONFIG_H

#include <string>
#include <vector>
#include <map>
#include "cross2d/c2d.h"
#include "cross2d/skeleton/config.h"

// 2: home moved to sdmc:/media/ (Switch), /data/media/ (PS4, PS5); 3: pplay_config.json
#define PPLAY_CONFIG_VERSION 3
#define OPT_HOME_PATH "HOME_PATH"
#define OPT_LAST_LOCAL_PATH "LAST_LOCAL_PATH"
#define OPT_LAST_MODULE "LAST_MODULE"
#define OPT_NETWORK_TIMEOUT "NETWORK_TIMEOUT"
// TLS verification (mpv, curl, probes)
#define OPT_TLS_VERIFY "TLS_VERIFY"
#define OPT_WATCHED_TOLERANCE "WATCHED_TOLERANCE"
#define OPT_NETWORK_RETRIES "NETWORK_RETRIES"
#ifdef __SMB2__
#define OPT_SMB_READ_BUFFER_MB "SMB_READ_BUFFER_MB"
#endif
#define OPT_AUTOPLAY_MODE "AUTOPLAY_MODE"
#define OPT_AUTOPLAY_TYPE_FILTER "AUTOPLAY_TYPE_FILTER"
#define OPT_LOG_LEVEL "LOG_LEVEL"
#define OPT_SWAP_CONTROLS "SWAP_CONTROLS"
#define OPT_UI_SCALE "UI_SCALE"          // main screen layout (restart to apply)
#define OPT_OSD_SCALE "OSD_SCALE"        // player OSD (live)
#define OPT_MENU_SCALE "MENU_SCALE"      // menus (live)
#define OPT_SHOW_MENU_BUTTON "SHOW_MENU_BUTTON"
#define OPT_CPU_BOOST "CPU_BOOST"
#define OPT_ACCENT_COLOR "ACCENT_COLOR"
#define OPT_LANGUAGE "LANGUAGE"
// preferred track languages: mpv alang / slang list, "Auto" / "Off" (mpv.conf)
#define OPT_AUDIO_LANGUAGE "AUDIO_LANGUAGE"
#define OPT_SUB_LANGUAGE "SUB_LANGUAGE"
// external audio / subtitles files found next to the video
#define OPT_EXTERNAL_TRACKS "EXTERNAL_TRACKS"
#define OPT_SUB_ASS_PLAIN "SUB_ASS_PLAIN"
#define OPT_SUB_POSITION "SUB_POSITION"
#define OPT_SUB_FONT_SIZE "SUB_FONT_SIZE"
#define OPT_SUB_COLOR "SUB_COLOR"
#define OPT_SUB_OUTLINE_COLOR "SUB_OUTLINE_COLOR"
// 0 off, 1 normalize, 2 night mode (dialogue up, no LFE, compressed)
#define OPT_AUDIO_NORMALIZE "AUDIO_NORMALIZE"
// output channels: 0 stereo, 1 auto (the device ones), 2 5.1, 3 7.1
#define OPT_AUDIO_CHANNELS "AUDIO_CHANNELS"
// last volume (-1: mpv.conf)
#define OPT_VOLUME "VOLUME"
// network cache: read ahead (seconds), memory (MB)
#define OPT_CACHE_SECS "CACHE_SECS"
#define OPT_CACHE_MB "CACHE_MB"
// 0 best (mpv.conf), 1 balanced, 2 fast (see Mpv::setQualityPreset), 3 auto (lighter on drops);
// the start of a file, the player menu changes the current one
#define OPT_DITHER "DITHER"
#define OPT_VIDEO_QUALITY "VIDEO_QUALITY"
// auto quality: lighter over this share of frames dropped (%) in that time (s), not checked
// that long (s) after a decoder change
#define OPT_AUTO_DROPS "AUTO_DROPS"
#define OPT_AUTO_DROPS_TIME "AUTO_DROPS_TIME"
#define OPT_AUTO_QUALITY_DELAY "AUTO_QUALITY_DELAY"
// auto decoder: the hardware one tried again after (min, then doubled, 0 never), that many times
#define OPT_AUTO_HW_RETRY "AUTO_HW_RETRY"
#define OPT_AUTO_HW_RETRIES "AUTO_HW_RETRIES"
// auto: paused for a moment while the decoder or the quality changes
#define OPT_AUTO_SWITCH_PAUSE "AUTO_SWITCH_PAUSE"
// TorrServer: the torrents list and the viewed marks asked again after (s)
#define OPT_TS_VIEWED_SYNC "TS_VIEWED_SYNC"
// media information read by itself: 0 the panel opened only, 2 the selected file,
// 1 the selected one and the files with progress (their durations)
#define OPT_MEDIA_PROBE "MEDIA_PROBE"
// audio files screen: 0 cover > visualizer > icon, 1 visualizer > icon, 2 icon
#define OPT_AUDIO_VISUALIZER "AUDIO_VISUALIZER"
// code page of the old tags / file names, 0: guessed
#define OPT_TEXT_ENCODING "TEXT_ENCODING"
// Per-network server playback progress (legacy persisted key retained for compatibility).
#define OPT_SERVER_PROGRESS "TS_PROGRESS_SYNC"
#define OPT_TS_PROGRESS OPT_SERVER_PROGRESS
// thin progress line under the hidden panel (and on the minimized player)
#define OPT_PROGRESS_LINE "PROGRESS_LINE"
#define OPT_OSD_HIDE_TIME "OSD_HIDE_TIME"
// progress kept for files from this length (minutes), saved every n seconds
#define OPT_PROGRESS_MIN_LENGTH "PROGRESS_MIN_LENGTH"
#define OPT_PROGRESS_INTERVAL "PROGRESS_INTERVAL"
// PS5 rest mode: 0 pPlay closes once the system resumes it, 1 it stays (unstable)
#define OPT_PS5_KEEP_IN_REST "PS5_KEEP_IN_REST"
// closed while playing (any exit): "<fullscreen 0/1>|<the playing file>", reopened at the next start
#define OPT_REST_SESSION "REST_SESSION"
// the last playing file opened at the start: 0 never, 1 after the rest mode (PS5), 2 always
#define OPT_RESTORE_SESSION "RESTORE_SESSION"
// the list after it: 1 at the restored file, 0 at the last location (the module and its path)
#define OPT_RESTORE_LOCATION "RESTORE_LOCATION"
// the picture shown while seeking (seeks on the way): 2 always, 1 local files (saved copies
// too), 0 never (one seek at the end, paused meanwhile)
#define OPT_SEEK_PREVIEW "SEEK_PREVIEW"
// saved copies (<home>/.pplay_save): parallel downloads, kept days (0: forever, by the last
// access), size limit (GB, 0: none, the oldest access removed first)
#define OPT_SAVE_THREADS "SAVE_THREADS"
#define OPT_SAVE_KEEP_DAYS "SAVE_KEEP_DAYS"
#define OPT_SAVE_MAX_GB "SAVE_MAX_GB"
#define OPT_LAST_SAVED_PATH "LAST_SAVED_PATH"
// PS5: the interface and the video at 3840x2160 if the screen is 4K (1), else 1920x1080 (0,
// scaled to the screen); read at the start
#define OPT_PS5_UI_4K "PS5_UI_4K"
// PS5: rest mode after that many minutes without a key, 0 off
#define OPT_SLEEP_TIMER "SLEEP_TIMER"
#define OPT_STANDBY_TIME "STANDBY_TIME"
// no automatic sleep while playing / saving: until that many minutes without a key pressed,
// 0 always (not on PS5: it never sleeps with the app open)
// ".name" files and directories listed
#define OPT_SHOW_HIDDEN "SHOW_HIDDEN"
// list non-media files too
#define OPT_SHOW_ALL_FILES "SHOW_ALL_FILES"
// the playlist added to last, the last location in the playlists
#define OPT_LAST_PLAYLIST "LAST_PLAYLIST"
#define OPT_LAST_PLAYLISTS_PATH "LAST_PLAYLISTS_PATH"
// hardware video decoding (Switch: nvtegra, Linux: vaapi / nvdec, Android: mediacodec), 0 off,
// 1 on, 2 auto (software when it fails, retried later)
#if defined(__SWITCH__) || defined(__LINUX__) || defined(__ANDROID__)
#define PPLAY_HW_DECODING
#endif
#define OPT_HW_DECODING "HW_DECODING"
// Android, sent to the background: 0 kept (paused), 1 closed (the playing file reopened at the
// next start); read at the start
#define OPT_ANDROID_BACKGROUND "ANDROID_BACKGROUND"
// the Saved / Playlists sections in the main menu: 1 if not empty, 0 hidden
#define OPT_SHOW_SAVED "SHOW_SAVED"
#define OPT_SHOW_PLAYLISTS "SHOW_PLAYLISTS"
// directories marked watched in the list: 0 off, 1 by the marks of their files, 2 by progress too
#define OPT_FOLDER_MARKS "FOLDER_MARKS"
#ifdef __SWITCH__
#define CACHE_MB_MAX 500
#else
#define CACHE_MB_MAX 1000
#endif

namespace pplay {
    class Io;
}

// the settings in pplay_config.json, grouped as in the menu; the networks a list (their last
// location in them). The options keep their names (NETWORKn_* for the networks, n from 1).
class PPLAYConfig : public c2d::config::Config {

public:

    explicit PPLAYConfig(pplay::Io *io, int version = PPLAY_CONFIG_VERSION);

    // the file (pplay.cfg of an older version imported if none)
    bool load(const std::string &overridePath = {}) override;

    bool save() override;

    // re-read the file, false on a parse error
    bool reload();

    static const char *networkOption(int index);
    static const char *networkLastOption(int index);
    static const char *networkNameOption(int index);
    static const char *networkTypeOption(int index);
    static const char *networkUserOption(int index);
    static const char *networkPasswordOption(int index);
    static const char *networkTimeoutOption(int index);
    static const char *networkTlsOption(int index);
    // TorrServer: the torrents list and the viewed marks asked again after (s)
    static const char *networkSyncOption(int index);
    static const char *networkSmbBufferOption(int index);
    // TS Silo: its api token (got with the user and password, or typed)
    static const char *networkTokenOption(int index);

    // True only when neither the current nor legacy settings file existed on startup.
    bool needsDpiScale() const { return firstRun; }

    int getNetworkCount() const { return networkCount; }

    // Numeric defaults captured before loading user preferences, used by slider reset.
    float getDefaultNumber(const std::string &name, float fallback = 0.0f) const {
        auto found = defaultNumbers.find(name);
        return found == defaultNumbers.end() ? fallback : found->second;
    }

    // a new one at the end: its index
    int addNetwork();

    void removeNetwork(int index);

    // with the next one (1) or the previous one (-1), false at an end
    bool moveNetwork(int index, int direction);

    // its name, "Network n" if none
    std::string networkName(int index);

    // the urls in use (paths, progress keys) keep the user and password of a network in them, as
    // in the older versions; the file and the menu have them apart

    // an url without them
    static std::string withoutCredentials(const std::string &url);

    // its url as typed (its user and password, if any, taken to their options)
    void setNetworkUrl(int index, const std::string &url);

    // its user or password changed: its urls with the new ones
    void updateNetworkCredentials(int index);

    // the index of a NETWORKn / NETWORKn_USER / NETWORKn_PASSWORD option, else 0
    static int networkOf(const std::string &option, const char *suffix);

private:

    // fix invalid values, apply the accent color
    void sanitize();

    // pplay.cfg of an older version: its options, its networks set
    bool importOld();

    // the options of the networks up to this one
    void allocateNetworks(int count);

    void clearNetwork(int index);

    void copyNetwork(int from, int to);

    // a setting of a new network (from the old global one)
    int networkDefault(int field);

    // an url of a network with its user and password
    std::string withNetworkCredentials(int index, const std::string &url);

    std::string defaultHomePath() const;

    pplay::Io *io;
    bool firstRun = false;
    std::map<std::string, float> defaultNumbers;
    int networkCount = 0;
    int networkSlots = 0;
};

#endif //PPLAY_CONFIG_H
