//
// Offline listings (see offline.h): "pPlay listing 1", then "D|F <size> <name> <path>"
// per file (tab separated, escaped). A damaged file is ignored.
//

#include <atomic>
#include <cstdio>
#include <mutex>
#include <unordered_map>

#include "io.h"
#include "offline.h"
#include "saved.h"
#include "utility.h"

#define LISTS_DIR ".lists"
#define LISTING_HEADER "pPlay listing 1"

using namespace pplay;

namespace {

    std::mutex g_mutex;
    // the text written this session per path (written again only if changed)
    std::unordered_map<std::string, std::string> g_written;
    std::atomic<bool> g_active{false};

    std::string dirOf() {
        return Saved::root() + LISTS_DIR;
    }

    std::string fileOf(const std::string &path) {
        return dirOf() + "/" + Utility::md5hash(path).substr(0, 16);
    }

    std::string escape(const std::string &s) {
        std::string out;
        for (char c: s) {
            out += c == '\\' ? "\\\\" : c == '\t' ? "\\t" : c == '\n' ? "\\n" : c == '\r' ? "\\r"
                                                                                             : std::string(1, c);
        }
        return out;
    }

    std::vector<std::string> fields(const std::string &line) {
        std::vector<std::string> out(1);
        for (size_t i = 0; i < line.size(); i++) {
            char c = line[i];
            if (c == '\t') {
                out.emplace_back();
            } else if (c == '\\' && i + 1 < line.size()) {
                char n = line[++i];
                out.back() += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
            } else {
                out.back() += c;
            }
        }
        return out;
    }
}

namespace pplay::Offline {

    void store(const std::string &path, const std::vector<c2d::Io::File> &files) {
        g_active = false;
        if (Saved::root().empty()) {
            return;
        }
        // the path first: a hash collision can't show another listing
        std::string text = LISTING_HEADER "\n" + escape(path) + "\n";
        for (auto &file: files) {
            text += std::string(file.type == c2d::Io::Type::Directory ? "D" : "F") + "\t"
                    + std::to_string((unsigned long long) file.size) + "\t" + escape(file.name) + "\t"
                    + escape(file.path) + "\n";
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_written.find(path);
        if (it != g_written.end() && it->second == text) {
            return;
        }
        Io *io = Io::get();
        if (!io->exist(dirOf())) {
            io->create(dirOf());
        }
        // whole or not at all
        std::string target = fileOf(path);
        if (io->writeText(target + ".tmp", text) && ::rename((target + ".tmp").c_str(), target.c_str()) == 0) {
            g_written[path] = text;
        } else {
            Utility::log(Utility::LogLevel::Error, "Offline: not written " + target);
        }
    }

    bool load(const std::string &path, std::vector<c2d::Io::File> *files) {
        if (Saved::root().empty()) {
            return false;
        }
        std::string text;
        if (!Io::get()->readText(fileOf(path), text)) {
            return false;
        }
        std::vector<c2d::Io::File> out;
        size_t start = 0;
        int line = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) {
                // cut short: damaged
                return false;
            }
            std::string row = text.substr(start, end - start);
            start = end + 1;
            if (line++ == 0) {
                if (row != LISTING_HEADER) {
                    return false;
                }
                continue;
            }
            std::vector<std::string> f = fields(row);
            if (line == 2) {
                if (f.size() != 1 || f[0] != path) {
                    return false;
                }
                continue;
            }
            if (f.size() != 4 || (f[0] != "D" && f[0] != "F") || f[2].empty() || f[3].empty()) {
                return false;
            }
            out.emplace_back(f[2], f[3], f[0] == "D" ? c2d::Io::Type::Directory : c2d::Io::Type::File,
                             (size_t) std::strtoull(f[1].c_str(), nullptr, 10));
        }
        if (line < 2) {
            return false;
        }
        files->swap(out);
        g_active = true;
        Utility::log(Utility::LogLevel::Info, "Offline: " + path + " from the kept listing, "
                                                      + std::to_string(files->size()) + " entries");
        return true;
    }

    bool active() {
        return g_active;
    }
}
