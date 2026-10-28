//
// Created by cpasjuste on 22/10/18.
//

#include <cstring>
#include "io.h"
#include "pplay_config.h"
#include "utility.h"

using namespace c2d;

static const char *networkOptionName(int index, const char *suffix) {
    static std::vector<std::string> names[4];
    static const char *suffixes[4] = {"", "_LAST", "_NAME", "_TYPE"};
    int s = 0;
    while (s < 3 && strcmp(suffixes[s], suffix) != 0) s++;
    if (names[s].empty()) {
        for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
            names[s].push_back("NETWORK" + std::to_string(i) + suffixes[s]);
        }
    }
    if (index < 1 || index > PPLAY_MAX_NETWORKS) index = 1;
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
    addOption({OPT_HOME_PATH, io->getDataPath()});
    addOption({OPT_LAST_LOCAL_PATH, io->getDataPath()});
    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        addOption({networkLastOption(i), "/"});
        addOption({networkNameOption(i), "Network " + std::to_string(i)});
        addOption({networkTypeOption(i), "Auto"}); // Auto, TorrServe, Samba, FTP, HTTP, WebDAV
    }
    addOption({OPT_LAST_MODULE, "LOCAL"});
    addOption({OPT_NETWORK_TIMEOUT, (int) 15});
    addOption({OPT_NETWORK_RETRIES, (int) 3});
#ifdef __SMB2__
    addOption({OPT_SMB_READ_BUFFER_MB, (int) 10});
#endif
    addOption({OPT_AUTOPLAY_MODE, (int) 1});
#ifdef PPLAY_ENABLE_SCRAPPING
    addOption({OPT_ENABLE_SCRAPPING, (int) 0});
    addOption({OPT_TMDB_LANGUAGE, "en-US"});
    addOption({OPT_CACHE_MEDIA_INFO, (int) 1});
#endif
    addOption({OPT_LOG_LEVEL, (int) 0}); // 0 OFF, 1 ERROR, 2 INFO, 3 DEBUG, 4 TRACE
    addOption({OPT_SWAP_CONTROLS, (int) 0});
    addOption({OPT_SEEK_SHORT_SEC, (float) 30.0});
    addOption({OPT_SEEK_LONG_SEC, (float) 300.0});
#ifdef __SWITCH__
    addOption({OPT_CPU_BOOST, "Disabled"}); // Disabled, Enabled
#endif
    addOption({OPT_ACCENT_COLOR, "#1078C8"});
    addOption({OPT_PREVIEW_PANEL, (int) 1});
    addOption({OPT_LANGUAGE, "en"});
    addOption({OPT_AUDIO_LANGUAGE, "Auto"});
    addOption({OPT_SUB_LANGUAGE, "Off"});
    addOption({OPT_EXTERNAL_TRACKS, (int) 1});

    // load the configuration from file, overwriting default values
    load();
    sanitize();

    // save configuration, in case new options needs to be added
    save();
}

bool PPLAYConfig::reload() {
    bool ok = load();
    sanitize();
    return ok;
}

void PPLAYConfig::sanitize() {

    if (!io->exist(getOption(OPT_HOME_PATH)->getString())) {
        getOption(OPT_HOME_PATH)->setString(io->getDataPath());
    }

    if (!io->exist(getOption(OPT_LAST_LOCAL_PATH)->getString())) {
        getOption(OPT_LAST_LOCAL_PATH)->setString(io->getDataPath());
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

#ifdef PPLAY_ENABLE_SCRAPPING
    if (getOption(OPT_TMDB_LANGUAGE)->getString().empty()) {
        getOption(OPT_TMDB_LANGUAGE)->setString("en-US");
    }
#endif

    if (!pplay::Utility::isValidHexColor(getOption(OPT_ACCENT_COLOR)->getString())) {
        getOption(OPT_ACCENT_COLOR)->setString("#1078C8");
    }
    pplay::Utility::setAccentColor(getOption(OPT_ACCENT_COLOR)->getString());

    if (getOption(OPT_LANGUAGE)->getString().empty()) {
        getOption(OPT_LANGUAGE)->setString("en");
    }
}
