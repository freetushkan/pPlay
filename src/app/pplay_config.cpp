//
// Created by cpasjuste on 22/10/18.
//

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <json.hpp>
#include "io.h"
#include "pplay_config.h"
#include "net_http.h"
#include "utility.h"

using namespace c2d;

namespace {
    const char *networkSuffixes[] = {"",         "_LAST",       "_NAME", "_TYPE",      "_USER",
                                     "_PASSWORD", "_TIMEOUT", "_TLS_VERIFY", "_SYNC", "_SMB_BUFFER"};
    const int NETWORK_FIELDS = 10;
    // integers from this one
    const int NETWORK_INTS = 6;

    // the json keys of a network
    const char *networkKeys[] = {"url",      "last",    "name",       "type", "user",
                                 "password", "timeout", "tls_verify", "sync", "smb_buffer"};

    // names kept for the program life: pointers given out (a deque does not move them)
    const char *networkOptionName(int index, int suffix) {
        static std::deque<std::string> names[NETWORK_FIELDS];
        index = std::max(1, index);
        while ((int) names[suffix].size() < index) {
            names[suffix].push_back("NETWORK" + std::to_string(names[suffix].size() + 1) + networkSuffixes[suffix]);
        }
        return names[suffix][(size_t) index - 1].c_str();
    }

    // the file groups, as in the menu; the others in "state"
    const std::vector<std::pair<const char *, std::vector<const char *>>> &groups() {
        static const std::vector<std::pair<const char *, std::vector<const char *>>> list = {
                {"playback", {OPT_AUTOPLAY_MODE, OPT_NETWORK_RETRIES, OPT_RESTORE_SESSION, OPT_RESTORE_LOCATION,
                              OPT_SLEEP_TIMER}},
                {"progress", {OPT_PROGRESS_MIN_LENGTH, OPT_WATCHED_TOLERANCE, OPT_PROGRESS_INTERVAL,
                              OPT_PROGRESS_LINE, OPT_FOLDER_MARKS, OPT_TS_PROGRESS}},
                {"audio_and_subtitles", {OPT_AUDIO_LANGUAGE, OPT_SUB_LANGUAGE, OPT_EXTERNAL_TRACKS, OPT_SUB_ASS_PLAIN,
                                         OPT_AUDIO_CHANNELS, OPT_AUDIO_NORMALIZE, OPT_AUDIO_VISUALIZER}},
                {"video", {OPT_VIDEO_QUALITY, OPT_AUTO_DROPS, OPT_AUTO_DROPS_TIME, OPT_AUTO_QUALITY_DELAY,
                           OPT_AUTO_SWITCH_PAUSE, OPT_HW_DECODING, OPT_AUTO_HW_RETRY, OPT_AUTO_HW_RETRIES, OPT_DITHER}},
                {"interface", {OPT_LANGUAGE, OPT_PS5_UI_4K, OPT_TEXT_ENCODING, OPT_ACCENT_COLOR, OPT_SHOW_HIDDEN,
                               OPT_SHOW_ALL_FILES, OPT_SHOW_SAVED, OPT_SHOW_PLAYLISTS, OPT_MEDIA_PROBE,
                               OPT_OSD_HIDE_TIME, OPT_SWAP_CONTROLS}},
                {"networks", {OPT_NETWORK_TIMEOUT, OPT_TLS_VERIFY, OPT_TS_VIEWED_SYNC, OPT_CACHE_SECS, OPT_CACHE_MB,
#ifdef __SMB2__
                              OPT_SMB_READ_BUFFER_MB
#endif
                             }},
                {"saved_files", {OPT_SAVE_THREADS, OPT_SAVE_KEEP_DAYS, OPT_SAVE_MAX_GB}},
                {"system", {OPT_HOME_PATH, OPT_STANDBY_TIME, OPT_ANDROID_BACKGROUND, OPT_CPU_BOOST,
                            OPT_PS5_KEEP_IN_REST, OPT_LOG_LEVEL}},
        };
        return list;
    }

