//
// Mpv playback controls and properties.
//

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "mpv.h"
#include "utility.h"

namespace {
    const uint64_t WatchReply = 0x5741544348000000ULL;
    const uint64_t SaveReply = 0x5341564500000000ULL;

    // read every frame: sent by mpv on changes
    const std::pair<const char *, mpv_format> watched[] = {
            {"playback-abort", MPV_FORMAT_FLAG},
            {"pause", MPV_FORMAT_FLAG},
            {"eof-reached", MPV_FORMAT_FLAG},
            {"idle-active", MPV_FORMAT_FLAG},
            {"paused-for-cache", MPV_FORMAT_FLAG},
            {"seeking", MPV_FORMAT_FLAG},
            {"playback-time", MPV_FORMAT_DOUBLE},
            {"duration", MPV_FORMAT_DOUBLE},
            {"speed", MPV_FORMAT_DOUBLE},
            {"demuxer-cache-duration", MPV_FORMAT_DOUBLE},
            {"cache-speed", MPV_FORMAT_INT64},
            {"current-tracks/video/id", MPV_FORMAT_INT64},
            {"current-tracks/video/albumart", MPV_FORMAT_FLAG},
            {"frame-drop-count", MPV_FORMAT_INT64},
            {"decoder-frame-drop-count", MPV_FORMAT_INT64},
            {"estimated-frame-number", MPV_FORMAT_INT64},
    };

    double valueOf(mpv_format format, void *data) {
        switch (format) {
            case MPV_FORMAT_FLAG:
                return *(int *) data;
            case MPV_FORMAT_INT64:
                return (double) *(int64_t *) data;
            case MPV_FORMAT_DOUBLE:
                return *(double *) data;
            default:
                return 0;
        }
    }
}

void Mpv::watchProperties() {
    mpv_observe_property(handle, WatchReply, "current-tracks/sub/codec", MPV_FORMAT_STRING);
    for (auto &p: watched) {
        if (mpv_observe_property(handle, WatchReply, p.first, p.second) >= 0) {
            props[p.first].watched = true;
        }
    }
}

void Mpv::refresh() {
    // Keep the 500 ms reconciliation window required by the information/seek
    // overlays, but never wait for mpv's busy core on the rendering thread.
    ++refreshGeneration;
    refreshUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    for (auto &entry: props) {
        entry.second.snapshotKnown = false;
        entry.second.nextPoll = {};
    }
}

void Mpv::pollProperties() {
    if (!isAvailable() || isSeekBusy()) return;
    const auto now = std::chrono::steady_clock::now();
    for (const auto &watchedProperty: watched) {
        auto &p = props[watchedProperty.first];
        if (!p.watched || p.pending || now < p.nextPoll || (p.known && now >= refreshUntil)) continue;
        // Dedicated ids cannot collide with observation, commands or external-track replies.
        const uint64_t id = 0x50524F5000000000ULL + ++propertyQueryId;
        if (mpv_get_property_async(handle, id, watchedProperty.first, watchedProperty.second) >= 0) {
            p.pending = id;
            propertyQueries.emplace(id, PropertyQuery{watchedProperty.first, refreshGeneration, p.version});
        }
        p.nextPoll = now + std::chrono::milliseconds(50);
    }
}

bool Mpv::propertyReply(mpv_event *event) {
    if (event->event_id != MPV_EVENT_GET_PROPERTY_REPLY) return false;
    auto it = propertyQueries.find(event->reply_userdata);
    if (it == propertyQueries.end()) return false;
    const auto query = it->second;
    propertyQueries.erase(it);
    auto &p = props[query.name];
    if (p.pending == event->reply_userdata) p.pending = 0;
    // A late query must not overwrite a new file, a seek, a command or a
    // newer observation. Poll snapshots are separate from the observer cache:
    // mpv sends changes relative to its last notification, not our last query.
    if (query.generation != refreshGeneration || query.version != p.version) return true;
    auto *reply = static_cast<mpv_event_property *>(event->data);
    p.snapshotOk = event->error >= 0 && reply && reply->format != MPV_FORMAT_NONE && reply->data;
    p.snapshotKnown = true;
    if (p.snapshotOk) p.snapshot = valueOf(reply->format, reply->data);
    return true;
}

