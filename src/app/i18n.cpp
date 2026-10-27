//
// User interface translations (see i18n.h).
//

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <dirent.h>

#include "i18n.h"

namespace {
    std::vector<std::string> g_paths;
    std::unordered_map<std::string, std::string> g_strings;
    std::string g_current = "en";

    std::string trim(const std::string &s) {
        size_t start = 0, end = s.size();
        while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) start++;
        while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) end--;
        return s.substr(start, end - start);
    }

    std::string unescape(const std::string &s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char c = s[++i];
                switch (c) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    default: out += c; break;   // "\\", "\=", "\#"
                }
            } else {
                out += s[i];
            }
        }
        return out;
    }

    bool readFile(const std::string &path, std::string &out) {
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) return false;
        char buf[4096];
        size_t n;
        out.clear();
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
        fclose(f);
        // utf-8 bom
        if (out.size() >= 3 && (unsigned char) out[0] == 0xEF
            && (unsigned char) out[1] == 0xBB && (unsigned char) out[2] == 0xBF) {
            out.erase(0, 3);
        }
        return true;
    }

    // key/value pairs of a .lang file
    bool parse(const std::string &path, std::unordered_map<std::string, std::string> &map) {
        std::string text;
        if (!readFile(path, text)) return false;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos) eol = text.size();
            std::string line = trim(text.substr(pos, eol - pos));
            pos = eol + 1;
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            // first "=" not escaped
            size_t eq = std::string::npos;
            for (size_t i = 0; i < line.size(); i++) {
                if (line[i] == '\\') { i++; continue; }
                if (line[i] == '=') { eq = i; break; }
            }
            if (eq == std::string::npos) continue;
            std::string key = unescape(trim(line.substr(0, eq)));
            std::string value = unescape(trim(line.substr(eq + 1)));
            if (!key.empty() && !value.empty()) map[key] = value;
        }
        return true;
    }

    std::string normalizeCode(std::string code) {
        std::transform(code.begin(), code.end(), code.begin(), [](unsigned char c) { return std::tolower(c); });
        return code;
    }
}

namespace pplay::I18n {

    void setSearchPaths(const std::vector<std::string> &paths) {
        g_paths.clear();
        for (auto path: paths) {
            if (path.empty()) continue;
            if (path.back() != '/') path += "/";
            if (std::find(g_paths.begin(), g_paths.end(), path) == g_paths.end()) g_paths.push_back(path);
        }
    }

    std::vector<Language> list() {
        std::vector<Language> languages = {{"en", "English"}};
        for (const auto &dir: g_paths) {
            DIR *d = opendir(dir.c_str());
            if (!d) continue;
            struct dirent *e;
            while ((e = readdir(d)) != nullptr) {
                std::string file = e->d_name;
                if (file.size() <= 5 || file.compare(file.size() - 5, 5, ".lang") != 0) continue;
                std::string code = normalizeCode(file.substr(0, file.size() - 5));
                std::unordered_map<std::string, std::string> map;
                if (!parse(dir + file, map)) continue;
                std::string name = map.count("@name") ? map["@name"] : code;
                auto it = std::find_if(languages.begin(), languages.end(),
                                       [&code](const Language &l) { return l.code == code; });
                if (it == languages.end()) languages.push_back({code, name});
                else it->name = name;
            }
            closedir(d);
        }
        std::sort(languages.begin() + 1, languages.end(),
                  [](const Language &a, const Language &b) { return a.code < b.code; });
        return languages;
    }

    bool load(const std::string &_code) {
        std::string code = normalizeCode(_code);
        g_strings.clear();
        g_current = "en";
        if (code.empty()) {
            return false;
        }
        bool found = code == "en";
        for (const auto &dir: g_paths) {
            found |= parse(dir + code + ".lang", g_strings);
        }
        if (found) g_current = code;
        return found;
    }

    const std::string &current() {
        return g_current;
    }

    const std::string &tr(const std::string &text) {
        if (g_strings.empty() || text.empty()) return text;
        auto it = g_strings.find(text);
        return it != g_strings.end() ? it->second : text;
    }
}
