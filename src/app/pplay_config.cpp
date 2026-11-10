//
// Created by cpasjuste on 22/10/18.
//

#include <cstdlib>
#include <cstring>
#include "io.h"
#include "pplay_config.h"
#include "utility.h"

using namespace c2d;

static const char *networkOptionName(int index, const char *suffix) {
    static std::vector<std::string> names[4];
    static const char *suffixes[4] = {"", "_LAST", "_NAME", "_TYPE"};
    int s = 0;
    while (s < 3 && strcmp(suffixes[s], suffix) != 0) {
        s++;
    }
    if (names[s].empty()) {
        for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
            names[s].push_back("NETWORK" + std::to_string(i) + suffixes[s]);
        }
    }
    if (index < 1 || index > PPLAY_MAX_NETWORKS) {
        index = 1;
    }
    return names[s][index - 1].c_str();
}

const char *PPLAYConfig::networkOption(int index) {
    return networkOptionName(index, "");
}

const char *PPLAYConfig::networkLastOption(int index) {
    return networkOptionName(index, "_LAST");
}

const char *PPLAYConfig::networkNameOption(int index) {
    return networkOptionName(index, "_NAME");
}

const char *PPLAYConfig::networkTypeOption(int index) {
    return networkOptionName(index, "_TYPE");
}

// old ts:// and tss:// TorrServe urls
static bool migrateTorrServeUrl(std::string &url) {
    if (Utility::startWith(url, "tss://")) {
        url = "https://" + url.substr(6);
        return true;
    }
    if (Utility::startWith(url, "ts://")) {
        url = "http://" + url.substr(5);
        return true;
    }
    return false;
}

PPLAYConfig::PPLAYConfig(pplay::Io *_io, int version)
    : Config("PPLAY", _io->getDataPath() + "pplay.cfg", version), io(_io) {

    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        addOption({networkOption(i), ""});
    }
    std::string homePath = defaultHomePath();
    addOption({OPT_HOME_PATH, homePath});
    addOption({OPT_LAST_LOCAL_PATH, homePath});
    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        addOption({networkLastOption(i), "/"});
        addOption({networkNameOption(i), "Network " + std::to_string(i)});
        addOption({networkTypeOption(i), "Auto"}); // Auto, TorrServe, Samba, FTP, HTTP, WebDAV
    }
    addOption({OPT_LAST_MODULE, "LOCAL"});
    addOption({OPT_NETWORK_TIMEOUT, (int) 15});
    addOption({OPT_TLS_VERIFY, (int) 1});
    addOption({OPT_NETWORK_RETRIES, (int) 3});
#ifdef __SMB2__
    addOption({OPT_SMB_READ_BUFFER_MB, (int) 10});
#endif
    addOption({OPT_AUTOPLAY_MODE, (int) 1});
    addOption({OPT_LOG_LEVEL, (int) 0}); // 0 OFF, 1 ERROR, 2 INFO, 3 DEBUG, 4 TRACE
    addOption({OPT_SWAP_CONTROLS, (int) 0});
    addOption({OPT_SEEK_SHORT_SEC, (float) 30.0});
    addOption({OPT_SEEK_LONG_SEC, (float) 300.0});
#ifdef __SWITCH__
    addOption({OPT_CPU_BOOST, "Disabled"}); // Disabled, Enabled
#endif
#ifdef PPLAY_HW_DECODING
    addOption({OPT_HW_DECODING, (int) 1});
