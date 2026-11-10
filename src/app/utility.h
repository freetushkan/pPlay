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

        // data directory (trailing slash), set first
        static void setDataPath(const std::string &path);

        static const std::string &getDataPath();

        static std::string getMediaInfoPath(const c2d::Io::File &file);

        static std::vector<std::string> getMediaExtensions();

        static bool isMedia(const c2d::Io::File &file);

        static std::string formatTime(double seconds);

        static std::string formatSize(uint64_t size);

        static LogLevel getLogLevel();

        static void setLogLevel(LogLevel level);

        static void log(LogLevel level, const std::string &message);

        // the first line, whatever the level (again after clearLog())
        static void setLogHeader(const std::string &header);

        static std::string getLogPath();

        static bool clearLog();

        // background network work one at a time where the system does not like it in
        // parallel (Switch: resolver, TLS); nothing elsewhere
        class NetworkLock {
        public:
            NetworkLock();

            ~NetworkLock();

            NetworkLock(const NetworkLock &) = delete;

            NetworkLock &operator=(const NetworkLock &) = delete;
        };

        static std::string md5hash(const std::string &input);

        static int hexValue(char c);

        static bool isValidHexColor(const std::string &hex);

        static c2d::Color hexToColor(const std::string &hex);

        static void setAccentColor(const std::string &hex);

        static c2d::Color &getAccentColor();

        static bool deleteFile(const std::string &path);

        // media information and posters; the files removed
        static int clearCache();

        static bool fileExists(const std::string &path);

        static std::string getWatchLater(const std::string &video_path);

        static void setMpvVersion(const std::string &version);

        static bool isWatchLaterExist(const std::string &video_path);

        static void fixWatchLater(const std::string &video_path);

        // the position kept by mpv (seconds), 0 if none
        static double watchLaterPosition(const std::string &video_path);

        static bool deleteWatchLater(const std::string &video_path);

        // "mark watched": the full duration kept; such a file starts over
        static bool setWatchLaterWatched(const std::string &video_path, double duration);

        static bool isWatchLaterWatched(const std::string &video_path);

        // the cached reads of a watch later file dropped (all: an empty path), after mpv wrote
        static void forgetWatchLater(const std::string &video_path = "");

#if defined(__PS4__) || defined(__PS5__) || defined(__LINUX__) || defined(__ANDROID__)
        static std::string getCertificatesPath();

        // certificates checked (all: mpv, curl, the media probes); the setting
        static bool isTlsVerify();

        static void setTlsVerify(bool verify);
#endif

        static std::string getKeysString(unsigned int keys);

        static void setCpuClock(const CpuClock &clock);
    };
}

#endif //PPLAY_UTILITY_H
