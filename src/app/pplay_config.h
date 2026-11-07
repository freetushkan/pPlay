//
// Created by cpasjuste on 22/10/18.
//

#ifndef PPLAY_CONFIG_H
#define PPLAY_CONFIG_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"
#include "cross2d/skeleton/config.h"

// NETWORKx, NETWORKx_LAST, NETWORKx_NAME, NETWORKx_TYPE
#define PPLAY_MAX_NETWORKS 9
// 2: home moved to sdmc:/media/ (Switch), /data/media/ (PS4, PS5)
#define PPLAY_CONFIG_VERSION 2
#define OPT_NETWORK "NETWORK1"
#define OPT_NETWORK1 "NETWORK1"
#define OPT_HOME_PATH "HOME_PATH"
#define OPT_LAST_LOCAL_PATH "LAST_LOCAL_PATH"
#define OPT_LAST_NETWORK_PATH "NETWORK1_LAST"
#define OPT_LAST_MODULE "LAST_MODULE"
#define OPT_NETWORK_TIMEOUT "NETWORK_TIMEOUT"
#define OPT_NETWORK_RETRIES "NETWORK_RETRIES"
#ifdef __SMB2__
#define OPT_SMB_READ_BUFFER_MB "SMB_READ_BUFFER_MB"
#endif
#define OPT_AUTOPLAY_MODE "AUTOPLAY_MODE"
#define OPT_LOG_LEVEL "LOG_LEVEL"
#define OPT_SWAP_CONTROLS "SWAP_CONTROLS"
#define OPT_SEEK_SHORT_SEC "SEEK_SHORT_SEC"
#define OPT_SEEK_LONG_SEC "SEEK_LONG_SEC"
#define OPT_CPU_BOOST "CPU_BOOST"
#define OPT_ACCENT_COLOR "ACCENT_COLOR"
#define OPT_LANGUAGE "LANGUAGE"
// preferred track languages: mpv alang / slang list, "Auto" / "Off" (mpv.conf)
#define OPT_AUDIO_LANGUAGE "AUDIO_LANGUAGE"
#define OPT_SUB_LANGUAGE "SUB_LANGUAGE"
// external audio / subtitles files found next to the video
#define OPT_EXTERNAL_TRACKS "EXTERNAL_TRACKS"
// 0 off, 1 normalize, 2 night mode (dialogue up, no LFE, compressed)
#define OPT_AUDIO_NORMALIZE "AUDIO_NORMALIZE"
// last volume (-1: mpv.conf)
#define OPT_VOLUME "VOLUME"
// network cache: read ahead (seconds), memory (MB)
#define OPT_CACHE_SECS "CACHE_SECS"
#define OPT_CACHE_MB "CACHE_MB"
// 0 best (mpv.conf), 1 balanced, 2 fast (see Mpv::setQualityPreset)
#define OPT_VIDEO_QUALITY "VIDEO_QUALITY"
// media information read by itself: 0 the panel opened only, 2 the selected file,
// 1 the selected one and the files with progress (their durations)
#define OPT_MEDIA_PROBE "MEDIA_PROBE"
// audio files screen: 0 cover > visualizer > icon, 1 visualizer > icon, 2 icon
#define OPT_AUDIO_VISUALIZER "AUDIO_VISUALIZER"
// code page of the old tags / file names, 0: guessed
#define OPT_TEXT_ENCODING "TEXT_ENCODING"
// playback position kept by TorrServer (its "track timecode" setting) read and sent
#define OPT_TS_PROGRESS "TS_PROGRESS_SYNC"
// thin progress line under the hidden panel (and on the minimized player)
#define OPT_PROGRESS_LINE "PROGRESS_LINE"
// progress kept for files from this length (minutes), saved every n seconds
#define OPT_PROGRESS_MIN_LENGTH "PROGRESS_MIN_LENGTH"
#define OPT_PROGRESS_INTERVAL "PROGRESS_INTERVAL"
// PS5 rest mode: 0 pPlay closes once the system resumes it, 1 it stays (unstable)
#define OPT_PS5_KEEP_IN_REST "PS5_KEEP_IN_REST"
// closed while playing (any exit): "<fullscreen 0/1>|<the playing file>", reopened at the next start
#define OPT_REST_SESSION "REST_SESSION"
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
#define OPT_PS5_SLEEP_TIMER "PS5_SLEEP_TIMER"
// no automatic sleep while playing / saving: until that many minutes without a key pressed,
// 0 always (not on PS5: it never sleeps with the app open)
#define OPT_STAY_AWAKE "STAY_AWAKE"
// ".name" files and directories listed
#define OPT_SHOW_HIDDEN "SHOW_HIDDEN"
// the playlist added to last, the last location in the playlists
#define OPT_LAST_PLAYLIST "LAST_PLAYLIST"
#define OPT_LAST_PLAYLISTS_PATH "LAST_PLAYLISTS_PATH"
// hardware video decoding (Switch: nvtegra, Linux: vaapi / nvdec, Android: mediacodec), 0 off
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

class PPLAYConfig : public c2d::config::Config {

public:

    explicit PPLAYConfig(pplay::Io *io, int version = PPLAY_CONFIG_VERSION);

    // re-read the file, false on a parse error
    bool reload();

    static const char *networkOption(int index);
    static const char *networkLastOption(int index);
    static const char *networkNameOption(int index);
    static const char *networkTypeOption(int index);

private:

    // fix invalid values, apply the accent color
    void sanitize();

    // one-time changes for a config saved by an older version
    void migrate(int from);

    std::string defaultHomePath() const;

    pplay::Io *io;
};

#endif //PPLAY_CONFIG_H
