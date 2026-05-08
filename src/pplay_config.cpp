//
// Created by cpasjuste on 22/10/18.
//

#include "main.h"
#include "pplay_config.h"

using namespace c2d;

PPLAYConfig::PPLAYConfig(Main *main, int version)
        : Config("PPLAY", main->getIo()->getDataPath() + "pplay.cfg", version) {

    addOption({OPT_NETWORK, "http://samples.ffmpeg.org/"});
#ifdef __SWITCH__
    addOption({OPT_UMS_DEVICE, "ums0:/"});
#endif
    addOption({OPT_HOME_PATH, main->getIo()->getDataPath()});
    addOption({OPT_LAST_LOCAL_PATH, main->getIo()->getDataPath()});
    addOption({OPT_LAST_NETWORK_PATH, "/"});
    addOption({OPT_LAST_MODULE, "LOCAL"});
    addOption({OPT_NETWORK_TIMEOUT, (int) 15});
    addOption({OPT_NETWORK_RETRIES, (int) 3});
    addOption({OPT_AUTOPLAY_NEXT, (int) 1});
    addOption({OPT_AUTOPLAY_LOOP, (int) 0});
    addOption({OPT_ENABLE_SCRAPPING, (int) 0});
    addOption({OPT_LOG_LEVEL, (int) 0}); // 0 OFF, 1 ERROR, 2 INFO, 3 DEBUG
    addOption({OPT_CACHE_MEDIA_INFO, (int) 1});
    addOption({OPT_SWAP_CONTROLS, (int) 0});
    addOption({OPT_UTC_OFFSET, (float) 0.0});
    addOption({OPT_SEEK_SHORT, (float) 0.5});
    addOption({OPT_SEEK_LONG, (float) 5.0});
    //addOption({OPT_BUFFER, "Low"}); // Low, Medium, High, VeryHigh
#ifdef __SWITCH__
    addOption({OPT_CPU_BOOST, "Disabled"}); // Disabled, Enabled
#endif
    addOption({OPT_TMDB_LANGUAGE, "en-US"});

    // load the configuration from file, overwriting default values
    load();

    if (!main->getIo()->exist(getOption(OPT_HOME_PATH)->getString())) {
        getOption(OPT_HOME_PATH)->setString(main->getIo()->getDataPath());
    }

    if (!main->getIo()->exist(getOption(OPT_LAST_LOCAL_PATH)->getString())) {
        getOption(OPT_LAST_LOCAL_PATH)->setString(main->getIo()->getDataPath());
    }
    if (getOption(OPT_LAST_NETWORK_PATH)->getString().empty()) {
        getOption(OPT_LAST_NETWORK_PATH)->setString("/");
    }
    if (getOption(OPT_LAST_MODULE)->getString().empty()) {
        getOption(OPT_LAST_MODULE)->setString("LOCAL");
    }

    if (getOption(OPT_TMDB_LANGUAGE)->getString().empty()) {
        getOption(OPT_TMDB_LANGUAGE)->setString("en-US");
    }

    // save configuration, in case new options needs to be added
    save();
}
