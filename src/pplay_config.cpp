//
// Created by cpasjuste on 22/10/18.
//

#include "main.h"
#include "pplay_config.h"

using namespace c2d;

const char *PPLAYConfig::networkOption(int index) {
    static const char *options[] = {
            OPT_NETWORK1, OPT_NETWORK2, OPT_NETWORK3, OPT_NETWORK4, OPT_NETWORK5
    };
    if (index < 1 || index > 5) return OPT_NETWORK1;
    return options[index - 1];
}

const char *PPLAYConfig::networkLastOption(int index) {
    static const char *options[] = {
            OPT_NETWORK1_LAST, OPT_NETWORK2_LAST, OPT_NETWORK3_LAST, OPT_NETWORK4_LAST, OPT_NETWORK5_LAST
    };
    if (index < 1 || index > 5) return OPT_NETWORK1_LAST;
    return options[index - 1];
}

const char *PPLAYConfig::networkNameOption(int index) {
    static const char *options[] = {
            OPT_NETWORK1_NAME, OPT_NETWORK2_NAME, OPT_NETWORK3_NAME, OPT_NETWORK4_NAME, OPT_NETWORK5_NAME
    };
    if (index < 1 || index > 5) return OPT_NETWORK1_NAME;
    return options[index - 1];
}

PPLAYConfig::PPLAYConfig(Main *main, int version)
        : Config("PPLAY", main->getIo()->getDataPath() + "pplay.cfg", version) {

    for (int i = 1; i <= 5; i++) {
        addOption({networkOption(i), ""});
    }
#ifdef __SWITCH__
    addOption({OPT_UMS_DEVICE, "ums0:/"});
#endif
    addOption({OPT_HOME_PATH, main->getIo()->getDataPath()});
    addOption({OPT_LAST_LOCAL_PATH, main->getIo()->getDataPath()});
    for (int i = 1; i <= 5; i++) {
        addOption({networkLastOption(i), "/"});
        addOption({networkNameOption(i), "Network " + std::to_string(i)});
    }
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
    for (int i = 1; i <= 5; i++) {
        if (getOption(networkLastOption(i))->getString().empty()) {
            getOption(networkLastOption(i))->setString("/");
        }
        if (getOption(networkNameOption(i))->getString().empty()) {
            getOption(networkNameOption(i))->setString("Network " + std::to_string(i));
        }
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