bool Mpv::isSeekBusy() {
    // (its end missed: not for long)
    seekBusy &= std::chrono::steady_clock::now() - seekAt < std::chrono::seconds(3);
    return seekBusy;
}

void Mpv::startSeek() {
    seekBusy = true;
    seekAt = std::chrono::steady_clock::now();
    refresh();
}

bool Mpv::read(const char *name, mpv_format format, double *value) {
    Prop &p = props[name];
    auto now = std::chrono::steady_clock::now();
    if (p.watched) {
        // Unknown observed values are requested asynchronously too. No first-read
        // synchronous fallback during file loading/decoder reconfiguration.
        pollProperties();
        if (p.snapshotKnown && now < refreshUntil) {
            *value = p.snapshotOk ? p.snapshot : p.value;
            return p.snapshotOk;
        }
        *value = p.value;
        return p.known && p.ok;
    }
    if (p.known && (isSeekBusy() || (saving && now - p.read < std::chrono::seconds(1)))) {
        *value = p.value;
        return p.ok;
    }
    union {
        int flag;
        int64_t i;
        double d;
    } data {};
    bool ok = isAvailable() && mpv_get_property(handle, name, format, &data) >= 0;
    double read = ok ? valueOf(format, &data) : 0;
    // watched: the value only from the changes (mpv sends one against its last sent value, not this one)
    if (!p.watched) {
        p.ok = ok;
        if (ok) {
            p.value = read;
        }
        p.known = true;
        p.read = now;
    }
    *value = ok ? read : p.value;
    return ok;
}

int Mpv::save(bool wait) {
    int res;
    if (!wait) {
        const char *cmd[] = {"write-watch-later-config", nullptr};
        res = mpv_command_async(handle, SaveReply, cmd);
        saving = res >= 0;
    } else {
        res = mpv_command_string(handle, "write-watch-later-config");
    }
    // written by mpv: read again (async: the cached reads expire in a few seconds anyway)
    pplay::Utility::forgetWatchLater();
    return res;
}

// a state just set: known at once (its change comes later; reading mpv meanwhile waits for its
// core, busy with a seek...)
void Mpv::setKnown(const char *name, double value) {
    Prop &p = props[name];
    p.value = value;
    p.ok = true;
    p.known = true;
    ++p.version;
    p.snapshotKnown = false;
}

int Mpv::pause() {
    const char *cmd[] = {"set", "pause", "yes", nullptr};
    int res = mpv_command_async(handle, 0, cmd);
    if (res >= 0) {
        setKnown("pause", 1);
        refresh();
    }
    return res;
}

int Mpv::resume() {
    const char *cmd[] = {"set", "pause", "no", nullptr};
    int res = mpv_command_async(handle, 0, cmd);
    if (res >= 0) {
        setKnown("pause", 0);
        refresh();
    }
    return res;
}

int Mpv::stop() {
    const char *cmd[] = {"stop", nullptr};
    int res = mpv_command_async(handle, 0, cmd);
    refresh();
    return res;
}

std::string Mpv::changeBrightness(double delta) {
    std::string cmd = "no-osd add brightness " + std::to_string(delta);
    mpv_command_string(handle, cmd.c_str());
    double brightness = 0.0;
    mpv_get_property(handle, "brightness", MPV_FORMAT_DOUBLE, &brightness);
    return std::to_string((int) brightness) + "%";
}

