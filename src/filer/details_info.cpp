//
// Details panel texts.
//

#include <ctime>

#include "cross2d/c2d.h"
#include "encodings.h"
#include "filer.h"
#include "history.h"
#include "i18n.h"
#include "player.h"
#include "details_info.h"
#include "text_wrap.h"
#include "torrserve.h"
#include "utility.h"

using pplay::TextWrap::noBreak;

namespace {
    std::string resolutionName(int width, int height) {
        if (width >= 3800) {
            return "2160p";
        }
        if (width >= 1900) {
            return "1080p";
        }
        if (width >= 1260) {
            return "720p";
        }
        return std::to_string(height) + "p";
    }

    std::string line(const std::string &name, const std::string &value) {
        return pplay::tr(name) + ": " + value + "\n";
    }

    // a line, or a title and a line per track
    std::string trackLines(const char *name, const std::vector<MediaInfo::Track> &tracks) {
        if (tracks.empty()) {
            return "";
        }
        if (tracks.size() == 1) {
            return line(name, encoding::fix(MediaInfo::describe(tracks[0])));
        }
        std::string out = pplay::tr(name) + " (" + std::to_string(tracks.size()) + "):\n";
        for (auto &track: tracks) {
            out += noBreak("   ") + encoding::fix(MediaInfo::describe(track)) + "\n";
        }
        return out;
    }

    // the sections separated by an empty line
    std::string mediaLines(const MediaInfo &mi, size_t size) {
        std::string general;
        if (mi.duration > 0) {
            general += line("Duration", pplay::Utility::formatTime((double) mi.duration));
        }
        if (size > 0) {
            general += line("Size", noBreak(pplay::Utility::formatSize(size)));
            if (mi.duration > 0) {
                general += line("Bitrate", noBreak(MediaInfo::formatBitrate((long) (size * 8 / mi.duration))));
            }
        }
        if (!mi.format.empty()) {
            general += line("Format", mi.format);
        }
        // the cover is shown, not listed
        std::vector<MediaInfo::Track> videos;
        for (auto &v: mi.videos) {
            if (!v.albumart) {
                videos.push_back(v);
            }
        }
        std::string video;
        if (videos.size() == 1) {
            const auto &v = videos[0];
            std::string name = v.width > 0 && v.height > 0 ? resolutionName(v.width, v.height) + ", " : "";
            video = line("Video", name + encoding::fix(MediaInfo::describe(v)));
        } else {
            video = trackLines("Video", videos);
        }
        std::string out;
        for (auto &section: {general, video, trackLines("Audio", mi.audios), trackLines("Subtitles", mi.subtitles)}) {
            if (!section.empty()) {
                out += (out.empty() ? "" : "\n") + section;
            }
        }
        return out;
    }

    // the metadata title, after the file name
    std::string titled(const std::string &name, const MediaInfo &mi) {
        return mi.title.empty() ? name : name + " \u00AB" + encoding::fix(mi.title) + "\u00BB";
    }

    // smb:// is opened by mpv only
    std::string coverOf(const MediaFile &file) {
        std::string url = pplay::TorrServe::toStreamUrl(file.path);
        if (!file.mediaInfo.hasCover() || c2d::Utility::startWith(url, "smb")) {
            return "";
        }
        return "embedded:" + url;
    }

    std::string lastPlayed(const MediaFile &file) {
        time_t last = pplay::History::lastPlayed(Player::keyOf(file.path));
        if (last <= 0) {
            return "";
        }
        struct tm tm {};
        localtime_r(&last, &tm);
        char date[32];
        strftime(date, sizeof(date), "%d.%m.%Y %H:%M", &tm);
        return line("Last played", date);
    }
}

pplay::DetailsInfo pplay::DetailsInfo::of(const MediaFile &file) {
    DetailsInfo info;
    if (Filer::isPlaceholder(file)) {
        return info;
    }
    if (file.name == "..") {
        info.title = tr("BACK");
        info.details = tr("Parent directory");
        return info;
    }

    std::string text;
    TorrServe::Info ts;
    if (TorrServe::getInfo(file.path, ts)) {
        info.title = encoding::fix(ts.title);
        info.poster = ts.poster;
        std::string files = tr("Files") + ": " + std::to_string(ts.fileCount);
        if (ts.filesSize > 0) {
            files += " (" + Utility::formatSize((size_t) ts.filesSize) + ")";
        }
        if (!ts.category.empty()) {
            text += line("Category", encoding::fix(ts.category));
        }
        text += noBreak(files) + "\n";
        if (ts.fileIndex >= 0) {
            text += "\n" + pplay::TextWrap::flat(titled(encoding::fix(ts.fileName), file.mediaInfo)) + "\n\n";
            text += mediaLines(file.mediaInfo, ts.fileSize > 0 ? (size_t) ts.fileSize : 0);
        }
    } else {
        info.title = encoding::fix(file.name);
        if (file.type == c2d::Io::Type::Directory) {
            text += tr("Directory") + "\n";
        } else {
            if (!file.mediaInfo.title.empty()) {
                text += pplay::TextWrap::flat("\u00AB" + encoding::fix(file.mediaInfo.title) + "\u00BB") + "\n\n";
            }
            text += mediaLines(file.mediaInfo, file.size);
        }
    }
    if (file.type == c2d::Io::Type::File) {
        info.cover = coverOf(file);
        std::string last = lastPlayed(file);
        if (!last.empty()) {
            text += (text.empty() ? "" : "\n") + last;
        }
    }
    while (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    info.details = text;
    return info;
}
