//
// Media information cache: one text file per media, a line per value / track.
//

#include <fstream>
#include <cstdio>

#include "media_info.h"
#include "utility.h"

#define MEDIA_INFO_VERSION "pplay-media 3"

namespace {
    // tab separated fields, these escaped
    std::string escape(const std::string &s) {
        std::string out;
        for (char c: s) {
            if (c == '\\') {
                out += "\\\\";
            } else if (c == '\t') {
                out += "\\t";
            } else if (c == '\n') {
                out += "\\n";
            } else {
                out += c;
            }
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
                out.back() += n == 't' ? '\t' : n == 'n' ? '\n' : n;
            } else {
                out.back() += c;
            }
        }
        return out;
    }
}

MediaInfo::MediaInfo(const c2d::Io::File &file) {
    if (!pplay::Utility::isMedia(file)) {
        return;
    }
    std::ifstream in(pplay::Utility::getMediaInfoPath(file));
    std::string line;
    if (!std::getline(in, line) || line != MEDIA_INFO_VERSION) {
        return;
    }
    known = true;
    while (std::getline(in, line)) {
        std::vector<std::string> f = fields(line);
        if (f[0] == "duration" && f.size() > 1) {
            duration = std::atol(f[1].c_str());
        } else if (f[0] == "format" && f.size() > 1) {
            format = f[1];
        } else if (f[0] == "title" && f.size() > 1) {
            title = f[1];
        } else if (f[0] == "track" && f.size() >= 17) {
            Track t;
            t.type = f[1];
            t.id = std::atoi(f[2].c_str());
            t.title = f[3];
            t.language = f[4];
            t.codec = f[5];
            t.channels = std::atoi(f[6].c_str());
            t.layout = f[7];
            t.sample_rate = std::atoi(f[8].c_str());
            t.width = std::atoi(f[9].c_str());
            t.height = std::atoi(f[10].c_str());
            t.fps = std::atof(f[11].c_str());
            t.bit_rate = std::atol(f[12].c_str());
            t.isDefault = f[13] == "1";
            t.forced = f[14] == "1";
            t.albumart = f[15] == "1";
            auto &list = t.type == "video" ? videos : t.type == "audio" ? audios : subtitles;
            list.push_back(t);
        }
    }
}

void MediaInfo::save(const c2d::Io::File &file) {
    std::ofstream out(pplay::Utility::getMediaInfoPath(file), std::ios::trunc);
    if (!out.is_open()) {
        return;
    }
    out << MEDIA_INFO_VERSION << "\n";
    out << "duration\t" << duration << "\n";
    out << "format\t" << escape(format) << "\n";
    out << "title\t" << escape(title) << "\n";
    for (auto *list: {&videos, &audios, &subtitles}) {
        for (auto &t: *list) {
            if (t.external) {
                continue;
            }
            // the last one: spare
            out << "track\t" << t.type << "\t" << t.id << "\t" << escape(t.title) << "\t" << escape(t.language) << "\t"
                << escape(t.codec) << "\t" << t.channels << "\t" << escape(t.layout) << "\t" << t.sample_rate << "\t"
                << t.width << "\t" << t.height << "\t" << t.fps << "\t" << t.bit_rate << "\t" << (t.isDefault ? 1 : 0)
                << "\t" << (t.forced ? 1 : 0) << "\t" << (t.albumart ? 1 : 0) << "\t\n";
        }
    }
}

std::string MediaInfo::formatBitrate(long bps) {
    char buf[32];
    if (bps >= 1000000) {
        snprintf(buf, sizeof(buf), "%.1f Mb/s", (double) bps / 1000000);
    } else {
        snprintf(buf, sizeof(buf), "%ld kb/s", bps / 1000);
    }
    return buf;
}

std::string MediaInfo::describe(const Track &t) {
    std::vector<std::string> parts;
    if (!t.language.empty()) {
        parts.push_back(t.language);
    }
    parts.push_back(c2d::Utility::toUpper(t.codec));
    if (t.width > 0 && t.height > 0) {
        parts.push_back(std::to_string(t.width) + "x" + std::to_string(t.height));
    }
    if (t.fps > 0) {
        char fps[32];
        snprintf(fps, sizeof(fps), "%.3g fps", t.fps);
        parts.emplace_back(fps);
    }
    if (!t.layout.empty()) {
        parts.push_back(t.layout);
    } else if (t.channels > 0) {
        parts.push_back(std::to_string(t.channels) + "ch");
    }
    if (t.sample_rate > 0) {
        char rate[32];
        snprintf(rate, sizeof(rate), "%g kHz", t.sample_rate / 1000.0);
        parts.emplace_back(rate);
    }
    if (t.bit_rate > 0) {
        parts.push_back(formatBitrate(t.bit_rate));
    }
    std::string out;
    for (auto &p: parts) {
        out += (out.empty() ? "" : " ") + p;
    }
    if (!t.title.empty()) {
        out += " \u00AB" + t.title + "\u00BB";
    }
    if (t.forced) {
        out += " [forced]";
    }
    if (t.external) {
        out += " [ext]";
    }
    return out;
}

bool MediaInfo::hasCover() const {
    for (auto &t: videos) {
        if (t.albumart) {
            return true;
        }
    }
    return false;
}
