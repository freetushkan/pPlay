//
// Created by cpasjuste on 22/10/18.
//

#ifndef PPLAY_CONFIG_H
#define PPLAY_CONFIG_H

#include "cross2d/skeleton/config.h"

#define OPT_NETWORK             "NETWORK1"
#define OPT_NETWORK1            "NETWORK1"
#define OPT_NETWORK2            "NETWORK2"
#define OPT_NETWORK3            "NETWORK3"
#define OPT_NETWORK4            "NETWORK4"
#define OPT_NETWORK5            "NETWORK5"
#define OPT_UMS_DEVICE          "UMS_DEVICE"
#define OPT_HOME_PATH           "HOME_PATH"
#define OPT_LAST_LOCAL_PATH     "LAST_LOCAL_PATH"
#define OPT_LAST_NETWORK_PATH   "NETWORK1_LAST"
#define OPT_NETWORK1_LAST       "NETWORK1_LAST"
#define OPT_NETWORK2_LAST       "NETWORK2_LAST"
#define OPT_NETWORK3_LAST       "NETWORK3_LAST"
#define OPT_NETWORK4_LAST       "NETWORK4_LAST"
#define OPT_NETWORK5_LAST       "NETWORK5_LAST"
#define OPT_NETWORK1_NAME       "NETWORK1_NAME"
#define OPT_NETWORK2_NAME       "NETWORK2_NAME"
#define OPT_NETWORK3_NAME       "NETWORK3_NAME"
#define OPT_NETWORK4_NAME       "NETWORK4_NAME"
#define OPT_NETWORK5_NAME       "NETWORK5_NAME"
#define OPT_LAST_MODULE         "LAST_MODULE"
#define OPT_NETWORK_TIMEOUT     "NETWORK_TIMEOUT"
#define OPT_NETWORK_RETRIES     "NETWORK_RETRIES"
#define OPT_SMB_READ_BUFFER_KIB "SMB_READ_BUFFER_KIB"
#define OPT_AUTOPLAY_NEXT       "AUTOPLAY_NEXT"
#define OPT_AUTOPLAY_LOOP       "AUTOPLAY_LOOP"
#define OPT_ENABLE_SCRAPPING    "ENABLE_SCRAPPING"
#define OPT_LOG_LEVEL           "LOG_LEVEL"
#define OPT_CACHE_MEDIA_INFO    "CACHE_MEDIA_INFO"
#define OPT_SWAP_CONTROLS       "SWAP_CONTROLS"
#define OPT_UTC_OFFSET          "UTC_OFFSET"
#define OPT_SEEK_SHORT          "SEEK_SHORT"
#define OPT_SEEK_LONG           "SEEK_LONG"
#define OPT_CPU_BOOST           "CPU_BOOST"
#define OPT_TMDB_LANGUAGE       "TMDB_LANGUAGE"

class Main;

class PPLAYConfig : public c2d::config::Config {

public:

    explicit PPLAYConfig(Main *main, int version = 1);

    static const char *networkOption(int index);
    static const char *networkLastOption(int index);
    static const char *networkNameOption(int index);
};

#endif //PPLAY_CONFIG_H
