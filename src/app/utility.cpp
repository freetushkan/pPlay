//
// Created by cpasjuste on 03/10/18.
//

#include <dirent.h>
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
#include "poster_cache.h"
#include <string>
#include <vector>
#ifdef __PS5__
#include <openssl/evp.h>
#else
#include <mbedtls/md5.h>
#endif
#include <cstdio>

#include <algorithm>
#include <cstring>
#include <cstdint>

#include <chrono>
#include <mutex>

using namespace pplay;
static Utility::LogLevel g_logLevel = Utility::LogLevel::Info;
static c2d::Color g_accentColor = c2d::Color(16, 120, 200, 255);
static std::string g_dataPath;

void Utility::setDataPath(const std::string &path) {
    g_dataPath = path;
    if (!g_dataPath.empty() && g_dataPath.back() != '/') {
        g_dataPath += "/";
    }
}

const std::string &Utility::getDataPath() {
    return g_dataPath;
}

std::string Utility::getMediaInfoPath(const c2d::Io::File &file) {
    std::string hash = std::to_string(std::hash<std::string>()(file.path));
    return getDataPath() + "cache/" + hash + ".info";
}

std::vector<std::string> Utility::getMediaExtensions() {
    return {".8svx", ".aac", ".ac3",  ".aif",  ".aiff", ".ape", ".asf", ".avi",  ".dsf",  ".dts", ".dv",
            ".eac3", ".flac", ".flv", ".m2ts", ".m2v", ".m4a", ".m4b", ".m4v", ".mka", ".mkv",  ".mov",
            ".mp3",  ".mp4", ".mpc", ".mpeg", ".mpg",  ".mts", ".oga", ".ogg",  ".ogv", ".opus", ".rmvb",
            ".swf",  ".ts",  ".tta", ".vob",  ".wav",  ".webm", ".wma", ".wmv",  ".wv",  ".3gp",  ".m3u", ".m3u8"};
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

int Utility::hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool Utility::isValidHexColor(const std::string &hex) {
    const size_t offset = !hex.empty() && hex[0] == '#' ? 1 : 0;
    if (hex.size() - offset != 6) {
        return false;
    }
    for (size_t i = offset; i < hex.size(); i++) {
        if (hexValue(hex[i]) < 0) {
            return false;
        }
    }
    return true;
}

c2d::Color Utility::hexToColor(const std::string &hex) {
    if (!isValidHexColor(hex)) {
        return c2d::Color(16, 120, 200, 255);
    }
    const size_t offset = hex[0] == '#' ? 1 : 0;
    auto byteAt = [&](size_t i) -> uint8_t {
        return (uint8_t) ((hexValue(hex[offset + i]) << 4) | hexValue(hex[offset + i + 1]));
    };
    return c2d::Color(byteAt(0), byteAt(2), byteAt(4), 255);
}

void Utility::setAccentColor(const std::string &hex) {
    g_accentColor = hexToColor(hex);
}

c2d::Color &Utility::getAccentColor() {
    return g_accentColor;
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
    int i = (int) lround(d); //(int) (d + 0.5);
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

Utility::LogLevel Utility::getLogLevel() {
    return g_logLevel;
}

#ifdef __SWITCH__
static std::recursive_mutex &networkMutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

Utility::NetworkLock::NetworkLock() {
    networkMutex().lock();
}

Utility::NetworkLock::~NetworkLock() {
    networkMutex().unlock();
}
#else
Utility::NetworkLock::NetworkLock() = default;

Utility::NetworkLock::~NetworkLock() = default;
#endif

void Utility::log(Utility::LogLevel level, const std::string &message) {
    if ((int) level > (int) g_logLevel || g_logLevel == Utility::LogLevel::Off) {
        return;
    }
#ifdef __PS4__
    sceKernelDebugOutText(0, ("[pPlay] " + message + "\n").c_str());
#endif
    if (g_dataPath.empty()) {
        return;
    }
    std::string path = g_dataPath + "pplay.log";
    bool writeBom = !fileExists(path);
    std::ofstream out(path, std::ios::app);
    if (!out.is_open()) {
        return;
    }
    if (writeBom) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        out.write((const char *) bom, 3);
    }
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    long long raw_ms = now_ms.time_since_epoch().count();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_info;
    gmtime_r(&t, &tm_info);
    int ms = raw_ms % 1000;
    std::ostringstream time_ss;
    time_ss << std::put_time(&tm_info, "%Y.%m.%d %H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms;
    out << time_ss.str() << " UTC (" << raw_ms << ") | " << message << "\n";
}

std::string Utility::md5hash(const std::string &input) {
    unsigned char output[16];

#ifdef __PS5__
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    if (context != nullptr) {
        if (EVP_DigestInit_ex(context, EVP_md5(), nullptr) == 1) {
            EVP_DigestUpdate(context, input.c_str(), input.length());
            unsigned int length = 0;
            EVP_DigestFinal_ex(context, output, &length);
        }
        EVP_MD_CTX_free(context);
    }
#else
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);
    mbedtls_md5_update_ret(&ctx, (const unsigned char *) input.c_str(), input.length());
    mbedtls_md5_finish_ret(&ctx, output);
    mbedtls_md5_free(&ctx);
#endif

    std::stringstream ss;
    for (int i = 0; i < 16; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int) output[i];
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
    if (FILE *file = std::fopen(path.c_str(), "r")) {
        std::fclose(file);
        return true;
    }
    return false;
}

// mpv 0.39 names the urls watch later files by their base name too, with
// ignore-path-in-watch-later-config (its url check is made on an unset string)
#ifdef __PS5__
static bool watchLaterUrlBaseName = true;
#else
static bool watchLaterUrlBaseName = false;
#endif

void Utility::setMpvVersion(const std::string &version) {
    // "mpv 0.39.0", "mpv v0.39.0-..."
    int major = -1, minor = -1;
    size_t digit = version.find_first_of("0123456789");
    if (digit != std::string::npos) {
        sscanf(version.c_str() + digit, "%d.%d", &major, &minor);
    }
    watchLaterUrlBaseName = major == 0 && minor == 39;
    log(LogLevel::Info, "Utility: watch later url base name: " + std::to_string(watchLaterUrlBaseName ? 1 : 0));
}

#if defined(__PS4__) || defined(__PS5__)
// mpv mp_is_url: "proto://", proto made of letters, digits, '+', '-', '.'
static bool isMpvUrl(const std::string &path) {
    size_t sep = path.find("://");
    if (sep == std::string::npos || sep == 0) {
        return false;
    }
    for (size_t i = 0; i < sep; i++) {
        unsigned char c = path[i];
        if (!isalnum(c) && c != '+' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}
#endif

std::string Utility::getWatchLater(const std::string &video_path) {
    std::string mpvPath = video_path;
#ifdef __SMB2__
    if (c2d::Utility::startWith(mpvPath, "smb://")) {
        std::replace(mpvPath.begin(), mpvPath.end(), '\\', '/');
        mpvPath.replace(0, strlen("smb://"), "smb2://");
    }
#endif
#if defined(__PS4__) || defined(__PS5__)
    // ignore-path-in-watch-later-config: mpv hashes the part after the last slash
    // (urls: full, but mpv 0.39)
    const bool basename = !isMpvUrl(mpvPath) || watchLaterUrlBaseName;
#elif defined(__LINUX__)
    // the whole path
    const bool basename = false;
#else
    const bool basename = !mpvPath.empty() && mpvPath[0] == '/';
#endif
    size_t last_slash = mpvPath.find_last_of('/');
    if (basename && last_slash != std::string::npos) {
        mpvPath = mpvPath.substr(last_slash + 1);
    }
    std::string hash = pplay::Utility::md5hash(mpvPath);
    std::transform(hash.begin(), hash.end(), hash.begin(), ::toupper);
    std::string path = getDataPath() + "mpv/watch_later/" + hash;
    log(LogLevel::Debug,
        "Utility::getWatchLater video_path=" + video_path + " mpv_path=" + mpvPath + " wl_path=" + path);
    return path;
}

bool Utility::isWatchLaterExist(const std::string &video_path) {
    std::string path = getWatchLater(video_path);
    return fileExists(path);
}

void Utility::fixWatchLater(const std::string &video_path) {
    // "aid=no": saved while the visualizer had the track, the file then fails to load
    std::string path = getWatchLater(video_path);
    std::ifstream in(path);
    if (!in.is_open()) {
        return;
    }
    std::string text, line;
    bool changed = false;
    while (std::getline(in, line)) {
        if (line == "aid=no" || line.rfind("lavfi-complex=", 0) == 0) {
            changed = true;
            continue;
        }
        text += line + "\n";
    }
    in.close();
    if (changed) {
        std::ofstream out(path, std::ios::trunc);
        out << text;
        log(LogLevel::Info, "Utility::fixWatchLater: " + path);
    }
}

double Utility::watchLaterPosition(const std::string &video_path) {
    FILE *f = fopen(getWatchLater(video_path).c_str(), "rb");
    if (f == nullptr) {
        return 0;
    }
    char line[256];
    double position = 0;
    while (fgets(line, sizeof(line), f) != nullptr) {
        if (std::strncmp(line, "start=", 6) == 0) {
            position = std::atof(line + 6);
            break;
        }
    }
    fclose(f);
    return position;
}

bool Utility::deleteWatchLater(const std::string &video_path) {
    std::string path = getWatchLater(video_path);
    if (fileExists(path)) {
        return deleteFile(path);
    }
    return false;
}

#if defined(__PS4__) || defined(__PS5__)
std::string Utility::getCertificatesPath() {
    return getDataPath() + "cacert.pem";
}
#elif defined(__LINUX__)
std::string Utility::getCertificatesPath() {
    static const char *paths[] = {
            "/etc/ssl/certs/ca-certificates.crt",
            "/etc/pki/tls/certs/ca-bundle.crt",
            "/etc/ssl/ca-bundle.pem",
            "/etc/pki/tls/cacert.pem",
            "/etc/ssl/cert.pem",
    };
    for (const char *path: paths) {
        if (fileExists(path)) {
            return path;
        }
    }
    return "";
}
#endif

std::string Utility::getKeysString(unsigned int keys) {
    if (keys == 0) {
        return "";
    }

    static const std::pair<unsigned int, const char *> btns[] = {
            {c2d::Input::Up, "Up"},       {c2d::Input::Down, "Down"},    {c2d::Input::Left, "Left"},
            {c2d::Input::Right, "Right"}, {c2d::Input::LB, "LB"},        {c2d::Input::RB, "RB"},
            {c2d::Input::LT, "LT"},       {c2d::Input::RT, "RT"},        {c2d::Input::A, "A"},
            {c2d::Input::B, "B"},         {c2d::Input::X, "X"},          {c2d::Input::Y, "Y"},
            {c2d::Input::Start, "Start"}, {c2d::Input::Select, "Select"}};

    std::string res = "Keys(" + std::to_string(keys) + "): ";
    for (const auto &[bit, name]: btns) {
        if (keys & bit) {
            res += std::string(name) + " ";
        }
    }
    return res;
}

void Utility::setCpuClock(const CpuClock &clock) {
#ifdef __SWITCH__
    if (clock == CpuClock::Min) {
        if (c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu)
            != c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu, true)) {
            int clock_old = c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu);
            c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Cpu, (int) c2d::SwitchSys::CPUClock::Stock);
            c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Gpu, (int) c2d::SwitchSys::GPUClock::Stock);
            c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Emc, (int) c2d::SwitchSys::EMCClock::Stock);
            printf("restoring cpu speed (old: %i, new: %i)\n", clock_old,
                   c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu));
        }
    } else {
        int clock_old = c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu);
        c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Cpu, (int) c2d::SwitchSys::CPUClock::Max);
        c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Gpu, (int) c2d::SwitchSys::GPUClock::Max);
        c2d::SwitchSys::setClock(c2d::SwitchSys::Module::Emc, (int) c2d::SwitchSys::EMCClock::Max);
        printf("setting max cpu speed (old: %i, new: %i)\n", clock_old,
               c2d::SwitchSys::getClock(c2d::SwitchSys::Module::Cpu));
    }
#endif
}

int Utility::clearCache() {
    int count = pplay::PosterCache::clear();
    // the media information files only
    std::string dir = getDataPath() + "cache/";
    DIR *d = opendir(dir.c_str());
    if (d != nullptr) {
        std::vector<std::string> names;
        while (struct dirent *entry = readdir(d)) {
            std::string name = entry->d_name;
            if (c2d::Utility::endsWith(name, ".info")) {
                names.push_back(name);
            }
        }
        closedir(d);
        for (auto &name: names) {
            count += deleteFile(dir + name) ? 1 : 0;
        }
    }
    log(LogLevel::Info, "Utility: cache cleared, " + std::to_string(count) + " files");
    return count;
}