    std::string keyOf(const std::string &option) {
        std::string key = option;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
        return key;
    }

    std::string groupOf(const std::string &option) {
        static std::map<std::string, std::string> of;
        if (of.empty()) {
            for (auto &group: groups()) {
                for (auto *name: group.second) {
                    of[name] = group.first;
                }
            }
        }
        auto it = of.find(option);
        return it != of.end() ? it->second : "state";
    }

    bool isNetworkOption(const std::string &name) {
        return name.rfind("NETWORK", 0) == 0 && name.size() > 7 && std::isdigit((unsigned char) name[7]);
    }

    nlohmann::ordered_json valueOf(config::Option &option) {
        switch (option.getType()) {
            case config::Option::Type::Integer:
                return option.getInteger();
            case config::Option::Type::Float:
                return option.getFloat();
            default:
                return option.getString();
        }
    }

    void setValue(config::Option *option, const nlohmann::json &value) {
        if (option == nullptr) {
            return;
        }
        if (option->getType() == config::Option::Type::Integer) {
            if (value.is_number()) {
                option->setInteger(value.get<int>());
            } else if (value.is_string()) {
                option->setInteger(std::atoi(value.get<std::string>().c_str()));
            }
        } else if (option->getType() == config::Option::Type::Float) {
            if (value.is_number()) {
                option->setFloat(value.get<float>());
            }
        } else if (value.is_string()) {
            option->setString(value.get<std::string>());
        } else if (value.is_number_integer()) {
            option->setString(std::to_string(value.get<int>()));
        }
    }
}

const char *PPLAYConfig::networkOption(int index) {
    return networkOptionName(index, 0);
}

const char *PPLAYConfig::networkLastOption(int index) {
    return networkOptionName(index, 1);
}

const char *PPLAYConfig::networkNameOption(int index) {
    return networkOptionName(index, 2);
}

const char *PPLAYConfig::networkTypeOption(int index) {
    return networkOptionName(index, 3);
}

const char *PPLAYConfig::networkUserOption(int index) {
    return networkOptionName(index, 4);
}

const char *PPLAYConfig::networkPasswordOption(int index) {
    return networkOptionName(index, 5);
}

const char *PPLAYConfig::networkTimeoutOption(int index) {
    return networkOptionName(index, 6);
}

const char *PPLAYConfig::networkTlsOption(int index) {
    return networkOptionName(index, 7);
}

const char *PPLAYConfig::networkSyncOption(int index) {
    return networkOptionName(index, 8);
}

const char *PPLAYConfig::networkSmbBufferOption(int index) {
    return networkOptionName(index, 9);
}

// the user and password of an url (scheme://user[:password]@host...): smb "domain.user" as "domain\user"
static bool credentialsOfUrl(const std::string &url, std::string *user, std::string *password) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return false;
    }
    size_t end = url.find_first_of("/?#", scheme + 3);
    std::string authority = url.substr(scheme + 3, end == std::string::npos ? std::string::npos : end - scheme - 3);
    size_t at = authority.rfind('@');
    if (at == std::string::npos || at == 0) {
        return false;
    }
    std::string info = authority.substr(0, at);
    size_t colon = info.find(':');
    *user = info.substr(0, colon);
    *password = colon == std::string::npos ? "" : info.substr(colon + 1);
    if (Utility::startWith(url, "smb://")) {
        size_t dot = user->find('.');
        if (dot != std::string::npos) {
            (*user)[dot] = '\\';
        }
    } else {
        *user = pplay::Http::unescape(*user);
        *password = pplay::Http::unescape(*password);
    }
    return !user->empty();
}

// the user info part of an url: [start, end) of "user[:password]@", false if none
static bool userInfoOf(const std::string &url, size_t *start, size_t *end) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return false;
    }
    *start = scheme + 3;
    size_t host = url.find_first_of("/?#", *start);
    size_t at = url.rfind('@', host == std::string::npos ? std::string::npos : host);
    if (at == std::string::npos || at < *start) {
        *end = *start;
        return false;
    }
    *end = at + 1;
    return true;
}

