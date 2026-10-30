//
// Last playback dates (see history.h): "<time>\t<key>" lines, the most recent kept.
//

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include "history.h"
#include "utility.h"

namespace {

#define HISTORY_MAX 3000

    std::map<std::string, time_t> g_history;
    bool g_loaded = false;

    std::string historyPath() {
        return pplay::Utility::getDataPath() + "history";
    }

    void load() {
        g_loaded = true;
        FILE *f = fopen(historyPath().c_str(), "rb");
        if (!f) {
            return;
        }
        char buf[4096];
        while (fgets(buf, sizeof(buf), f)) {
            std::string line = buf;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            size_t tab = line.find('\t');
            if (tab == std::string::npos || tab + 1 >= line.size()) {
                continue;
            }
            g_history[line.substr(tab + 1)] = (time_t) strtoll(line.substr(0, tab).c_str(), nullptr, 10);
        }
        fclose(f);
    }

    void save() {
        std::vector<std::pair<time_t, std::string>> entries;
        for (auto &entry: g_history) {
            entries.emplace_back(entry.second, entry.first);
        }
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        if (entries.size() > HISTORY_MAX) {
            entries.resize(HISTORY_MAX);
            g_history.clear();
            for (auto &entry: entries) {
                g_history[entry.second] = entry.first;
            }
        }
        std::string tmp = historyPath() + ".tmp";
        FILE *f = fopen(tmp.c_str(), "wb");
        if (!f) {
            return;
        }
        for (auto &entry: entries) {
            fprintf(f, "%lld\t%s\n", (long long) entry.first, entry.second.c_str());
        }
        fclose(f);
        if (rename(tmp.c_str(), historyPath().c_str()) != 0) {
            remove(historyPath().c_str());
            rename(tmp.c_str(), historyPath().c_str());
        }
    }
}

namespace pplay::History {

    void touch(const std::string &key) {
        if (key.empty()) {
            return;
        }
        if (!g_loaded) {
            load();
        }
        g_history[key] = time(nullptr);
        save();
    }

    time_t lastPlayed(const std::string &key) {
        if (!g_loaded) {
            load();
        }
        auto it = g_history.find(key);
        return it == g_history.end() ? 0 : it->second;
    }
}
