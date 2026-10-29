//
// Created by cpasjuste on 31/03/19.
//

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "io.h"
#include "utility.h"
#include "pplay_config.h"
#include "net_http.h"
#include "torrserve.h"
#include "ftp.h"
#ifdef __SWITCH__
#include "usbfs.h"
#endif

using namespace pplay;

#ifdef __LINUX__
#include <climits>
#include <sys/stat.h>
#include <unistd.h>

std::string Io::getDataPath() {
    static std::string path;
    if (path.empty()) {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        const char *home = getenv("HOME");
        std::string base = xdg && xdg[0] == '/' ? xdg : std::string(home && home[0] ? home : ".") + "/.config";
        path = base + "/pplay/";
        // the parents too
        for (size_t i = 1; i < path.size(); i++) {
            if (path[i] == '/') {
                mkdir(path.substr(0, i).c_str(), 0755);
            }
        }
    }
    return path;
}

std::string Io::getRomFsPath() {
    static std::string path;
    if (path.empty()) {
        std::string dir = "./";
        char exe[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (len > 0) {
            exe[len] = '\0';
            std::string exePath = exe;
            dir = exePath.substr(0, exePath.find_last_of('/') + 1);
        }
        path = dir + "data_romfs/";
    }
    return path;
}
#endif

static Io *g_io = nullptr;

Io::Io() : c2d::C2DIo() {
    g_io = this;
    Http::init();
    Ftp::init();
}

Io *Io::get() {
    return g_io;
}

const std::vector<std::string> &Io::networkTypeNames() {
    static const std::vector<std::string> names = {
            "Auto", "TorrServe", "Samba", "FTP", "HTTP", "WebDAV"
    };
    return names;
}

Io::NetworkType Io::parseNetworkType(const std::string &name) {
    const std::string n = c2d::Utility::toLower(name);
    if (n == "torrserve" || n == "torrserver") return NetworkType::TorrServe;
    if (n == "samba" || n == "smb") return NetworkType::Smb;
    if (n == "ftp") return NetworkType::Ftp;
    if (n == "http" || n == "https") return NetworkType::Http;
    if (n == "webdav" || n == "dav") return NetworkType::WebDav;
    return NetworkType::Auto;
}

std::vector<c2d::Io::File> Io::getDirList(const pplay::Io::DeviceType &type, const std::vector<std::string> &extensions,
                                          const std::string &path, int timeout, bool sort, bool showHidden) {

    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Io::getDirList path=" + path);
    lastError.clear();

    std::vector<c2d::Io::File> files = list(type, path, timeout, &lastError, sort, showHidden);
    if (sort && type != DeviceType::Local) {
        std::sort(files.begin(), files.end(), compare);
    }

    // remove items by extensions, if provided
    if (!extensions.empty()) {
        files.erase(
                std::remove_if(files.begin(), files.end(), [extensions](const Io::File &file) {
                    for (auto &ext: extensions) {
                        if (c2d::Utility::endsWith(file.name, ext, false)) {
                            return false;
                        }
                    }
                    return file.type == c2d::Io::Type::File;
                }), files.end());
    }

    return files;
}

std::vector<c2d::Io::File> Io::list(const DeviceType &type, const std::string &path, int timeout,
                                    std::string *error, bool sort, bool showHidden) {
    std::vector<c2d::Io::File> files;
    if (type == DeviceType::Local) {
#ifdef __SWITCH__
        if (path == "ums_list") {
            for (const auto &mountName: usbGetMountNames()) {
                files.emplace_back(c2d::Utility::removeLastSlash(mountName), mountName, Io::Type::Directory, 0);
            }
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "Io::UsbList entries="
                                                                    + std::to_string(files.size()));
            return files;
        }
#endif
        files = c2d::C2DIo::getDirList(path, sort, showHidden);
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Io::Local path=" + path
                                                            + " entries=" + std::to_string(files.size()));
    } else if (type == DeviceType::Http || type == DeviceType::WebDav) {
        files = type == DeviceType::Http ? Http::listIndex(path, timeout, error)
                                         : Http::listWebDav(path, timeout, error);
    } else if (type == DeviceType::TorrServe) {
        files = pplay::TorrServe::getDirList(path, timeout, error);
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Io::TorrServe path=" + path
                                                            + " entries=" + std::to_string(files.size()));
    } else if (type == DeviceType::Ftp) {
        files = Ftp::list(path, timeout, error);
    }
#ifdef __SMB2__
    else if (type == DeviceType::Smb) {
        files = Smb::list(path, timeout, error);
    }
#endif
    return files;
}

