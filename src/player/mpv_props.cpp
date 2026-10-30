//
// Mpv playback controls and properties.
//

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "mpv.h"
#include "utility.h"

int Mpv::save() {
    return mpv_command_string(handle, "write-watch-later-config");
}

int Mpv::pause() {
    return mpv_command_string(handle, "set pause yes");
}

int Mpv::resume() {
    return mpv_command_string(handle, "set pause no");
}

int Mpv::stop() {
    return mpv_command_string(handle, "stop");
}

std::string Mpv::changeBrightness(double delta) {
    std::string cmd = "no-osd add brightness " + std::to_string(delta);
    mpv_command_string(handle, cmd.c_str());
    double brightness = 0.0;
    mpv_get_property(handle, "brightness", MPV_FORMAT_DOUBLE, &brightness);
    return "Brightness: " + std::to_string((int) brightness) + "%";
}

std::string Mpv::changeVolume(double delta) {
    std::string cmd = "no-osd add volume " + std::to_string(delta);
    mpv_command_string(handle, cmd.c_str());
    double volume = 0.0;
    mpv_get_property(handle, "volume", MPV_FORMAT_DOUBLE, &volume);
    saveVolume();
    return "Volume: " + std::to_string((int) volume) + "%";
}

void Mpv::saveVolume() {
    double currentVolume = 100.0;
    mpv_get_property(handle, "volume", MPV_FORMAT_DOUBLE, &currentVolume);
    lastVolume = currentVolume;
}

int Mpv::restoreVolume() {
    if (lastVolume >= 0.0) {
        std::string cmd = "no-osd set volume " + std::to_string((int) lastVolume);
        int res = mpv_command_string(handle, cmd.c_str());
        return res;
    }
    return 0;
}

int Mpv::getOsdWidth() {
    int64_t res = -1;
    mpv_get_property(handle, "osd-width", MPV_FORMAT_INT64, &res);
    return static_cast<int>(res);
}

int Mpv::getOsdHeight() {
    int64_t res = -1;
    mpv_get_property(handle, "osd-height", MPV_FORMAT_INT64, &res);
    return static_cast<int>(res);
}

int Mpv::seek(double position) {
    std::string cmd = "no-osd seek " + std::to_string(position);
    return mpv_command_string(handle, cmd.c_str());
}

std::string Mpv::setSpeed(double speed) {
    std::string cmd = "no-osd set speed " + std::to_string(speed);
    mpv_command_string(handle, cmd.c_str());
    double currentSpeed = getSpeed();
    return "Speed: " + std::to_string((int) (currentSpeed * 100.0)) + "%";
}

double Mpv::getSpeed() {
    double res = -1;
    mpv_get_property(handle, "speed", MPV_FORMAT_DOUBLE, &res);
    return res;
}

void Mpv::saveSpeed() {
    double currentSpeed = 1.0;
    mpv_get_property(handle, "speed", MPV_FORMAT_DOUBLE, &currentSpeed);
    lastSpeed = currentSpeed;
}

int Mpv::restoreSpeed() {
    if (lastSpeed > 1.0) {
        std::string cmd = "no-osd set speed " + std::to_string(lastSpeed);
        return mpv_command_string(handle, cmd.c_str());
    }
    return 0;
}

int Mpv::setVid(int id) {
    std::string cmd = "no-osd set vid ";
    cmd += id < 0 ? "no" : std::to_string(id);
    return mpv_command_string(handle, cmd.c_str());
}

int Mpv::setAid(int id) {
    if (hasVisualizer()) {
        visualAid = id;
        applyVisualizer();
        return 0;
    }
    std::string cmd = "no-osd set aid ";
    cmd += id < 0 ? "no" : std::to_string(id);
    return mpv_command_string(handle, cmd.c_str());
}

int Mpv::setSid(int id) {
    std::string cmd = "no-osd set sid ";
    cmd += id < 0 ? "no" : std::to_string(id);
    return mpv_command_string(handle, cmd.c_str());
}