#endif
    addOption({OPT_ACCENT_COLOR, "#1078C8"});
    addOption({OPT_LANGUAGE, "en"});
    addOption({OPT_AUDIO_LANGUAGE, "Auto"});
    addOption({OPT_SUB_LANGUAGE, "Off"});
    addOption({OPT_EXTERNAL_TRACKS, (int) 1});
    addOption({OPT_AUDIO_NORMALIZE, (int) 0});
    addOption({OPT_VOLUME, (int) -1});
    addOption({OPT_CACHE_SECS, (int) 60});
    addOption({OPT_CACHE_MB, (int) 150});
    addOption({OPT_VIDEO_QUALITY, (int) 1});
    addOption({OPT_AUTO_DROPS, (int) 10});
    addOption({OPT_AUTO_DROPS_TIME, (int) 10});
    addOption({OPT_AUTO_QUALITY_DELAY, (int) 30});
    addOption({OPT_AUTO_HW_RETRY, (int) 1});
    addOption({OPT_AUTO_HW_RETRIES, (int) 3});
    addOption({OPT_AUTO_SWITCH_PAUSE, (int) 0});
    addOption({OPT_TS_VIEWED_SYNC, (int) 30});
    addOption({OPT_MEDIA_PROBE, (int) 1});
    addOption({OPT_AUDIO_VISUALIZER, (int) 0});
    addOption({OPT_TEXT_ENCODING, (int) 0});
    addOption({OPT_TS_PROGRESS, (int) 2});
    addOption({OPT_PROGRESS_LINE, (int) 1});
    addOption({OPT_FOLDER_MARKS, (int) 1});
    addOption({OPT_SHOW_SAVED, (int) 1});
    addOption({OPT_ANDROID_BACKGROUND, (int) 0});
    addOption({OPT_SHOW_PLAYLISTS, (int) 1});
    addOption({OPT_PROGRESS_MIN_LENGTH, (int) 5});
    addOption({OPT_PROGRESS_INTERVAL, (int) 10});
    addOption({OPT_PS5_KEEP_IN_REST, (int) 0});
    addOption({OPT_REST_SESSION, ""});
    addOption({OPT_RESTORE_SESSION, (int) 2});
#ifdef __SWITCH__
    addOption({OPT_SAVE_THREADS, (int) 1});
#else
    addOption({OPT_SAVE_THREADS, (int) 2});
#endif
    addOption({OPT_SAVE_KEEP_DAYS, (int) 0});
    addOption({OPT_SAVE_MAX_GB, (int) 0});
    addOption({OPT_LAST_SAVED_PATH, ""});
    addOption({OPT_SHOW_HIDDEN, (int) 0});
    addOption({OPT_STAY_AWAKE, (int) 0});
#ifdef __PS5__
    addOption({OPT_PS5_SLEEP_TIMER, (int) 0});
#endif
#ifdef __PS5__
    addOption({OPT_PS5_UI_4K, (int) 0});
#endif
    addOption({OPT_LAST_PLAYLIST, ""});
    addOption({OPT_LAST_PLAYLISTS_PATH, ""});

    // load the configuration from file, overwriting default values
    if (load() && getConfigVersion() < getAppVersion()) {
        migrate(getConfigVersion());
    }
    sanitize();

    // save configuration, in case new options needs to be added
    save();
}

std::string PPLAYConfig::defaultHomePath() const {
#ifdef __LINUX__
    // the user home, not the settings directory
    const char *home = getenv("HOME");
    if (home && home[0] == '/') {
        return std::string(home) + "/";
    }
    return io->getDataPath();
#else
#if defined(__SWITCH__)
    std::string path = "/media/"; // sdmc
#elif defined(__ANDROID__)
    // the shared storage (all files access asked at the start)
    std::string path = "/storage/emulated/0/";
#else
    std::string path = "/data/media/";
#endif
    if (!io->exist(path)) {
        io->create(path);
    }
    return io->exist(path) ? path : io->getDataPath();
#endif
}

void PPLAYConfig::migrate(int from) {
    printf("PPLAYConfig::migrate: %i -> %i\n", from, getAppVersion());
#ifndef __LINUX__
    if (from < 2) {
        std::string home = defaultHomePath();
        getOption(OPT_HOME_PATH)->setString(home);
        getOption(OPT_LAST_LOCAL_PATH)->setString(home);
    }
#endif
    getOption("VERSION")->setInteger(getAppVersion());
}

