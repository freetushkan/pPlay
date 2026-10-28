//
// External audio / subtitles files (see external_tracks.h).
//

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>

#include "external_tracks.h"
#include "io.h"
#include "paths.h"
#include "torrserve.h"
#include "utility.h"

namespace {

    // sub directories levels, listings (network requests) at most
#define FIND_DEPTH 2
#define FIND_MAX_LISTINGS 24

    std::string lower(const std::string &s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return c < 0x80 ? (char) std::tolower(c) : (char) c;
        });
        return out;
    }

    bool endsWithAny(const std::string &name, const std::vector<std::string> &extensions) {
        for (const auto &ext: extensions) {
            if (c2d::Utility::endsWith(name, ext, false)) {
                return true;
            }
        }
        return false;
    }

    std::string baseName(const std::string &name) {
        size_t dot = name.find_last_of('.');
        return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    }

    // lower case, ascii punctuation and spaces removed (utf-8 bytes kept)
    std::string normalize(const std::string &s) {
        std::string out;
        for (unsigned char c: s) {
            if (c >= 0x80 || std::isalnum(c)) {
                out += c < 0x80 ? (char) std::tolower(c) : (char) c;
            }
        }
        return out;
    }

    // "s01e05" / "1x05" -> "1e5", or ""
    std::string episodeKey(const std::string &name) {
        std::string n = lower(name);
        for (size_t i = 0; i + 3 < n.size(); i++) {
            size_t p = i;
            bool sxe = n[p] == 's' && std::isdigit((unsigned char) n[p + 1]);
            if (sxe) {
                p++;
            } else if (!std::isdigit((unsigned char) n[p]) || (p > 0 && std::isdigit((unsigned char) n[p - 1]))) {
                continue;
            }
            int season = 0, digits = 0;
            while (p < n.size() && std::isdigit((unsigned char) n[p]) && digits < 3) {
                season = season * 10 + (n[p] - '0');
                p++;
                digits++;
            }
            if (p >= n.size() || (sxe ? n[p] != 'e' : n[p] != 'x')) {
                continue;
            }
            p++;
            int episode = 0;
            digits = 0;
            while (p < n.size() && std::isdigit((unsigned char) n[p]) && digits < 4) {
                episode = episode * 10 + (n[p] - '0');
                p++;
                digits++;
            }
            if (digits == 0 || (p < n.size() && std::isdigit((unsigned char) n[p]))) {
                continue;
            }
            return std::to_string(season) + "e" + std::to_string(episode);
        }
        return "";
    }

    std::string trimDecorations(const std::string &s) {
        static const char *chars = " ._-[]()";
        size_t a = s.find_first_not_of(chars);
        if (a == std::string::npos) {
            return "";
        }
        size_t b = s.find_last_not_of(chars);
        return s.substr(a, b - a + 1);
    }

    // file name part after the video name (".rus", " [LostFilm]"...)
    std::string nameRest(const std::string &fileBase, const std::string &videoBase) {
        if (lower(fileBase).rfind(lower(videoBase), 0) == 0) {
            return trimDecorations(fileBase.substr(videoBase.size()));
        }
        return fileBase;
    }

    std::string guessLanguage(const std::string &text) {
        static const std::pair<const char *, const char *> words[] = {
                {"rus", "rus"}, {"ru", "rus"}, {"russian", "rus"},
                {"eng", "eng"}, {"en", "eng"}, {"english", "eng"},
                {"ukr", "ukr"}, {"ua", "ukr"}, {"ukrainian", "ukr"},
        };
        static const std::pair<const char *, const char *> cyrillic[] = {
                {"рус", "rus"}, {"Рус", "rus"}, {"РУС", "rus"},
                {"англ", "eng"}, {"Англ", "eng"}, {"АНГЛ", "eng"},
                {"укр", "ukr"}, {"Укр", "ukr"}, {"УКР", "ukr"},
        };
        std::string l = lower(text);
        std::string token;
        for (size_t i = 0; i <= l.size(); i++) {
            unsigned char c = i < l.size() ? (unsigned char) l[i] : ' ';
            if (c < 0x80 && std::isalnum(c)) {
                token += (char) c;
                continue;
            }
            for (const auto &word: words) {
                if (token == word.first) {
                    return word.second;
                }
            }
            token.clear();
        }
        for (const auto &word: cyrillic) {
            if (text.find(word.first) != std::string::npos) {
                return word.second;
            }
        }
        return "";
    }

    std::string bindingsPath(const std::string &videoKey) {
        return pplay::Utility::getDataPath() + "tracks/" + pplay::Utility::md5hash(videoKey);
    }

    std::vector<std::string> split(const std::string &s, char sep) {
        std::vector<std::string> out;
        size_t start = 0;
        while (true) {
            size_t pos = s.find(sep, start);
            out.push_back(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
            if (pos == std::string::npos) {
                return out;
            }
            start = pos + 1;
        }
    }
}

namespace pplay::ExternalTracks {

    const std::vector<std::string> &audioExtensions() {
        static const std::vector<std::string> extensions = {
                ".mka", ".ac3", ".eac3", ".dts", ".aac", ".flac", ".mp3", ".ogg", ".opus", ".m4a", ".wav",
                ".thd", ".mlp", ".wv"
        };
        return extensions;
    }

    const std::vector<std::string> &subtitleExtensions() {
        static const std::vector<std::string> extensions = {
                ".srt", ".ass", ".ssa", ".vtt", ".sub", ".sup"
        };
        return extensions;
    }