// as typed in the older urls: only what would break the url escaped
static std::string escapeUserInfo(const std::string &value) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c: value) {
        if (c <= ' ' || c >= 0x7F || std::strchr("@:/?#%", c) != nullptr) {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        } else {
            out += (char) c;
        }
    }
    return out;
}

std::string PPLAYConfig::withoutCredentials(const std::string &url) {
    size_t start, end;
    return userInfoOf(url, &start, &end) ? url.substr(0, start) + url.substr(end) : url;
}

std::string PPLAYConfig::withNetworkCredentials(int index, const std::string &url) {
    std::string plain = withoutCredentials(url);
    std::string user = getOption(networkUserOption(index))->getString();
    std::string password = getOption(networkPasswordOption(index))->getString();
    size_t start, end;
    userInfoOf(plain, &start, &end);
    if (user.empty() || plain.find("://") == std::string::npos ||
        pplay::Io::parseNetworkType(getOption(networkTypeOption(index))->getString()) == pplay::Io::NetworkType::Dlna) {
        return plain;
    }
    std::string info;
    if (Utility::startWith(plain, "smb://")) {
        // "domain\user" as "domain.user"
        size_t slash = user.find('\\');
        if (slash != std::string::npos) {
            user[slash] = '.';
        }
        info = user + (password.empty() ? "" : ":" + password);
    } else {
        info = escapeUserInfo(user) + (password.empty() ? "" : ":" + escapeUserInfo(password));
    }
    return plain.substr(0, start) + info + "@" + plain.substr(start);
}

int PPLAYConfig::networkOf(const std::string &option, const char *suffix) {
    if (option.rfind("NETWORK", 0) != 0) {
        return 0;
    }
    char *end = nullptr;
    long index = std::strtol(option.c_str() + 7, &end, 10);
    return index > 0 && end != nullptr && std::strcmp(end, suffix) == 0 ? (int) index : 0;
}

void PPLAYConfig::setNetworkUrl(int index, const std::string &url) {
    std::string user, password;
    if (credentialsOfUrl(url, &user, &password)) {
        getOption(networkUserOption(index))->setString(user);
        getOption(networkPasswordOption(index))->setString(password);
    }
    getOption(networkOption(index))->setString(withoutCredentials(url));
    updateNetworkCredentials(index);
}

