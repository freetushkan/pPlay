//
// Created by cpasjuste on 22/10/18.
//

#ifndef PPLAY_CONFIG_H
#define PPLAY_CONFIG_H

#include "cross2d/skeleton/config.h"

#define OPT_NETWORK             "NETWORK"
#define OPT_UMS_DEVICE          "UMS_DEVICE"
#define OPT_HOME_PATH           "HOME_PATH"
#define OPT_LAST_LOCAL_PATH     "LAST_LOCAL_PATH"
#define OPT_LAST_NETWORK_PATH   "LAST_NETWORK_PATH"
#define OPT_LAST_MODULE         "LAST_MODULE"
#define OPT_NETWORK_TIMEOUT     "NETWORK_TIMEOUT"
#define OPT_NETWORK_RETRIES     "NETWORK_RETRIES"
#define OPT_AUTOPLAY_NEXT       "AUTOPLAY_NEXT"
#define OPT_AUTOPLAY_LOOP       "AUTOPLAY_LOOP"
#define OPT_ENABLE_SCRAPPING    "ENABLE_SCRAPPING"
#define OPT_LOG_LEVEL           "LOG_LEVEL"
#define OPT_CACHE_MEDIA_INFO    "CACHE_MEDIA_INFO"
#define OPT_SWAP_CONTROLS       "SWAP_CONTROLS"
#define OPT_UTC_OFFSET          "UTC_OFFSET"
#define OPT_SEEK_SHORT          "SEEK_SHORT"
#define OPT_SEEK_LONG           "SEEK_LONG"
//#define OPT_BUFFER              "BUFFER"
#define OPT_CPU_BOOST           "CPU_BOOST"
#define OPT_TMDB_LANGUAGE       "TMDB_LANGUAGE"

class Main;

class PPLAYConfig : public c2d::config::Config {

public:

    explicit PPLAYConfig(Main *main, int version = 1);
};

#endif //PPLAY_CONFIG_H
