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
#define PPLAY_MAX_NETWORKS      9
#define OPT_NETWORK             "NETWORK1"
#define OPT_NETWORK1            "NETWORK1"
#define OPT_HOME_PATH           "HOME_PATH"
#define OPT_LAST_LOCAL_PATH     "LAST_LOCAL_PATH"
#define OPT_LAST_NETWORK_PATH   "NETWORK1_LAST"
#define OPT_LAST_MODULE         "LAST_MODULE"
#define OPT_NETWORK_TIMEOUT     "NETWORK_TIMEOUT"
#define OPT_NETWORK_RETRIES     "NETWORK_RETRIES"
#ifdef __SMB2__
#define OPT_SMB_READ_BUFFER_MB  "SMB_READ_BUFFER_MB"
#endif
#define OPT_AUTOPLAY_MODE       "AUTOPLAY_MODE"
#ifdef PPLAY_ENABLE_SCRAPPING
#define OPT_ENABLE_SCRAPPING    "ENABLE_SCRAPPING"
#define OPT_TMDB_LANGUAGE       "TMDB_LANGUAGE"
#define OPT_CACHE_MEDIA_INFO    "CACHE_MEDIA_INFO"
#endif
#define OPT_LOG_LEVEL           "LOG_LEVEL"
#define OPT_SWAP_CONTROLS       "SWAP_CONTROLS"
#define OPT_SEEK_SHORT_SEC      "SEEK_SHORT_SEC"
#define OPT_SEEK_LONG_SEC       "SEEK_LONG_SEC"
#define OPT_CPU_BOOST           "CPU_BOOST"
#define OPT_ACCENT_COLOR        "ACCENT_COLOR"
#define OPT_PREVIEW_PANEL       "PREVIEW_PANEL"
#define OPT_LANGUAGE            "LANGUAGE"
// preferred track languages: mpv alang / slang list, "Auto" / "Off" (mpv.conf)
#define OPT_AUDIO_LANGUAGE      "AUDIO_LANGUAGE"
#define OPT_SUB_LANGUAGE        "SUB_LANGUAGE"
// external audio / subtitles files found next to the video
#define OPT_EXTERNAL_TRACKS     "EXTERNAL_TRACKS"

namespace pplay {
    class Io;
}

class PPLAYConfig : public c2d::config::Config {

public:

    explicit PPLAYConfig(pplay::Io *io, int version = 1);

    // re-read the file, false on a parse error
    bool reload();


    static const char *networkOption(int index);
    static const char *networkLastOption(int index);
    static const char *networkNameOption(int index);
    static const char *networkTypeOption(int index);

private:

    // fix invalid values, apply the accent color
    void sanitize();

    pplay::Io *io;
};

#endif //PPLAY_CONFIG_H
