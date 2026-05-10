//
// Created by cpasjuste on 03/10/18.
//

#include <sstream>
#include <iomanip>
#include <codecvt>
#include <locale>
#include <fstream>
#include <ctime>

#include "cross2d/c2d.h"

#ifdef __SWITCH__
#if __has_include("cross2d/platforms/switch/switch_sys.h")
#include "cross2d/platforms/switch/switch_sys.h"
#elif __has_include("platforms/switch/switch_sys.h")
#include "platforms/switch/switch_sys.h"
#endif
#endif

#include "utility.h"
#include "io.h"
#include <string>
#include <vector>
#include <mbedtls/md5.h>
#include <cstdio>

using namespace pplay;
static Utility::LogLevel g_logLevel = Utility::LogLevel::Info;

std::string Utility::getMediaInfoPath(const c2d::Io::File &file) {
    std::string hash = std::to_string(std::hash<std::string>()(file.path));
    return c2d_renderer->getIo()->getDataPath() + "cache/" + hash + ".info";
}

std::string Utility::getMediaScrapPath(const c2d::Io::File &file) {
    std::string hash = std::to_string(std::hash<std::string>()(file.path));
    return c2d_renderer->getIo()->getDataPath() + "cache/" + hash + ".scrap";
}

std::string Utility::getMediaPosterPath(const c2d::Io::File &file) {
    std::string hash = std::to_string(std::hash<std::string>()(file.path));
    return c2d_renderer->getIo()->getDataPath() + "cache/" + hash + "-poster.jpg";
}

std::string Utility::getMediaBackdropPath(const c2d::Io::File &file) {
    std::string hash = std::to_string(std::hash<std::string>()(file.path));
    return c2d_renderer->getIo()->getDataPath() + "cache/" + hash + "-backdrop.jpg";
}

std::vector<std::string> Utility::getMediaExtensions() {
    return {
            ".8svx",
            ".aac",
            ".ac3",
            ".aif",
            ".asf",
            ".avi",
            ".dv",
            ".flv",
            ".m2ts",
            ".m2v",
            ".m4a",
            ".mkv",
            ".mov",
            ".mp3",
            ".mp4",
            ".mpeg",
            ".mpg",
            ".mts",
            ".ogg",
            ".rmvb",
            ".swf",
            ".ts",
            ".vob",
            ".wav",
            ".wma",
            ".wmv",
            ".m3u",
            ".m3u8"
    };
}

bool Utility::isMedia(const c2d::Io::File &file) {

    if (file.type == c2d::Io::Type::File) {
        std::vector<std::string> extensions = getMediaExtensions();
        for (auto &ext: extensions) {
            if (c2d::Utility::endsWith(file.name, ext, false)) {
                return true;
            }
        }
    }

    return false;
}

std::string Utility::formatTime(double seconds) {

    if (seconds <= 0) {
        return "00:00:00";
    }

    int h((int) seconds / 3600);
    int min((int) seconds / 60 - h * 60);
    int sec((int) seconds - (h * 60 + min) * 60);

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << h << ":";
    oss << std::setfill('0') << std::setw(2) << min << ":";
    oss << std::setfill('0') << std::setw(2) << sec;

    return oss.str();
}

std::string Utility::formatTimeShort(double seconds) {

    int h((int) seconds / 3600);
    int min((int) seconds / 60 - h * 60);
    int sec((int) seconds - (h * 60 + min) * 60);

    std::ostringstream oss;
    if (h > 0) {
        oss << std::setfill('0') << std::setw(2) << h << ":";
    }
    if (min > 0) {
        oss << std::setfill('0') << std::setw(2) << min << ":";
    }
    oss << std::setfill('0') << std::setw(2) << sec;

    return oss.str();
}

static std::string convertToString(double num) {
    std::ostringstream convert;
    convert << num;
    return convert.str();
}

static double roundOff(double n) {
    double d = n * 100.0;
    int i = (int) lround(d);//(int) (d + 0.5);
    d = (float) i / 100.0;
    return d;
}

std::string Utility::formatSize(size_t size) {

    static const char *sizes[] = {"B", "KB", "MB", "GB"};
    int div = 0;
    size_t rem = 0;

    while (size >= 1024 && (size_t) div < (sizeof sizes / sizeof *sizes)) {
        rem = (size_t) (size % 1024);
        div++;
        size /= 1024;
    }

    double size_d = (float) size + (float) rem / 1024.0;
    return convertToString(roundOff(size_d)) + " " + sizes[div];
}