void PPLAYConfig::updateNetworkCredentials(int index) {
    if (index < 1 || index > networkCount) {
        return;
    }
    config::Option *url = getOption(networkOption(index));
    url->setString(withNetworkCredentials(index, url->getString()));
    // the last location: on that network only
    config::Option *last = getOption(networkLastOption(index));
    std::string plain = withoutCredentials(last->getString());
    if (Utility::startWith(plain, withoutCredentials(url->getString()))) {
        last->setString(withNetworkCredentials(index, plain));
    }
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
    : Config("PPLAY", _io->getDataPath() + "pplay_config.json", version), io(_io) {
    // the options not moved (read by other threads) while networks are added
    getOptions()->reserve(2048);

    std::string homePath = defaultHomePath();
    addOption({OPT_HOME_PATH, homePath});
    addOption({OPT_LAST_LOCAL_PATH, homePath});
    addOption({OPT_LAST_MODULE, "LOCAL"});
    addOption({OPT_NETWORK_TIMEOUT, (int) 15});
    addOption({OPT_TLS_VERIFY, (int) 1});
    addOption({OPT_WATCHED_TOLERANCE, (int) 0});
    addOption({OPT_NETWORK_RETRIES, (int) 3});
#ifdef __SMB2__
    addOption({OPT_SMB_READ_BUFFER_MB, (int) 10});
#endif
    addOption({OPT_AUTOPLAY_MODE, (int) 1});
    addOption({OPT_LOG_LEVEL, (int) 0}); // 0 OFF, 1 ERROR, 2 INFO, 3 DEBUG, 4 TRACE
    addOption({OPT_SWAP_CONTROLS, (int) 0});
#ifdef __SWITCH__
    addOption({OPT_CPU_BOOST, "Disabled"}); // Disabled, Enabled
#endif
#ifdef PPLAY_HW_DECODING
    addOption({OPT_HW_DECODING, (int) 1});
#endif
    addOption({OPT_ACCENT_COLOR, "#1078C8"});
    addOption({OPT_LANGUAGE, "en"});
    addOption({OPT_AUDIO_LANGUAGE, "Auto"});
    addOption({OPT_SUB_LANGUAGE, "Off"});
    addOption({OPT_EXTERNAL_TRACKS, (int) 1});
#if defined(__PS4__) || defined(__PS5__)
    addOption({OPT_SUB_ASS_PLAIN, (int) 1});
#else
    addOption({OPT_SUB_ASS_PLAIN, (int) 0});
#endif
    addOption({OPT_AUDIO_NORMALIZE, (int) 0});
    addOption({OPT_AUDIO_CHANNELS, (int) 0});
    addOption({OPT_VOLUME, (int) -1});
    addOption({OPT_CACHE_SECS, (int) 60});
    addOption({OPT_CACHE_MB, (int) 150});
    addOption({OPT_VIDEO_QUALITY, (int) 1});
    addOption({OPT_DITHER, (int) 0});
    addOption({OPT_AUTO_DROPS, (int) 10});
    addOption({OPT_AUTO_DROPS_TIME, (int) 10});
    addOption({OPT_AUTO_QUALITY_DELAY, (int) 30});
    addOption({OPT_AUTO_HW_RETRY, (int) 1});
    addOption({OPT_AUTO_HW_RETRIES, (int) 3});
    addOption({OPT_AUTO_SWITCH_PAUSE, (int) 0});
    addOption({OPT_TS_VIEWED_SYNC, (int) 30});
    addOption({OPT_MEDIA_PROBE, (int) 1});
    addOption({OPT_AUDIO_VISUALIZER, (int) 0});
    addOption({OPT_TEXT_ENCODING, (int) 0});
    addOption({OPT_TS_PROGRESS, (int) 2});
    addOption({OPT_PROGRESS_LINE, (int) 1});
    addOption({OPT_OSD_HIDE_TIME, (int) 4});
    addOption({OPT_FOLDER_MARKS, (int) 1});
    addOption({OPT_SHOW_SAVED, (int) 1});
    addOption({OPT_ANDROID_BACKGROUND, (int) 0});
    addOption({OPT_SHOW_PLAYLISTS, (int) 1});
    addOption({OPT_PROGRESS_MIN_LENGTH, (int) 5});
    addOption({OPT_PROGRESS_INTERVAL, (int) 10});
    addOption({OPT_PS5_KEEP_IN_REST, (int) 0});
    addOption({OPT_REST_SESSION, ""});
    addOption({OPT_RESTORE_SESSION, (int) 2});
    addOption({OPT_RESTORE_LOCATION, (int) 1});
#ifdef __SWITCH__
    addOption({OPT_SAVE_THREADS, (int) 1});
#else
    addOption({OPT_SAVE_THREADS, (int) 2});
#endif
    addOption({OPT_SAVE_KEEP_DAYS, (int) 0});
    addOption({OPT_SAVE_MAX_GB, (int) 0});
    addOption({OPT_LAST_SAVED_PATH, ""});
    addOption({OPT_SHOW_HIDDEN, (int) 0});
    addOption({OPT_SHOW_ALL_FILES, (int) 0});
    // idle minutes before standby, 0: system timers, -1: never
#ifdef __SWITCH__
    addOption({OPT_STANDBY_TIME, (int) 0});
#else
    addOption({OPT_STANDBY_TIME, (int) 60});
#endif
    // minutes, -1: end of file, 0: off
    addOption({OPT_SLEEP_TIMER, (int) 0});
#ifdef __PS5__
    addOption({OPT_PS5_UI_4K, (int) 0});
#endif
    addOption({OPT_LAST_PLAYLIST, ""});
    addOption({OPT_LAST_PLAYLISTS_PATH, ""});

    load();
    sanitize();
    // the new options written
    save();
}

void PPLAYConfig::allocateNetworks(int count) {
    while (networkSlots < count) {
        networkSlots++;
        addOption({networkOption(networkSlots), ""});
        addOption({networkLastOption(networkSlots), "/"});
        // "": "Network n"
        addOption({networkNameOption(networkSlots), ""});
        addOption({networkTypeOption(networkSlots), "TorrServe"}); // TorrServe, Samba, FTP, HTTP, WebDAV, DLNA
        addOption({networkUserOption(networkSlots), ""});
        addOption({networkPasswordOption(networkSlots), ""});
        for (int s = NETWORK_INTS; s < NETWORK_FIELDS; s++) {
            addOption({networkOptionName(networkSlots, s), (int) 0});
        }
    }
}

// new networks: the settings of the older versions (one for all), else their defaults
int PPLAYConfig::networkDefault(int field) {
    const char *global = field == 6   ? OPT_NETWORK_TIMEOUT
                         : field == 7 ? OPT_TLS_VERIFY
                         : field == 8 ? OPT_TS_VIEWED_SYNC
                                      : "SMB_READ_BUFFER_MB";
    config::Option *option = getOption(global);
    return option != nullptr ? option->getInteger() : field == 6 ? 15 : field == 7 ? 1 : field == 8 ? 30 : 10;
}

void PPLAYConfig::clearNetwork(int index) {
    getOption(networkOption(index))->setString("");
    getOption(networkLastOption(index))->setString("/");
    getOption(networkNameOption(index))->setString("");
    getOption(networkTypeOption(index))->setString("TorrServe");
    getOption(networkUserOption(index))->setString("");
    getOption(networkPasswordOption(index))->setString("");
    for (int s = NETWORK_INTS; s < NETWORK_FIELDS; s++) {
        getOption(networkOptionName(index, s))->setInteger(networkDefault(s));
    }
}

void PPLAYConfig::copyNetwork(int from, int to) {
    for (int s = 0; s < NETWORK_FIELDS; s++) {
        config::Option *source = getOption(networkOptionName(from, s)), *target = getOption(networkOptionName(to, s));
        if (s < NETWORK_INTS) {
            target->setString(source->getString());
        } else {
            target->setInteger(source->getInteger());
        }
    }
}

int PPLAYConfig::addNetwork() {
    allocateNetworks(networkCount + 1);
    networkCount++;
    clearNetwork(networkCount);
    return networkCount;
}

void PPLAYConfig::removeNetwork(int index) {
    if (index < 1 || index > networkCount) {
        return;
    }
    for (int i = index; i < networkCount; i++) {
        copyNetwork(i + 1, i);
    }
    clearNetwork(networkCount);
    networkCount--;
}

bool PPLAYConfig::moveNetwork(int index, int direction) {
    int other = index + direction;
    if (index < 1 || index > networkCount || other < 1 || other > networkCount) {
        return false;
    }
    // (a free slot to swap through)
    allocateNetworks(networkCount + 1);
    copyNetwork(index, networkCount + 1);
    copyNetwork(other, index);
    copyNetwork(networkCount + 1, other);
    clearNetwork(networkCount + 1);
    return true;
}

std::string PPLAYConfig::networkName(int index) {
    std::string name = index >= 1 && index <= networkCount ? getOption(networkNameOption(index))->getString() : "";
    return name.empty() ? "Network " + std::to_string(index) : name;
}

bool PPLAYConfig::load(const std::string &overridePath) {
    std::string path = overridePath.empty() ? getPath() : overridePath;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return importOld();
    }
    std::stringstream text;
    text << in.rdbuf();
    nlohmann::json root = nlohmann::json::parse(text.str(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        printf("PPLAYConfig::load: %s: not valid json\n", path.c_str());
        return false;
    }
    for (auto &option: *getOptions()) {
        const std::string &name = option.getName();
        if (name == "VERSION" || isNetworkOption(name)) {
            continue;
        }
        auto group = root.find(groupOf(name));
        if (group != root.end() && group->is_object()) {
            auto value = group->find(keyOf(name));
            if (value != group->end()) {
                setValue(&option, *value);
            }
        }
    }
    networkCount = 0;
    auto networks = root.find("networks");
    if (networks != root.end() && networks->is_object()) {
        auto list = networks->find("list");
        if (list != networks->end() && list->is_array()) {
            for (auto &entry: *list) {
                if (!entry.is_object()) {
                    continue;
                }
                int index = addNetwork();
                for (int s = 0; s < NETWORK_FIELDS; s++) {
                    auto value = entry.find(networkKeys[s]);
                    if (value != entry.end()) {
                        setValue(getOption(networkOptionName(index, s)), *value);
                    }
                }
                // typed in the file: theirs over the options
                setNetworkUrl(index, getOption(networkOption(index))->getString());
            }
        }
    }
    return true;
}

bool PPLAYConfig::save() {
    // in the menu order
    nlohmann::ordered_json root = nlohmann::ordered_json::object();
    root["version"] = getAppVersion();
    for (auto &group: groups()) {
        root[group.first] = nlohmann::ordered_json::object();
        for (auto *name: group.second) {
            config::Option *option = getOption(name);
            // (now per network: read from an older file only)
            bool perNetwork = std::strcmp(name, OPT_NETWORK_TIMEOUT) == 0 || std::strcmp(name, OPT_TLS_VERIFY) == 0
                              || std::strcmp(name, OPT_TS_VIEWED_SYNC) == 0
                              || std::strcmp(name, "SMB_READ_BUFFER_MB") == 0;
            if (option != nullptr && !perNetwork) {
                root[group.first][keyOf(name)] = valueOf(*option);
            }
        }
    }
    root["state"] = nlohmann::ordered_json::object();
    nlohmann::ordered_json list = nlohmann::ordered_json::array();
    for (int i = 1; i <= networkCount; i++) {
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        for (int s: {2, 3, 0, 4, 5, 6, 7, 8, 9, 1}) {
            config::Option *option = getOption(networkOptionName(i, s));
            if (s >= NETWORK_INTS) {
                entry[networkKeys[s]] = option->getInteger();
            } else {
                // (the user and password apart)
                entry[networkKeys[s]] = s <= 1 ? withoutCredentials(option->getString()) : option->getString();
            }
        }
        list.push_back(entry);
    }
    root["networks"]["list"] = list;
    for (auto &option: *getOptions()) {
        const std::string &name = option.getName();
        if (name == "VERSION" || isNetworkOption(name) || groupOf(name) != "state") {
            continue;
        }
        root["state"][keyOf(name)] = valueOf(option);
    }

    // written aside, then in place (not left half written)
    std::string path = getPath(), temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << root.dump(2) << "\n";
        if (!out.good()) {
            return false;
        }
    }
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(path.c_str());
        return std::rename(temp.c_str(), path.c_str()) == 0;
    }
    return true;
}