int Mpv::setPlaylistPos(int id) {
    int64_t pos = id;
    return mpv_set_property(handle, "playlist-pos", MPV_FORMAT_INT64, &pos);
}

int Mpv::getVid() {
    int64_t vid = -1;
    mpv_get_property(handle, "vid", MPV_FORMAT_INT64, &vid);
    return (int) vid;
}

int Mpv::getAid() {
    if (hasVisualizer()) {
        return visualAid;
    }
    int64_t aid = -1;
    mpv_get_property(handle, "aid", MPV_FORMAT_INT64, &aid);
    return (int) aid;
}

int Mpv::getSid() {
    int64_t sid = -1;
    mpv_get_property(handle, "sid", MPV_FORMAT_INT64, &sid);
    return (int) sid;
}

int Mpv::getPlaylistPos() {
    int64_t pos = -1;
    mpv_get_property(handle, "playlist-pos", MPV_FORMAT_INT64, &pos);
    return (int) pos;
}

int Mpv::getPlaylistCount() {
    int64_t count = 0;
    mpv_get_property(handle, "playlist-count", MPV_FORMAT_INT64, &count);
    return (int) count;
}

std::string Mpv::getPlaylistCurrentTitle() {
    char *title = mpv_get_property_string(handle, "media-title");
    std::string res = title ? title : "";
    if (title) {
        mpv_free(title);
    }
    return res;
}

std::string Mpv::getCurrentPath() {
    char *path = mpv_get_property_string(handle, "path");
    std::string res = path ? path : "";
    if (path) {
        mpv_free(path);
    }
    return res;
}

std::vector<std::pair<int, std::string>> Mpv::getPlaylistItems() {
    std::vector<std::pair<int, std::string>> items;
    mpv_node node;
    if (mpv_get_property(handle, "playlist", MPV_FORMAT_NODE, &node) < 0) {
        return items;
    }
    if (node.format == MPV_FORMAT_NODE_ARRAY && node.u.list) {
        for (int i = 0; i < node.u.list->num; i++) {
            const mpv_node &entry = node.u.list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP || !entry.u.list) {
                continue;
            }
            std::string title;
            for (int n = 0; n < entry.u.list->num; n++) {
                std::string key = entry.u.list->keys[n];
                const mpv_node &value = entry.u.list->values[n];
                if (key == "title" && value.format == MPV_FORMAT_STRING && value.u.string) {
                    title = value.u.string;
                } else if (key == "filename" && title.empty() && value.format == MPV_FORMAT_STRING && value.u.string) {
                    title = value.u.string;
                }
            }
            items.emplace_back(i, title.empty() ? ("Item " + std::to_string(i + 1)) : title);
        }
    }
    mpv_free_node_contents(&node);
    return items;
}

int Mpv::getVideoBitrate() {
    double bitrate = 0;
    mpv_get_property(handle, "video-bitrate", MPV_FORMAT_DOUBLE, &bitrate);
    return (int) bitrate;
}

int Mpv::getAudioBitrate() {
    int64_t bitrate = 0;
    mpv_get_property(handle, "audio-bitrate", MPV_FORMAT_INT64, &bitrate);
    return (int) bitrate;
}

long Mpv::getDuration() {
    long duration = 0;
    mpv_get_property(handle, "duration", MPV_FORMAT_INT64, &duration);
    return duration;
}

long Mpv::getPosition() {
    long position = 0;
    mpv_get_property(handle, "playback-time", MPV_FORMAT_INT64, &position);
    return position;
}

double Mpv::getTime() {
    double time = -1;
    if (mpv_get_property(handle, "playback-time", MPV_FORMAT_DOUBLE, &time) < 0) {
        return -1;
    }
    return time;
}

bool Mpv::isPausedForCache() {
    int res = 0;
    mpv_get_property(handle, "paused-for-cache", MPV_FORMAT_FLAG, &res);
    return res == 1;
}

long Mpv::getCacheSpeed() {
    int64_t speed = 0;
    mpv_get_property(handle, "cache-speed", MPV_FORMAT_INT64, &speed);
    return (long) speed;
}