bool PPLAYConfig::reload() {
    bool ok = load();
    sanitize();
    return ok;
}

void PPLAYConfig::sanitize() {

    // a missing home is kept (an unplugged drive), only an empty one is reset
    if (getOption(OPT_HOME_PATH)->getString().empty()) {
        getOption(OPT_HOME_PATH)->setString(defaultHomePath());
    }

    if (!io->exist(getOption(OPT_LAST_LOCAL_PATH)->getString())) {
        getOption(OPT_LAST_LOCAL_PATH)->setString(getOption(OPT_HOME_PATH)->getString());
    }
    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        if (getOption(networkLastOption(i))->getString().empty()) {
            getOption(networkLastOption(i))->setString("/");
        }
        if (getOption(networkNameOption(i))->getString().empty()) {
            getOption(networkNameOption(i))->setString("Network " + std::to_string(i));
        }
        if (getOption(networkTypeOption(i))->getString().empty()) {
            getOption(networkTypeOption(i))->setString("Auto");
        }
        std::string url = getOption(networkOption(i))->getString();
        if (migrateTorrServeUrl(url)) {
            getOption(networkOption(i))->setString(url);
            getOption(networkTypeOption(i))->setString("TorrServe");
            std::string last = getOption(networkLastOption(i))->getString();
            if (migrateTorrServeUrl(last)) {
                getOption(networkLastOption(i))->setString(last);
            }
        }
    }
    if (getOption(OPT_LAST_MODULE)->getString().empty()) {
        getOption(OPT_LAST_MODULE)->setString("LOCAL");
    }

#ifdef __SMB2__
    if (getOption(OPT_SMB_READ_BUFFER_MB)->getInteger() < 1) {
        getOption(OPT_SMB_READ_BUFFER_MB)->setString("1");
    } else if (getOption(OPT_SMB_READ_BUFFER_MB)->getInteger() > 100) {
        getOption(OPT_SMB_READ_BUFFER_MB)->setString("100");
    }
#endif

    auto clamp = [this](const char *name, int min, int max) {
        int value = getOption(name)->getInteger();
        if (value < min || value > max) {
            getOption(name)->setString(std::to_string(value < min ? min : max));
        }
    };
    clamp(OPT_CACHE_SECS, 10, 600);
    clamp(OPT_CACHE_MB, 50, CACHE_MB_MAX);
    clamp(OPT_VIDEO_QUALITY, 0, 3);
    clamp(OPT_AUTO_DROPS, 1, 50);
    clamp(OPT_AUTO_DROPS_TIME, 5, 60);
    clamp(OPT_AUTO_QUALITY_DELAY, 0, 120);
    clamp(OPT_AUTO_HW_RETRY, 0, 30);
    clamp(OPT_AUTO_HW_RETRIES, 1, 10);
    clamp(OPT_TS_VIEWED_SYNC, 10, 600);
#ifdef PPLAY_HW_DECODING
    clamp(OPT_HW_DECODING, 0, 2);
#endif
    clamp(OPT_PROGRESS_MIN_LENGTH, 0, 60);
    clamp(OPT_PROGRESS_INTERVAL, 5, 120);
    clamp(OPT_SAVE_THREADS, 1, 4);
    clamp(OPT_STAY_AWAKE, 0, 1440);
    clamp(OPT_SAVE_KEEP_DAYS, 0, 3650);
    clamp(OPT_SAVE_MAX_GB, 0, 100000);

    if (!pplay::Utility::isValidHexColor(getOption(OPT_ACCENT_COLOR)->getString())) {
        getOption(OPT_ACCENT_COLOR)->setString("#1078C8");
    }
    pplay::Utility::setAccentColor(getOption(OPT_ACCENT_COLOR)->getString());

    if (getOption(OPT_LANGUAGE)->getString().empty()) {
        getOption(OPT_LANGUAGE)->setString("en");
    }
}