static Io::DeviceType deviceTypeFromScheme(const std::string &path) {
    if (c2d::Utility::startWith(path, "http://") || c2d::Utility::startWith(path, "https://")) {
        return Io::DeviceType::Http;
    } else if (c2d::Utility::startWith(path, "ftp://")) {
        return Io::DeviceType::Ftp;
    } else if (c2d::Utility::startWith(path, "smb://")) {
        return Io::DeviceType::Smb;
    } else if (c2d::Utility::startWith(path, "tss://") || c2d::Utility::startWith(path, "ts://")) {
        // legacy TorrServe pseudo schemes
        return Io::DeviceType::TorrServe;
    }
    return Io::DeviceType::Local;
}

// url without user info, lower case host, no trailing slash
static std::string comparableUrl(const std::string &url) {
    std::string u = url;
    size_t scheme = u.find("://");
    if (scheme == std::string::npos) {
        return u;
    }
    size_t slash = u.find('/', scheme + 3);
    size_t at = u.find('@', scheme + 3);
    if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
        u.erase(scheme + 3, at - (scheme + 3) + 1);
        slash = u.find('/', scheme + 3);
    }
    size_t hostEnd = slash == std::string::npos ? u.size() : slash;
    std::transform(u.begin(), u.begin() + (long) hostEnd, u.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::replace(u.begin(), u.end(), '\\', '/');
    while (u.size() > scheme + 3 && c2d::Utility::endsWith(u, "/")) {
        u.pop_back();
    }
    return u;
}

int Io::findNetwork(const std::string &path, bool typedOnly) {
    if (path.find("://") == std::string::npos) {
        return -1;
    }
    if (config == nullptr) {
        return -1;
    }
    const std::string target = comparableUrl(path);
    int best = -1;
    size_t bestLen = 0;
    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        const std::string network = config->getOption(PPLAYConfig::networkOption(i))->getString();
        if (network.empty()) {
            continue;
        }
        if (typedOnly && parseNetworkType(
                config->getOption(PPLAYConfig::networkTypeOption(i))->getString()) == NetworkType::Auto) {
            continue;
        }
        const std::string root = comparableUrl(network);
        if (target == root || (c2d::Utility::startWith(target, root)
                               && (target[root.size()] == '/' || target[root.size()] == '?'))) {
            // longest match wins
            if (root.size() > bestLen) {
                best = i;
                bestLen = root.size();
            }
        }
    }
    return best;
}

std::string Io::getNetworkRoot(const std::string &path) {
    int index = findNetwork(path, false);
    if (index < 1) {
        return {};
    }
    std::string root = config->getOption(PPLAYConfig::networkOption(index))->getString();
    std::replace(root.begin(), root.end(), '\\', '/');
    if (!c2d::Utility::endsWith(root, "/")) {
        root += "/";
    }
    return root;
}

Io::DeviceType Io::getDeviceType(const std::string &path) {
    int index = findNetwork(path, true);
    if (index > 0) {
        switch (parseNetworkType(config->getOption(PPLAYConfig::networkTypeOption(index))->getString())) {
            case NetworkType::TorrServe:
                return DeviceType::TorrServe;
            case NetworkType::Smb:
                return DeviceType::Smb;
            case NetworkType::Ftp:
                return DeviceType::Ftp;
            case NetworkType::Http:
                return DeviceType::Http;
            case NetworkType::WebDav:
                return DeviceType::WebDav;
            default:
                break;
        }
    }

    return deviceTypeFromScheme(path);
}

void Io::syncRomFs(const std::string &relativePath) {
    const std::string romfs = getRomFsPath();
    if (romfs == getDataPath() || !exist(romfs + relativePath)) {
        return;
    }
    auto files = c2d::C2DIo::getDirList(romfs + relativePath, false, false);
    for (const auto &file: files) {
        if (file.name == "." || file.name == ".." || file.name == "sce_sys" || file.name == "sce_module") {
            continue;
        }
        std::string relPath = relativePath.empty() ? file.name : relativePath + "/" + file.name;
        std::string target = getDataPath() + relPath;
        if (file.type == c2d::Io::Type::Directory) {
            if (!exist(target)) {
                create(target);
            }
            syncRomFs(relPath);
        } else if (file.type == c2d::Io::Type::File) {
            // new or updated (size changed) file
            std::string sourcePath = romfs + relPath;
            size_t size = getSize(sourcePath);
            if (size > 0 && (!exist(target) || getSize(target) != size)) {
                char *buf = (char *) malloc(size);
                if (buf) {
                    read(sourcePath, buf, size);
                    write(target, buf, size);
                    free(buf);
                }
            }
        }
    }
}

bool Io::readText(const std::string &path, std::string &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    out.clear();
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    fclose(f);
    return true;
}

bool Io::writeText(const std::string &path, const std::string &text) {
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        remove(tmp.c_str());
        return false;
    }
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

Io::~Io() {
    if (g_io == this) {
        g_io = nullptr;
    }
}