std::string Mpv::changeVolume(double delta) {
    std::string cmd = "no-osd add volume " + std::to_string(delta);
    mpv_command_string(handle, cmd.c_str());
    double volume = 0.0;
    mpv_get_property(handle, "volume", MPV_FORMAT_DOUBLE, &volume);
    saveVolume();
    return std::to_string((int) volume) + "%";
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
    return (int) getInt("osd-width", -1);
}

int Mpv::getOsdHeight() {
    return (int) getInt("osd-height", -1);
}

int Mpv::seek(double position) {
    std::string cmd = "no-osd seek " + std::to_string(position);
    int res = mpv_command_string(handle, cmd.c_str());
    if (res >= 0) {
        startSeek();
    }
    // paused: a hardware decoder gives the new frame only with more input (MediaCodec...), one
    // frame stepped to show it
    std::string hwdec = getString("hwdec-current");
    if (res >= 0 && isPaused() && !hwdec.empty() && hwdec != "no") {
        mpv_command_string(handle, "no-osd frame-step");
    }
    return res;
}

std::string Mpv::setSpeed(double speed) {
    std::string cmd = "no-osd set speed " + std::to_string(speed);
    mpv_command_string(handle, cmd.c_str());
    refresh();
    double currentSpeed = getSpeed();
    lastSpeed = currentSpeed;
    return std::to_string((int) (currentSpeed * 100.0)) + "%";
}

double Mpv::getSpeed() {
    return getDouble("speed", -1);
}

int Mpv::restoreSpeed() {
    // 1 until set (an old watch later file may hold another one)
    std::string cmd = "no-osd set speed " + std::to_string(lastSpeed > 0.0 ? lastSpeed : 1.0);
    return mpv_command_string(handle, cmd.c_str());
}

int Mpv::setVid(int id) {
    std::string cmd = "no-osd set vid ";
    cmd += id < 0 ? "no" : std::to_string(id);
    return mpv_command_string(handle, cmd.c_str());
}