// pplay.cfg (kept as it is for the older versions): the networks set apart, theirs only (with
// an url), their users and passwords out of the urls
bool PPLAYConfig::importOld() {
    const int oldNetworks = 9;
    std::string cfg = io->getDataPath() + "pplay.cfg";
    if (!io->exist(cfg)) {
        return false;
    }
    allocateNetworks(oldNetworks);
    networkCount = oldNetworks;
    if (!Config::load(cfg)) {
        networkCount = 0;
        for (int i = 1; i <= oldNetworks; i++) {
            clearNetwork(i);
        }
        return false;
    }
    printf("PPLAYConfig::importOld: %s\n", cfg.c_str());
    if (getConfigVersion() < 2) {
#ifndef __LINUX__
        std::string home = defaultHomePath();
        getOption(OPT_HOME_PATH)->setString(home);
        getOption(OPT_LAST_LOCAL_PATH)->setString(home);
#endif
    }
    std::string lastModule = getOption(OPT_LAST_MODULE)->getString();
    int lastNetwork = lastModule.rfind("NETWORK", 0) == 0 ? std::atoi(lastModule.c_str() + 7) : 0;
    int kept = 0;
    bool lastKept = lastNetwork == 0;
    for (int i = 1; i <= oldNetworks; i++) {
        std::string url = getOption(networkOption(i))->getString();
        if (url.empty()) {
            continue;
        }
        kept++;
        if (kept != i) {
            copyNetwork(i, kept);
        }
        if (lastNetwork == i) {
            getOption(OPT_LAST_MODULE)->setString("NETWORK" + std::to_string(kept));
            lastKept = true;
        }
        // the old default names: shown as such
        if (getOption(networkNameOption(kept))->getString() == "Network " + std::to_string(i)) {
            getOption(networkNameOption(kept))->setString("");
        }
        // no "Auto" type any more: the one its url gave
        if (pplay::Io::parseNetworkType(getOption(networkTypeOption(kept))->getString()) == pplay::Io::NetworkType::Auto) {
            std::string type = Utility::startWith(url, "smb://")   ? "Samba"
                               : Utility::startWith(url, "ftp://") ? "FTP"
                               : Utility::startWith(url, "http")   ? "HTTP"
                                                                   : "TorrServe";
            getOption(networkTypeOption(kept))->setString(type);
        }
        if (migrateTorrServeUrl(url)) {
            getOption(networkOption(kept))->setString(url);
            getOption(networkTypeOption(kept))->setString("TorrServe");
            std::string last = getOption(networkLastOption(kept))->getString();
            if (migrateTorrServeUrl(last)) {
                getOption(networkLastOption(kept))->setString(last);
            }
        }
        std::string user, password;
        if (getOption(networkUserOption(kept))->getString().empty() && credentialsOfUrl(url, &user, &password)) {
            getOption(networkUserOption(kept))->setString(user);
            getOption(networkPasswordOption(kept))->setString(password);
        }
    }
    for (int i = 1; i <= kept; i++) {
        updateNetworkCredentials(i);
        for (int s = NETWORK_INTS; s < NETWORK_FIELDS; s++) {
            getOption(networkOptionName(i, s))->setInteger(networkDefault(s));
        }
    }
    for (int i = kept + 1; i <= oldNetworks; i++) {
        clearNetwork(i);
    }
    networkCount = kept;
    if (!lastKept) {
        getOption(OPT_LAST_MODULE)->setString("LOCAL");
    }
    return true;
}