    bool isAudio(const std::string &name) {
        return endsWithAny(name, audioExtensions());
    }

    bool isSubtitle(const std::string &name) {
        return endsWithAny(name, subtitleExtensions());
    }

    std::vector<Track> find(Io *io, const std::string &videoPath, const std::string &videoName, int timeout) {
        std::vector<Track> tracks;
        const std::string videoBase = baseName(videoName);
        const std::string videoNorm = normalize(videoBase);
        const std::string videoEpisode = episodeKey(videoBase);
        if (videoNorm.size() < 3) {
            return tracks;
        }

        struct Dir {
            std::string path;
            std::string label; // relative to the video directory
            int depth;
        };
        std::deque<Dir> dirs = {{Paths::extractDirPath(videoPath), "", 0}};
        int listings = 0;

        while (!dirs.empty() && listings < FIND_MAX_LISTINGS) {
            Dir dir = dirs.front();
            dirs.pop_front();
            std::string error;
            Io::DeviceType type = io->getDeviceType(dir.path);
            std::vector<c2d::Io::File> files = io->list(type, dir.path, timeout, &error, false, false);
            listings++;

            for (const auto &f: files) {
                if (f.name == "." || f.name == "..") {
                    continue;
                }
                if (f.type == c2d::Io::Type::Directory) {
                    if (dir.depth < FIND_DEPTH) {
                        dirs.push_back({f.path, dir.label.empty() ? f.name : dir.label + " / " + f.name,
                                        dir.depth + 1});
                    }
                    continue;
                }
                bool audio = isAudio(f.name);
                if (!audio && !isSubtitle(f.name)) {
                    continue;
                }
                std::string fileBase = baseName(f.name);
                bool match = normalize(fileBase).rfind(videoNorm, 0) == 0
                             || (!videoEpisode.empty() && episodeKey(fileBase) == videoEpisode);
                if (!match) {
                    continue;
                }
                Track track;
                track.audio = audio;
                track.path = f.path;
                std::string rest = nameRest(fileBase, videoBase);
                if (dir.label.empty()) {
                    track.title = rest.empty() ? f.name : rest;
                } else {
                    track.title = dir.label + (rest.empty() || rest == fileBase ? "" : " / " + rest);
                }
                track.lang = guessLanguage(dir.label + " " + rest);
                tracks.push_back(track);
            }
        }

        Utility::log(Utility::LogLevel::Info, "ExternalTracks: " + std::to_string(tracks.size())
                                              + " file(s) for " + videoName + " (" + std::to_string(listings)
                                              + " listing(s))");
        return tracks;
    }

    std::vector<Track> load(const std::string &videoKey) {
        std::vector<Track> tracks;
        FILE *f = fopen(bindingsPath(videoKey).c_str(), "rb");
        if (!f) {
            return tracks;
        }
        char buf[4096];
        while (fgets(buf, sizeof(buf), f)) {
            std::string line = buf;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            std::vector<std::string> fields = split(line, '\t');
            if (fields.size() < 5 || fields[2].empty()) {
                continue;
            }
            Track track;
            track.audio = fields[0] == "a";
            track.selected = fields[1] == "1";
            track.path = fields[2];
            track.title = fields[3];
            track.lang = fields[4];
            tracks.push_back(track);
        }
        fclose(f);
        return tracks;
    }

    void save(const std::string &videoKey, const std::vector<Track> &tracks) {
        std::string path = bindingsPath(videoKey);
        if (tracks.empty()) {
            Utility::deleteFile(path);
            return;
        }
        std::string dir = Utility::getDataPath() + "tracks";
        if (Io::get() && !Io::get()->exist(dir)) {
            Io::get()->create(dir);
        }
        FILE *f = fopen(path.c_str(), "wb");
        if (!f) {
            Utility::log(Utility::LogLevel::Error, "ExternalTracks: can't write " + path);
            return;
        }
        for (const auto &track: tracks) {
            fprintf(f, "%s\t%d\t%s\t%s\t%s\n", track.audio ? "a" : "s", track.selected ? 1 : 0,
                    track.path.c_str(), track.title.c_str(), track.lang.c_str());
        }
        fclose(f);
    }

    Track fromPath(bool audio, const std::string &path, const std::string &videoPath) {
        Track track;
        track.audio = audio;
        track.path = path;
        std::string name = Paths::getLeafName(path);
        size_t query = name.find('?');
        if (query != std::string::npos) {
            name = name.substr(0, query);
        }
        std::string dir = Paths::extractDirPath(path);
        if (dir == Paths::extractDirPath(videoPath)) {
            track.title = baseName(name);
        } else {
            std::string parent = c2d::Utility::removeLastSlash(dir);
            track.title = Paths::getLeafName(parent) + " / " + baseName(name);
        }
        track.lang = guessLanguage(track.title);
        return track;
    }

    std::string toMpvUrl(const std::string &path) {
        std::string url = TorrServe::toStreamUrl(path);
#ifdef __SMB2__
        if (c2d::Utility::startWith(url, "smb://")) {
            std::replace(url.begin(), url.end(), '\\', '/');
            url.replace(0, strlen("smb://"), "smb2://");
        }
#endif
        return url;
    }

    bool matchLanguage(const std::string &list, const std::string &lang) {
        if (lang.empty()) {
            return false;
        }
        std::string l = lower(lang);
        for (const auto &item: split(lower(list), ',')) {
            if (!item.empty() && item == l) {
                return true;
            }
        }
        return false;
    }
}