double Mpv::getCacheDuration() {
    double duration = -1;
    if (mpv_get_property(handle, "demuxer-cache-duration", MPV_FORMAT_DOUBLE, &duration) < 0) {
        return -1;
    }
    return duration;
}

long Mpv::getFrameNumber() {
    int64_t frame = -1;
    if (mpv_get_property(handle, "estimated-frame-number", MPV_FORMAT_INT64, &frame) < 0) {
        return -1;
    }
    return (long) frame;
}

bool Mpv::hasMovingVideo() {
    int64_t id = -1;
    if (mpv_get_property(handle, "current-tracks/video/id", MPV_FORMAT_INT64, &id) < 0) {
        return false;
    }
    int albumArt = 0;
    mpv_get_property(handle, "current-tracks/video/albumart", MPV_FORMAT_FLAG, &albumArt);
    return albumArt == 0;
}

bool Mpv::isAvailable() {
    return handle != nullptr;
}

bool Mpv::isStopped() {
    int res = 1;
    mpv_get_property(handle, "playback-abort", MPV_FORMAT_FLAG, &res);
    return res == 1;
}

bool Mpv::isPaused() {
    int res = -1;
    mpv_get_property(handle, "pause", MPV_FORMAT_FLAG, &res);
    return res == 1;
}

mpv_event *Mpv::getEvent() {
    return mpv_wait_event(handle, 0);
}

mpv_handle *Mpv::getHandle() {
    return handle;
}

mpv_render_context *Mpv::getContext() {
    return context;
}

std::string Mpv::getString(const char *name) {
    char *value = nullptr;
    if (!isAvailable() || mpv_get_property(handle, name, MPV_FORMAT_STRING, &value) < 0 || !value) {
        return "";
    }
    std::string out = value;
    mpv_free(value);
    return out;
}

double Mpv::getDouble(const char *name, double fallback) {
    double value = fallback;
    if (!isAvailable() || mpv_get_property(handle, name, MPV_FORMAT_DOUBLE, &value) < 0) {
        return fallback;
    }
    return value;
}

long Mpv::getInt(const char *name, long fallback) {
    int64_t value = fallback;
    if (!isAvailable() || mpv_get_property(handle, name, MPV_FORMAT_INT64, &value) < 0) {
        return fallback;
    }
    return (long) value;
}

std::vector<Mpv::TrackEntry> Mpv::getTrackList() {
    std::vector<TrackEntry> tracks;
    mpv_node node;
    if (!isAvailable() || mpv_get_property(handle, "track-list", MPV_FORMAT_NODE, &node) < 0) {
        return tracks;
    }
    if (node.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < node.u.list->num; i++) {
            const mpv_node &entry = node.u.list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP) {
                continue;
            }
            TrackEntry track;
            for (int n = 0; n < entry.u.list->num; n++) {
                std::string key = entry.u.list->keys[n];
                const mpv_node &value = entry.u.list->values[n];
                if (value.format == MPV_FORMAT_STRING) {
                    if (key == "type") {
                        track.type = value.u.string;
                    } else if (key == "lang") {
                        track.lang = value.u.string;
                    } else if (key == "external-filename") {
                        track.filename = value.u.string;
                    }
                } else if (value.format == MPV_FORMAT_INT64 && key == "id") {
                    track.id = (int) value.u.int64;
                } else if (value.format == MPV_FORMAT_FLAG && key == "external") {
                    track.external = value.u.flag != 0;
                } else if (value.format == MPV_FORMAT_FLAG && key == "selected") {
                    track.selected = value.u.flag != 0;
                }
            }
            tracks.push_back(track);
        }
    }
    mpv_free_node_contents(&node);
    return tracks;
}