void Utility::setLogLevel(Utility::LogLevel level) {
    g_logLevel = level;
}

void Utility::log(Utility::LogLevel level, const std::string &message) {
    if ((int) level > (int) g_logLevel || g_logLevel == Utility::LogLevel::Off) {
        return;
    }
    std::string path = c2d_renderer->getIo()->getDataPath() + "pplay.log";
    bool writeBom = !c2d_renderer->getIo()->exist(path);
    std::ofstream out(path, std::ios::app);
    if (!out.is_open()) {
        return;
    }
    if (writeBom) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        out.write((const char *) bom, 3);
    }
    std::time_t t = std::time(nullptr);
    out << std::to_string((long long) t) << " | " << message << "\n";
}

std::string Utility::md5hash(const std::string &input) {
    unsigned char output[16];
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);
    mbedtls_md5_update_ret(&ctx, (const unsigned char*)input.c_str(), input.length());
    mbedtls_md5_finish_ret(&ctx, output);
    mbedtls_md5_free(&ctx);

    std::stringstream ss;
    for(int i = 0; i < 16; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)output[i];
    }
    return ss.str();
}

bool Utility::deleteFile(const std::string &path) {
    if (std::remove(path.c_str()) == 0) {
        return true;
    } else {
        return false;
    }
}

bool Utility::fileExists(const std::string &path) {
    if (FILE* file = std::fopen(path.c_str(), "r")) {
        std::fclose(file);
        return true;
    }
    return false;
}

std::string Utility::getWatchLater(const std::string &video_path) {
    std::string hash = pplay::Utility::md5hash(video_path);
    std::transform(hash.begin(), hash.end(), hash.begin(), ::toupper);
    std::string path = c2d_renderer->getIo()->getDataPath() + "mpv/watch_later/" + hash;
    log(LogLevel::Debug, "Utility::getWatchLater video_path=" + video_path + " wl_path=" + path);
    return path;
}

bool Utility::isWatchLaterExist(const std::string &video_path) {
    std::string path = getWatchLater(video_path);
    return fileExists(path);
}

bool Utility::deleteWatchLater(const std::string &video_path) {
    std::string path = getWatchLater(video_path);
    if (fileExists(path)) {
        return deleteFile(path);
    }
}

std::string Utility::getCertificatesPath() {
    std::string customCA = c2d_renderer->getIo()->getDataPath() + "cacert.pem";
    std::string defaultCA = c2d_renderer->getIo()->getRomFsPath() + "cacert.pem";
    return fileExists(customCA) ? customCA : defaultCA;
}

std::string Utility::getKeysString(unsigned int keys) {
    if (keys == 0) return "";
    
    static const std::pair<unsigned int, const char*> btns[] = {
        {c2d::Input::Up, "Up"}, {c2d::Input::Down, "Down"}, {c2d::Input::Left, "Left"}, {c2d::Input::Right, "Right"},
        {c2d::Input::LB, "LB"}, {c2d::Input::RB, "RB"}, {c2d::Input::LT, "LT"}, {c2d::Input::RT, "RT"},
        {c2d::Input::A, "A"}, {c2d::Input::B, "B"}, {c2d::Input::X, "X"}, {c2d::Input::Y, "Y"},
        {c2d::Input::Start, "Start"}, {c2d::Input::Select, "Select"}
    };

    std::string res = "Keys(" + std::to_string(keys) + "): ";
    for (const auto& [bit, name] : btns) {
        if (keys & bit) res += std::string(name) + " ";
    }
    return res;
}

void Utility::setCpuClock(const CpuClock &clock) {
#ifdef __SWITCH__
    if (clock == CpuClock::Min) {
        if (c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu) !=
            c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu, true)) {
            int clock_old = c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu);
            c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Cpu, (int) c2d::SwitchSys::CPUClock::Stock);
            c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Gpu, (int) c2d::SwitchSys::GPUClock::Stock);
            c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Emc, (int) c2d::SwitchSys::EMCClock::Stock);
            printf("restoring cpu speed (old: %i, new: %i)\n",
                   clock_old, c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu));
        }
    } else {
        int clock_old = c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu);
        c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Cpu, (int) c2d::SwitchSys::CPUClock::Max);
        c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Gpu, (int) c2d::SwitchSys::GPUClock::Max);
        c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Emc, (int) c2d::SwitchSys::EMCClock::Max);
        printf("setting max cpu speed (old: %i, new: %i)\n",
               clock_old, c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu));
    }
#endif
}