std::string PPLAYConfig::defaultHomePath() const {
#ifdef __LINUX__
    // the user home, not the settings directory
    const char *home = getenv("HOME");
    if (home && home[0] == '/') {
        return std::string(home) + "/";
    }
    return io->getDataPath();
#else
#if defined(__SWITCH__)
    std::string path = "/media/"; // sdmc
#elif defined(__ANDROID__)
    // the shared storage (all files access asked at the start)
    std::string path = "/storage/emulated/0/";
#else
    std::string path = "/data/media/";
#endif
    if (!io->exist(path)) {
        io->create(path);
    }
    return io->exist(path) ? path : io->getDataPath();
#endif
}

bool PPLAYConfig::reload() {
    bool ok = load();
    sanitize();
    return ok;
}

void PPLAYConfig::sanitize() {

    // a missing home is kept (an unplugged drive), only an empty one is reset
    if (getOption(OPT_HOME_PATH)->getString().empty()) {
        getOption(OPT_HOME_PATH)->setString(defaultHomePath());
    }

    if (!io->exist(getOption(OPT_LAST_LOCAL_PATH)->getString())) {
        getOption(OPT_LAST_LOCAL_PATH)->setString(getOption(OPT_HOME_PATH)->getString());
    }
    for (int i = 1; i <= networkCount; i++) {
        if (getOption(networkLastOption(i))->getString().empty()) {
            getOption(networkLastOption(i))->setString("/");
        }
        if (pplay::Io::parseNetworkType(getOption(networkTypeOption(i))->getString()) == pplay::Io::NetworkType::Auto) {
            getOption(networkTypeOption(i))->setString("TorrServe");
        }
        auto clampNetwork = [this](const char *name, int min, int max) {
            int value = getOption(name)->getInteger();
            getOption(name)->setInteger(std::max(min, std::min(max, value)));
        };
        clampNetwork(networkTimeoutOption(i), 1, 300);
        clampNetwork(networkTlsOption(i), 0, 1);
        clampNetwork(networkSyncOption(i), 10, 600);
        clampNetwork(networkSmbBufferOption(i), 1, 100);
    }
    if (getOption(OPT_LAST_MODULE)->getString().empty()) {
        getOption(OPT_LAST_MODULE)->setString("LOCAL");
    }

#ifdef __SMB2__
    if (getOption(OPT_SMB_READ_BUFFER_MB)->getInteger() < 1) {
        getOption(OPT_SMB_READ_BUFFER_MB)->setInteger(1);
    } else if (getOption(OPT_SMB_READ_BUFFER_MB)->getInteger() > 100) {
        getOption(OPT_SMB_READ_BUFFER_MB)->setInteger(100);
    }
#endif

    auto clamp = [this](const char *name, int min, int max) {
        int value = getOption(name)->getInteger();
        if (value < min || value > max) {
            getOption(name)->setInteger(value < min ? min : max);
        }
    };
    clamp(OPT_CACHE_SECS, 10, 600);
    clamp(OPT_CACHE_MB, 50, CACHE_MB_MAX);
    clamp(OPT_VIDEO_QUALITY, 0, 3);
    clamp(OPT_AUDIO_CHANNELS, 0, 3);
    clamp(OPT_DITHER, 0, 2);
    clamp(OPT_AUTO_DROPS, 1, 50);
    clamp(OPT_AUTO_DROPS_TIME, 5, 60);
    clamp(OPT_AUTO_QUALITY_DELAY, 0, 120);
    clamp(OPT_AUTO_HW_RETRY, 0, 30);
    clamp(OPT_AUTO_HW_RETRIES, 1, 10);
    clamp(OPT_TS_VIEWED_SYNC, 10, 600);
#ifdef PPLAY_HW_DECODING
    clamp(OPT_HW_DECODING, 0, 2);
#endif
    clamp(OPT_PROGRESS_MIN_LENGTH, 0, 60);
    clamp(OPT_PROGRESS_INTERVAL, 5, 120);
    clamp(OPT_SAVE_THREADS, 1, 4);
    clamp(OPT_STANDBY_TIME, -1, 1440);
    clamp(OPT_OSD_HIDE_TIME, 0, 30);
    clamp(OPT_SLEEP_TIMER, -1, 1440);
    clamp(OPT_SAVE_KEEP_DAYS, 0, 3650);
    clamp(OPT_SAVE_MAX_GB, 0, 100000);

    if (!pplay::Utility::isValidHexColor(getOption(OPT_ACCENT_COLOR)->getString())) {
        getOption(OPT_ACCENT_COLOR)->setString("#1078C8");
    }
    pplay::Utility::setAccentColor(getOption(OPT_ACCENT_COLOR)->getString());

    if (getOption(OPT_LANGUAGE)->getString().empty()) {
        getOption(OPT_LANGUAGE)->setString("en");
    }
}