MediaInfo Mpv::getMediaInfo(const c2d::Io::File &file) {
    MediaInfo mediaInfo(file);
    if (!isAvailable() || isStopped()) {
        return mediaInfo;
    }

    mpv_node node;
    if (mpv_get_property(handle, "track-list", MPV_FORMAT_NODE, &node) < 0) {
        return mediaInfo;
    }
    mediaInfo.videos.clear();
    mediaInfo.audios.clear();
    mediaInfo.subtitles.clear();
    if (node.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < node.u.list->num; i++) {
            const mpv_node &entry = node.u.list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP) {
                continue;
            }
            MediaInfo::Track t;
            for (int n = 0; n < entry.u.list->num; n++) {
                const std::string key = entry.u.list->keys[n];
                const mpv_node &v = entry.u.list->values[n];
                std::string str = v.format == MPV_FORMAT_STRING ? v.u.string : "";
                long num = v.format == MPV_FORMAT_INT64 ? (long) v.u.int64 : 0;
                bool flag = v.format == MPV_FORMAT_FLAG && v.u.flag;
                if (key == "type") {
                    t.type = str;
                } else if (key == "id") {
                    t.id = (int) num;
                } else if (key == "title") {
                    t.title = str;
                } else if (key == "lang") {
                    t.language = str;
                } else if (key == "codec") {
                    t.codec = str;
                } else if (key == "demux-w") {
                    t.width = (int) num;
                } else if (key == "demux-h") {
                    t.height = (int) num;
                } else if (key == "demux-fps" && v.format == MPV_FORMAT_DOUBLE) {
                    t.fps = v.u.double_;
                } else if (key == "demux-samplerate") {
                    t.sample_rate = (int) num;
                } else if (key == "demux-channel-count") {
                    t.channels = (int) num;
                } else if (key == "demux-channels") {
                    t.layout = str;
                } else if (key == "demux-bitrate") {
                    t.bit_rate = num;
                } else if (key == "default") {
                    t.isDefault = flag;
                } else if (key == "forced") {
                    t.forced = flag;
                } else if (key == "albumart") {
                    t.albumart = flag;
                } else if (key == "external") {
                    t.external = flag;
                }
            }
            auto &list = t.type == "video"   ? mediaInfo.videos
                         : t.type == "audio" ? mediaInfo.audios
                                             : mediaInfo.subtitles;
            if (t.type == "video" || t.type == "audio" || t.type == "sub") {
                list.push_back(t);
            }
        }
    }
    mpv_free_node_contents(&node);

    mediaInfo.duration = getDuration();
    mediaInfo.format = getString("file-format");
    mediaInfo.title = getString("metadata/by-key/title");
    return mediaInfo;
}

void Mpv::setVisualizer(const std::string &graph) {
    if (graph == visualGraph) {
        return;
    }
    if (graph.empty()) {
        // the track back to the normal output
        visualGraph.clear();
        mpv_set_property_string(handle, "lavfi-complex", "");
        setAid(visualAid);
        return;
    }
    if (visualGraph.empty()) {
        visualAid = getAid();
    }
    visualGraph = graph;
    applyVisualizer();
}

void Mpv::applyVisualizer() {
    std::string complex;
    if (visualAid > 0) {
        std::string track = "[aid" + std::to_string(visualAid) + "]";
        complex = track + "asplit[ao][visual];[visual]" + visualGraph + "[vo]";
    }
    int res = mpv_set_property_string(handle, "lavfi-complex", complex.c_str());
    if (visualAid <= 0) {
        mpv_set_property_string(handle, "aid", "no");
    }
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Mpv::applyVisualizer: aid=" + std::to_string(visualAid)
                                                                + (res < 0 ? std::string(": ") + mpv_error_string(res) : ""));
}

void Mpv::setFrameLayout(float left, float top, float right, float bottom) {
    const char *names[] = {"video-margin-ratio-left", "video-margin-ratio-top", "video-margin-ratio-right",
                           "video-margin-ratio-bottom"};
    float values[] = {left, top, right, bottom};
    for (int i = 0; i < 4; i++) {
        double value = values[i];
        mpv_set_property(handle, names[i], MPV_FORMAT_DOUBLE, &value);
    }
}
