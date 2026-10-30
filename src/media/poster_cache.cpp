//
// Pictures cache (see poster_cache.h). Index lines: "<md5>\t<last use>\t<url>".
//

#include <cstdio>
#include <ctime>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>

#include "io.h"
#include "poster_cache.h"
#include "utility.h"

#define DAY (24 * 3600)

namespace {
    struct Entry {
        std::string hash;
        time_t used = 0;
    };

    std::mutex g_mutex;
    std::map<std::string, Entry> g_index;
    bool g_loaded = false;

    std::string dir() {
        return pplay::Utility::getDataPath() + "cache/posters/";
    }

    std::string indexPath() {
        return dir() + "index";
    }

    // a picture file name: 32 hex digits
    bool isHash(const std::string &name) {
        if (name.size() != 32) {
            return false;
        }
        for (char c: name) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
        return true;
    }

    bool isFile(const std::string &path, time_t *modified = nullptr) {
        struct stat st {};
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            return false;
        }
        if (modified != nullptr) {
            *modified = st.st_mtime;
        }
        return true;
    }

    // the regular files of a directory
    std::vector<std::string> files(const std::string &path) {
        std::vector<std::string> names;
        DIR *d = opendir(path.c_str());
        if (d == nullptr) {
            return names;
        }
        while (struct dirent *entry = readdir(d)) {
            std::string name = entry->d_name;
            if (name != "." && name != ".." && isFile(path + name)) {
                names.push_back(name);
            }
        }
        closedir(d);
        return names;
    }

    void load() {
        if (g_loaded) {
            return;
        }
        g_loaded = true;
        FILE *f = fopen(indexPath().c_str(), "rb");
        if (f == nullptr) {
            return;
        }
        char line[4096];
        while (fgets(line, sizeof(line), f) != nullptr) {
            std::string s = line;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
                s.pop_back();
            }
            size_t a = s.find('\t');
            size_t b = a == std::string::npos ? a : s.find('\t', a + 1);
            if (b == std::string::npos || !isHash(s.substr(0, a))) {
                continue;
            }
            g_index[s.substr(b + 1)] = {s.substr(0, a), (time_t) std::atoll(s.substr(a + 1, b - a - 1).c_str())};
        }
        fclose(f);
    }

    void save() {
        std::string text;
        for (auto &entry: g_index) {
            text += entry.second.hash + "\t" + std::to_string((long long) entry.second.used) + "\t" + entry.first + "\n";
        }
        if (pplay::Io::get() != nullptr) {
            pplay::Io::get()->writeText(indexPath(), text);
        }
    }

    bool referenced(const std::string &hash) {
        for (auto &entry: g_index) {
            if (entry.second.hash == hash) {
                return true;
            }
        }
        return false;
    }
}

namespace pplay::PosterCache {

    std::string find(const std::string &url) {
        std::lock_guard<std::mutex> lock(g_mutex);
        load();
        auto it = g_index.find(url);
        if (it == g_index.end()) {
            return "";
        }
        std::string path = dir() + it->second.hash;
        if (!isFile(path)) {
            g_index.erase(it);
            save();
            return "";
        }
        // the last use, a day precision is enough
        time_t now = time(nullptr);
        if (now - it->second.used > DAY) {
            it->second.used = now;
            save();
        }
        return path;
    }

    bool store(const std::string &url, const std::string &data, std::string *error) {
        std::string hash = Utility::md5hash(data);
        std::string path = dir() + hash;
        std::lock_guard<std::mutex> lock(g_mutex);
        load();
        if (Io::get() != nullptr && !Io::get()->exist(dir())) {
            Io::get()->create(dir());
        }
        if (!isFile(path)) {
            std::string tmp = path + ".tmp";
            FILE *f = fopen(tmp.c_str(), "wb");
            bool ok = f != nullptr && fwrite(data.data(), 1, data.size(), f) == data.size();
            ok = f != nullptr && fclose(f) == 0 && ok;
            if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
                Utility::deleteFile(tmp);
                *error = "can't write cache file";
                return false;
            }
        }
        g_index[url] = {hash, time(nullptr)};
        save();
        return true;
    }

    void forget(const std::string &url) {
        std::lock_guard<std::mutex> lock(g_mutex);
        load();
        auto it = g_index.find(url);
        if (it == g_index.end()) {
            return;
        }
        std::string hash = it->second.hash;
        g_index.erase(it);
        if (!referenced(hash)) {
            Utility::deleteFile(dir() + hash);
        }
        save();
    }

    void cleanup(int days) {
        std::lock_guard<std::mutex> lock(g_mutex);
        load();
        time_t old = time(nullptr) - (time_t) days * DAY;
        int removed = 0;

        // urls not used lately, then the pictures no url uses (the old cache layout too)
        std::set<std::string> used;
        for (auto it = g_index.begin(); it != g_index.end();) {
            if (it->second.used < old) {
                it = g_index.erase(it);
                removed++;
            } else {
                used.insert(it->second.hash);
                ++it;
            }
        }
        std::set<std::string> present;
        for (auto &name: files(dir())) {
            bool picture = isHash(name);
            bool tmp = name.size() == 36 && isHash(name.substr(0, 32)) && name.compare(32, 4, ".tmp") == 0;
            if (picture && used.count(name) > 0) {
                present.insert(name);
            } else if ((picture || tmp) && Utility::deleteFile(dir() + name)) {
                removed++;
            }
        }
        for (auto it = g_index.begin(); it != g_index.end();) {
            it = present.count(it->second.hash) > 0 ? std::next(it) : g_index.erase(it);
        }
        save();

        // media information: written on probe / playback
        std::string info = Utility::getDataPath() + "cache/";
        for (auto &name: files(info)) {
            time_t modified = 0;
            if (c2d::Utility::endsWith(name, ".info") && isFile(info + name, &modified) && modified < old
                && Utility::deleteFile(info + name)) {
                removed++;
            }
        }
        Utility::log(Utility::LogLevel::Info, "PosterCache: cleanup, " + std::to_string(removed) + " removed");
    }

    int clear() {
        std::lock_guard<std::mutex> lock(g_mutex);
        int count = 0;
        for (auto &name: files(dir())) {
            bool tmp = c2d::Utility::endsWith(name, ".tmp");
            if ((isHash(name) || name == "index" || tmp) && Utility::deleteFile(dir() + name)) {
                count++;
            }
        }
        g_index.clear();
        g_loaded = true;
        return count;
    }
}
