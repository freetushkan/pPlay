//
// Created by cpasjuste on 03/10/18.
//

#ifndef PPLAY_UTILITY_H
#define PPLAY_UTILITY_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"
#include "cross2d/skeleton/io.h"

namespace pplay {

    class Utility {

    public:
        enum class LogLevel {
            Off = 0,
            Error = 1,
            Info = 2,
            Debug = 3,
            Trace = 4
        };

        enum class CpuClock {
            Min = 0,
            Max = 1
        };

        static std::string getMediaInfoPath(const c2d::Io::File &file);

#ifdef PPLAY_ENABLE_SCRAPPING
        static std::string getMediaScrapPath(const c2d::Io::File &file);

        static std::string getMediaPosterPath(const c2d::Io::File &file);

        static std::string getMediaBackdropPath(const c2d::Io::File &file);
#endif

        static std::vector<std::string> getMediaExtensions();

        static bool isMedia(const c2d::Io::File &file);

        static std::string formatTime(double seconds);

        static std::string formatTimeShort(double seconds);

        static std::string formatSize(size_t size);

        static void setLogLevel(LogLevel level);
        
        static void log(LogLevel level, const std::string &message);
        
        static std::string md5hash(const std::string &input);

        static int hexValue(char c);

        static bool isValidHexColor(const std::string &hex);

        static c2d::Color hexToColor(const std::string &hex);

        static void setAccentColor(const std::string &hex);

        static c2d::Color& getAccentColor();

        static bool deleteFile(const std::string &path);

        static bool fileExists(const std::string &path);

        static std::string getWatchLater(const std::string &video_path);

        static bool isWatchLaterExist(const std::string &video_path);

        static bool deleteWatchLater(const std::string &video_path);

#ifdef __PPLAY_PLAYSTATION__
        static std::string getCertificatesPath();
#endif

        static std::string getKeysString(unsigned int keys);

        static void setCpuClock(const CpuClock &clock);
    };
}

#endif //PPLAY_UTILITY_H