int Mpv::setAid(int id) {
    if (hasVisualizer() && id > 0) {
        visualAid = id;
        applyVisualizer();
        return 0;
    }
    if (hasVisualizer()) {
        // no audio: nothing to show
        visualGraph.clear();
        mpv_set_property_string(handle, "lavfi-complex", "");
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
    return (int) getInt("vid", -1);
}

int Mpv::getAid() {
    if (hasVisualizer()) {
        return visualAid;
    }
    return (int) getInt("aid", -1);
}

int Mpv::getSid() {
    return (int) getInt("sid", -1);
}

int Mpv::getPlaylistPos() {
    return (int) getInt("playlist-pos", -1);
}

std::string Mpv::getPlaylistCurrentTitle() {
    char *title = mpv_get_property_string(handle, "media-title");
    std::string res = title ? title : "";
    if (title) {
        mpv_free(title);
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

// whole seconds as mpv gives them (truncated)
long Mpv::getDuration() {
    double duration = 0;
    return read("duration", MPV_FORMAT_DOUBLE, &duration) ? (long) (int64_t) duration : 0;
}

long Mpv::getPosition() {
    double position = 0;
    return read("playback-time", MPV_FORMAT_DOUBLE, &position) ? (long) (int64_t) position : 0;
}

double Mpv::getTime() {
    return getDouble("playback-time", -1);
}

int Mpv::seekTo(double time, bool exact) {
    std::string cmd = "no-osd seek " + std::to_string(time) + (exact ? " absolute+exact" : " absolute+keyframes");
    int res = mpv_command_string(handle, cmd.c_str());
    // (no reads from mpv while it seeks: they wait for it, frames lost)
    if (res >= 0) {
        startSeek();
    }
    // paused: a hardware decoder gives the new frame only with more input, one frame stepped
    std::string hwdec = getString("hwdec-current");
    if (res >= 0 && isPaused() && !hwdec.empty() && hwdec != "no") {
        mpv_command_string(handle, "no-osd frame-step");
    }
    return res;
}

bool Mpv::isSeeking() {
    double res = 0;
    return read("seeking", MPV_FORMAT_FLAG, &res) && res != 0;
}

bool Mpv::isPausedForCache() {
    double res = 0;
    return read("paused-for-cache", MPV_FORMAT_FLAG, &res) && res != 0;
}

long Mpv::getCacheSpeed() {
    return getInt("cache-speed", 0);
}

double Mpv::getCacheDuration() {
    return getDouble("demuxer-cache-duration", -1);
}

bool Mpv::getCacheEnd(double *end, bool *eof) {
    if (isSeekBusy()) {
        *end = cacheEnd;
        *eof = cacheEof;
        return cacheOk;
    }
    mpv_node node;
    if (!isAvailable() || mpv_get_property(handle, "demuxer-cache-state", MPV_FORMAT_NODE, &node) < 0) {
        cacheOk = false;
        return false;
    }
    bool found = false;
    *eof = false;
    if (node.format == MPV_FORMAT_NODE_MAP && node.u.list != nullptr) {
        for (int i = 0; i < node.u.list->num; i++) {
            const std::string key = node.u.list->keys[i];
            const mpv_node &value = node.u.list->values[i];
            if (key == "cache-end" && value.format == MPV_FORMAT_DOUBLE) {
                *end = value.u.double_;
                found = true;
            } else if (key == "eof" && value.format == MPV_FORMAT_FLAG) {
                *eof = value.u.flag != 0;
            }
        }
    }
    mpv_free_node_contents(&node);
    cacheOk = found;
    cacheEnd = *end;
    cacheEof = *eof;
    return found;
}

long Mpv::getFrameNumber() {
    return getInt("estimated-frame-number", -1);
}

bool Mpv::hasMovingVideo() {
    double id = -1, albumArt = 0;
    if (!read("current-tracks/video/id", MPV_FORMAT_INT64, &id)) {
        return false;
    }
    return !read("current-tracks/video/albumart", MPV_FORMAT_FLAG, &albumArt) || albumArt == 0;
}

bool Mpv::isAvailable() {
    return handle != nullptr;
}

bool Mpv::isStopped() {
    double res = 1;
    return !read("idle-active", MPV_FORMAT_FLAG, &res) || res != 0;
}

bool Mpv::isAtEnd() {
    double res = 0;
    return read("eof-reached", MPV_FORMAT_FLAG, &res) && res != 0;
}

void Mpv::setKeepOpen(bool enabled) {
    if (keepOpen == (int) enabled || !isAvailable()) return;
    const char *cmd[] = {"set", "keep-open", enabled ? "yes" : "no", nullptr};
    if (mpv_command_async(handle, 0, cmd) >= 0) keepOpen = (int) enabled;
}

bool Mpv::isPaused() {
    double res = 0;
    return read("pause", MPV_FORMAT_FLAG, &res) && res != 0;
}

mpv_event *Mpv::getEvent() {
    pollProperties();
    while (true) {
        mpv_event *event = mpv_wait_event(handle, 0);
        if (propertyReply(event)) continue;
        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE && event->reply_userdata == WatchReply) {
            auto *change = (mpv_event_property *) event->data;
            if (std::string(change->name) == "current-tracks/sub/codec") {
                const std::string codec = change->format == MPV_FORMAT_STRING && change->data && *(char **) change->data
                                          ? *(char **) change->data : "";
                assSubtitle = codec == "ass" || codec == "ssa" || codec == "ASS" || codec == "SSA";
                continue;
            }
            Prop &p = props[change->name];
            ++p.version;
            p.snapshotKnown = false;
            p.ok = change->format != MPV_FORMAT_NONE && change->data != nullptr;
            if (p.ok) {
                p.value = valueOf(change->format, change->data);
            }
            p.known = true;
            continue;
        }
        // Seed lifecycle state immediately. Property-change notifications can
        // follow FILE_LOADED/END_FILE, but their handlers need the new state now.
        if (event->event_id == MPV_EVENT_START_FILE || event->event_id == MPV_EVENT_FILE_LOADED) {
            setKnown("idle-active", 0);
            setKnown("eof-reached", 0);
        } else if (event->event_id == MPV_EVENT_END_FILE || event->event_id == MPV_EVENT_IDLE) {
            setKnown("idle-active", 1);
            setKnown("eof-reached", 0);
        }
        switch (event->event_id) {
            case MPV_EVENT_COMMAND_REPLY:
                if (event->reply_userdata == SaveReply) {
                    saving = false;
                }
                break;
            // a new state: read once (the changes may come after it)
            // (a seek: once done, not while mpv seeks)
            case MPV_EVENT_SEEK:
                startSeek();
                break;
            case MPV_EVENT_START_FILE:
            case MPV_EVENT_FILE_LOADED:
            case MPV_EVENT_END_FILE:
            case MPV_EVENT_IDLE:
            case MPV_EVENT_PLAYBACK_RESTART:
            case MPV_EVENT_VIDEO_RECONFIG:
            case MPV_EVENT_AUDIO_RECONFIG:
                seekBusy &= event->event_id != MPV_EVENT_PLAYBACK_RESTART && event->event_id != MPV_EVENT_END_FILE
                            && event->event_id != MPV_EVENT_IDLE;
                refresh();
                break;
            default:
                break;
        }
        return event;
    }
}

mpv_handle *Mpv::getHandle() {
    return handle;
}

mpv_render_context *Mpv::getContext() {
    return context;
}

std::string Mpv::getString(const char *name) {
    if (isSeekBusy()) {
        auto known = strings.find(name);
        if (known != strings.end()) {
            return known->second;
        }
    }
    char *value = nullptr;
    if (!isAvailable() || mpv_get_property(handle, name, MPV_FORMAT_STRING, &value) < 0 || !value) {
        strings[name] = "";
        return "";
    }
    std::string out = value;
    mpv_free(value);
    strings[name] = out;
    return out;
}

double Mpv::getDouble(const char *name, double fallback) {
    double value = fallback;
    return read(name, MPV_FORMAT_DOUBLE, &value) ? value : fallback;
}

long Mpv::getInt(const char *name, long fallback) {
    double value = (double) fallback;
    // int64_t: a long is 32 bits on armv7
    return read(name, MPV_FORMAT_INT64, &value) ? (long) (int64_t) value : fallback;
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
    mediaInfo.title = c2d::Utility::trim(getString("metadata/by-key/title"));
    mediaInfo.artist = c2d::Utility::trim(getString("metadata/by-key/artist"));
    mediaInfo.album = c2d::Utility::trim(getString("metadata/by-key/album"));
    mediaInfo.date = getString("metadata/by-key/date");
    if (mediaInfo.date.empty()) {
        mediaInfo.date = getString("metadata/by-key/year");
    }
    mediaInfo.genre = getString("metadata/by-key/genre");
    return mediaInfo;
}

void Mpv::setVisualizer(const std::string &graph, int aid) {
    if (graph.empty()) {
        if (!visualGraph.empty()) {
            visualGraph.clear();
            mpv_set_property_string(handle, "lavfi-complex", "");
            // the track back to the normal output, never "no"
            mpv_set_property_string(handle, "aid", aid > 0 ? std::to_string(aid).c_str() : "auto");
        }
        return;
    }
    if (graph == visualGraph && aid == visualAid) {
        return;
    }
    visualGraph = graph;
    visualAid = aid;
    applyVisualizer();
}

void Mpv::applyVisualizer() {
    std::string complex = "[aid" + std::to_string(visualAid) + "]asplit[ao][visual];[visual]" + visualGraph + "[vo]";
    int res = mpv_set_property_string(handle, "lavfi-complex", complex.c_str());
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
