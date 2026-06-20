//
// Created by cpasjuste on 02/04/19.
//

#include <SDL_video.h>
#include <cmath>
#include "mpv.h"
#include "utility.h"
#include "io.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <vector>
#include <string>
#include <sstream>

static std::vector<std::string> get_ffmpeg_protocols(bool output) {
    std::vector<std::string> result;

    void *opaque = nullptr;
    const char *name = nullptr;

    while ((name = avio_enum_protocols(&opaque, output))) {
        if (name) {
            result.emplace_back(name);
        }
    }

    return result;
}

static std::string join_protocols(const std::vector<std::string> &protos) {
    std::ostringstream oss;
    for (size_t i = 0; i < protos.size(); ++i) {
        if (i) oss << ' ';
        oss << protos[i];
    }
    return oss.str();
}

static std::string mpv_status(int res) {
    return res >= 0 ? "ok" : std::string("err(") + mpv_error_string(res) + ")";
}

static std::string mpv_ptr(const void *p) {
    std::ostringstream oss;
    oss << p;
    return oss.str();
}

static std::string mpv_quote(const std::string &s) {
    return "\"" + s + "\"";
}

static std::string join_command_args(const char *const *args) {
    if (!args) {
        return "<null>";
    }

    std::ostringstream oss;
    bool first = true;
    for (size_t i = 0; args[i]; ++i) {
        if (!first) {
            oss << ' ';
        }
        first = false;
        oss << args[i];
    }
    return oss.str();
}

static void log_mpv_debug(const std::string &msg) {
    pplay::Utility::log(pplay::Utility::LogLevel::Trace, msg);
}

static mpv_handle *logged_mpv_create() {
    log_mpv_debug("mpv_create()");
    mpv_handle *h = mpv_create();
    log_mpv_debug(std::string("mpv_create() -> ") + mpv_ptr(h));
    return h;
}

static int logged_mpv_set_option_string(mpv_handle *handle, const char *name, const char *value) {
    std::string msg = "mpv_set_option_string(" + std::string(name ? name : "<null>") + ", " +
                      mpv_quote(value ? value : "") + ")";
    log_mpv_debug(msg);
    int res = mpv_set_option_string(handle, name, value);
    log_mpv_debug(msg + " -> " + mpv_status(res));
    return res;
}

static int logged_mpv_initialize(mpv_handle *handle) {
    log_mpv_debug("mpv_initialize()");
    int res = mpv_initialize(handle);
    log_mpv_debug(std::string("mpv_initialize() -> ") + mpv_status(res));
    return res;
}

static int logged_mpv_command(mpv_handle *handle, const char *const *args) {
    std::string cmd = join_command_args(args);
    log_mpv_debug("mpv_command(" + cmd + ")");
    int res = mpv_command(handle, const_cast<const char **>(args));
    log_mpv_debug("mpv_command(" + cmd + ") -> " + mpv_status(res));
    return res;
}

static int logged_mpv_command_string(mpv_handle *handle, const char *cmd) {
    std::string command = cmd ? cmd : "";
    log_mpv_debug("mpv_command_string(" + mpv_quote(command) + ")");
    int res = mpv_command_string(handle, cmd);
    log_mpv_debug("mpv_command_string(" + mpv_quote(command) + ") -> " + mpv_status(res));
    return res;
}

template <typename T>
static int logged_mpv_get_property(mpv_handle *handle, const char *name, mpv_format format, T *data) {
    std::string prop = name ? name : "<null>";
    log_mpv_debug("mpv_get_property(" + prop + ")");
    int res = mpv_get_property(handle, name, format, data);
    log_mpv_debug("mpv_get_property(" + prop + ") -> " + mpv_status(res));
    return res;
}

static char *logged_mpv_get_property_string(mpv_handle *handle, const char *name) {
    std::string prop = name ? name : "<null>";
    log_mpv_debug("mpv_get_property_string(" + prop + ")");
    char *res = mpv_get_property_string(handle, name);
    log_mpv_debug(std::string("mpv_get_property_string(") + prop + ") -> " + (res ? res : "<null>"));
    return res;
}

static int logged_mpv_render_context_create(mpv_render_context **ctx, mpv_handle *handle, mpv_render_param *params) {
    log_mpv_debug("mpv_render_context_create()");
    int res = mpv_render_context_create(ctx, handle, params);
    log_mpv_debug(std::string("mpv_render_context_create() -> ") + mpv_status(res));
    return res;
}

static mpv_event *logged_mpv_wait_event(mpv_handle *handle, double timeout) {
    log_mpv_debug("mpv_wait_event(timeout=" + std::to_string(timeout) + ")");
    mpv_event *ev = mpv_wait_event(handle, timeout);
    if (ev) {
        log_mpv_debug("mpv_wait_event() -> event_id=" + std::to_string((int)ev->event_id));
    } else {
        log_mpv_debug("mpv_wait_event() -> <null>");
    }
    return ev;
}

#ifdef __PS4__
extern "C" int ps4_mpv_use_precompiled_shaders;
extern "C" int ps4_mpv_dump_shaders;
#endif

static void *get_proc_address_mpv(void *unused, const char *name) {
    return SDL_GL_GetProcAddress(name);
}

Mpv::Mpv(const std::string &configPath, bool initRender) {
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Mpv::Mpv: configPath=" + configPath);
#ifdef __PS4__
    ps4_mpv_use_precompiled_shaders = 1;
    ps4_mpv_dump_shaders = 0;
#endif

    handle = logged_mpv_create();
    if (!handle) {
        pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Mpv::Mpv: error: mpv_create");
        return;
    }

    logged_mpv_set_option_string(handle, "config", "yes");
    logged_mpv_set_option_string(handle, "config-dir", configPath.c_str());
    logged_mpv_set_option_string(handle, "osd-scale", "0.5");
#ifndef NDEBUG
    logged_mpv_set_option_string(handle, "terminal", "yes");
    logged_mpv_set_option_string(handle, "msg-level", "all=v");
#endif

#ifdef __SWITCH__
    logged_mpv_set_option_string(handle, "vd-lavc-threads", "4");
    logged_mpv_set_option_string(handle, "dither", "no");
    // TODO: test this
    // logged_mpv_set_option_string(handle, "fbo-format", "rgba8");
    // logged_mpv_set_option_string(handle, "opengl-pbo", "yes");
#else
    logged_mpv_set_option_string(handle, "vd-lavc-threads", "6");
    logged_mpv_set_option_string(handle, "video-sync", "audio");
#endif

    logged_mpv_set_option_string(handle, "audio-channels", "stereo");
    logged_mpv_set_option_string(handle, "audio-normalize-downmix", "yes");
    logged_mpv_set_option_string(handle, "cache-pause", "yes");
    logged_mpv_set_option_string(handle, "cache-secs", "60");

#if defined(__PS4__) || defined(__PS5__)
    logged_mpv_set_option_string(handle, "dither", "no");
    logged_mpv_set_option_string(handle, "ignore-path-in-watch-later-config", "yes");
    logged_mpv_set_option_string(handle, "tls-ca-file", pplay::Utility::getCertificatesPath().c_str());
    logged_mpv_set_option_string(handle, "sub-ass-override", "force");
    logged_mpv_set_option_string(handle, "osd-rendering-mode", "normal");
    logged_mpv_set_option_string(handle, "sub-font-provider", "none");
    logged_mpv_set_option_string(handle, "osd-font-provider", "none");
    logged_mpv_set_option_string(handle, "sub-font", "subfont.ttf");
    logged_mpv_set_option_string(handle, "osd-font", "subfont.ttf");
#endif

#ifdef __PS4__
    logged_mpv_set_option_string(handle, "cscale", "bilinear");
    logged_mpv_set_option_string(handle, "scale", "bilinear");
    logged_mpv_set_option_string(handle, "dscale", "bilinear");
    logged_mpv_set_option_string(handle, "sws-scaler", "bilinear");
    logged_mpv_set_option_string(handle, "sub-fonts-dir", "/data/pplay/mpv");
    logged_mpv_set_option_string(handle, "osd-fonts-dir", "/data/pplay/mpv");
#endif

#ifdef __PS5__
    logged_mpv_set_option_string(handle, "cscale", "bilinear");
    logged_mpv_set_option_string(handle, "scale", "bicubic");
    logged_mpv_set_option_string(handle, "dscale", "bicubic");
    logged_mpv_set_option_string(handle, "sws-scaler", "bicubic");
    logged_mpv_set_option_string(handle, "sub-fonts-dir", "/data/homebrew/pplay/mpv");
    logged_mpv_set_option_string(handle, "osd-fonts-dir", "/data/homebrew/pplay/mpv");
#endif

#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(2,0)
    logged_mpv_set_option_string(handle, "vo", "libmpv");
#endif

#ifdef FULL_TEXTURE_TEST
    logged_mpv_set_option_string(handle, "video-unscaled", "yes");
#endif
    //TODO: should add this as option (big quality loss)
    //logged_mpv_set_option_string(handle, "vd-lavc-skiploopfilter", "all");
    //logged_mpv_set_option_string(handle, "vd-lavc-fast", "yes");

#ifdef __SMB2__
    register_smb_mpv(handle);
#endif

#if defined(__LINUX__) && defined(NDEBUG)
    logged_mpv_set_option_string(handle, "hwdec", "auto-safe");
#endif

    if (!initRender) {
        logged_mpv_set_option_string(handle, "vid", "no");
        logged_mpv_set_option_string(handle, "aid", "no");
        logged_mpv_set_option_string(handle, "sid", "no");
        logged_mpv_set_option_string(handle, "vo", "null");
        logged_mpv_set_option_string(handle, "ao", "null");
    }

    int res = logged_mpv_initialize(handle);
    if (res) {
        printf("Mpv::Mpv: error: mpv_initialize: %s\n", mpv_error_string(res));
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
            std::string("Mpv::Mpv: error: mpv_initialize:\n") + mpv_error_string(res));

        mpv_terminate_destroy(handle);
        handle = nullptr;
        return;
    }

    char *mpvVersion = logged_mpv_get_property_string(handle, "mpv-version");
    std::string versionStr = mpvVersion ? mpvVersion : "unknown";
    mpv_node node;
    std::string protoList = "unknown";
    if (logged_mpv_get_property(handle, "protocol-list", MPV_FORMAT_NODE, &node) >= 0) {
        if (node.format == MPV_FORMAT_NODE_ARRAY && node.u.list) {
            protoList.clear();
            for (int i = 0; i < node.u.list->num; i++) {
                const mpv_node &v = node.u.list->values[i];
                if (v.format == MPV_FORMAT_STRING && v.u.string) {
                    if (!protoList.empty())
                        protoList += " ";
                    protoList += v.u.string;
                }
            }
        }
        mpv_free_node_contents(&node);
    }
    pplay::Utility::log(
        pplay::Utility::LogLevel::Info,
        "Mpv::Mpv: version: " + versionStr + " | protocols: " + protoList
    );
    if (mpvVersion) {
        mpv_free(mpvVersion);
    }

    auto in_protos = get_ffmpeg_protocols(false);
    auto out_protos = get_ffmpeg_protocols(true);
    std::string log = "FFmpeg protocols:\n  Input: " + join_protocols(in_protos) +
                      "\n  Output: " + join_protocols(out_protos);
    pplay::Utility::log(pplay::Utility::LogLevel::Info, log);
    if (initRender) {
#if MPV_CLIENT_API_VERSION < MPV_MAKE_VERSION(2,0)
        mpv_opengl_init_params gl_init_params{get_proc_address_mpv, nullptr, nullptr};
#else
        mpv_opengl_init_params gl_init_params{get_proc_address_mpv, nullptr};
#endif
        mpv_render_param params[]{
                {MPV_RENDER_PARAM_API_TYPE,           (void *) MPV_RENDER_API_TYPE_OPENGL},
                {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
                {MPV_RENDER_PARAM_INVALID,            nullptr}
        };

        int renderRes = logged_mpv_render_context_create(&context, handle, params);
        if (renderRes < 0) {
            pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                std::string("Mpv::Mpv: error: mpv_render_context_create: ") + mpv_error_string(renderRes));
            mpv_terminate_destroy(handle);
            handle = nullptr;
        }
    }
}

Mpv::~Mpv() {
    if (context) {
        mpv_render_context_free(context);
    }
    if (handle) {
        mpv_terminate_destroy(handle);
    }
}

int Mpv::load(const std::string &file, LoadType loadType, const std::string &options) {
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Mpv::load(" + file + ")");
    if (handle) {
        stop();
        std::string type = "replace";
        if (loadType == LoadType::Append) {
            type = "append";
        } else if (loadType == LoadType::AppendPlay) {
            type = "append-play";
        }
#if MPV_CLIENT_API_VERSION < MPV_MAKE_VERSION(2,3)
        const char *cmd[] = {"loadfile", file.c_str(), type.c_str(), options.c_str(), nullptr};
#else
        const char *cmd[] = {"loadfile", file.c_str(), type.c_str(), "-1", options.c_str(), nullptr};
#endif
        return logged_mpv_command(handle, cmd);
    }

    return -1;
}

int Mpv::save() {
    return logged_mpv_command_string(handle, "write-watch-later-config");
}

int Mpv::pause() {
    return logged_mpv_command_string(handle, "set pause yes");
}

int Mpv::resume() {
    return logged_mpv_command_string(handle, "set pause no");
}

int Mpv::stop() {
    return logged_mpv_command_string(handle, "stop");
}

std::string Mpv::changeBrightness(double delta) {
    std::string cmd = "no-osd add brightness " + std::to_string(delta);
    logged_mpv_command_string(handle, cmd.c_str());
    double brightness = 0.0;
    logged_mpv_get_property(handle, "brightness", MPV_FORMAT_DOUBLE, &brightness);
    return "Brightness: " + std::to_string((int) brightness) + "%";
}

std::string Mpv::changeVolume(double delta) {
    std::string cmd = "no-osd add volume " + std::to_string(delta);
    logged_mpv_command_string(handle, cmd.c_str());
    double volume = 0.0;
    logged_mpv_get_property(handle, "volume", MPV_FORMAT_DOUBLE, &volume);
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
        std::string cmd = "no-osd set volume " + std::to_string((int)lastVolume);
        int res = logged_mpv_command_string(handle, cmd.c_str());
        return res;
    }
    return 0;
}

int Mpv::getOsdWidth() {
    int64_t res = -1;
    logged_mpv_get_property(handle, "osd-width", MPV_FORMAT_INT64, &res);
    return static_cast<int>(res);
}

int Mpv::getOsdHeight() {
    int64_t res = -1;
    logged_mpv_get_property(handle, "osd-height", MPV_FORMAT_INT64, &res);
    return static_cast<int>(res);
}

int Mpv::seek(double position) {
    std::string cmd = "no-osd seek " + std::to_string(position);
    return logged_mpv_command_string(handle, cmd.c_str());
}

std::string Mpv::setSpeed(double speed) {
    std::string cmd = "no-osd set speed " + std::to_string(speed);
    logged_mpv_command_string(handle, cmd.c_str());
    double currentSpeed = getSpeed();
    return "Speed: " + std::to_string((int) (currentSpeed * 100.0)) + "%";
}

double Mpv::getSpeed() {
    double res = -1;
    logged_mpv_get_property(handle, "speed", MPV_FORMAT_DOUBLE, &res);
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
        return logged_mpv_command_string(handle, cmd.c_str());
    }
    return 0;
}

int Mpv::setVid(int id) {
    std::string cmd = "no-osd set vid ";
    cmd += id < 0 ? "no" : std::to_string(id);
    return logged_mpv_command_string(handle, cmd.c_str());
}

int Mpv::setAid(int id) {
    std::string cmd = "no-osd set aid ";
    cmd += id < 0 ? "no" : std::to_string(id);
    return logged_mpv_command_string(handle, cmd.c_str());
}

int Mpv::setSid(int id) {
    std::string cmd = "no-osd set sid ";
    cmd += id < 0 ? "no" : std::to_string(id);
    return logged_mpv_command_string(handle, cmd.c_str());
}

int Mpv::setPlaylistPos(int id) {
    int64_t pos = id;
    return mpv_set_property(handle, "playlist-pos", MPV_FORMAT_INT64, &pos);
}

int Mpv::getVid() {
    int64_t vid = -1;
    logged_mpv_get_property(handle, "vid", MPV_FORMAT_INT64, &vid);
    return (int) vid;
}

int Mpv::getAid() {
    int64_t aid = -1;
    logged_mpv_get_property(handle, "aid", MPV_FORMAT_INT64, &aid);
    return (int) aid;
}

int Mpv::getSid() {
    int64_t sid = -1;
    logged_mpv_get_property(handle, "sid", MPV_FORMAT_INT64, &sid);
    return (int) sid;
}

int Mpv::getPlaylistPos() {
    int64_t pos = -1;
    logged_mpv_get_property(handle, "playlist-pos", MPV_FORMAT_INT64, &pos);
    return (int) pos;
}

int Mpv::getPlaylistCount() {
    int64_t count = 0;
    logged_mpv_get_property(handle, "playlist-count", MPV_FORMAT_INT64, &count);
    return (int) count;
}

std::string Mpv::getPlaylistCurrentTitle() {
    char *title = logged_mpv_get_property_string(handle, "media-title");
    std::string res = title ? title : "";
    if (title) mpv_free(title);
    return res;
}

std::string Mpv::getCurrentPath() {
    char *path = logged_mpv_get_property_string(handle, "path");
    std::string res = path ? path : "";
    if (path) mpv_free(path);
    return res;
}

std::vector<std::pair<int, std::string>> Mpv::getPlaylistItems() {
    std::vector<std::pair<int, std::string>> items;
    mpv_node node;
    if (logged_mpv_get_property(handle, "playlist", MPV_FORMAT_NODE, &node) < 0) {
        return items;
    }
    if (node.format == MPV_FORMAT_NODE_ARRAY && node.u.list) {
        for (int i = 0; i < node.u.list->num; i++) {
            const mpv_node &entry = node.u.list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP || !entry.u.list) continue;
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
    logged_mpv_get_property(handle, "video-bitrate", MPV_FORMAT_DOUBLE, &bitrate);
    return (int) bitrate;
}

int Mpv::getAudioBitrate() {
    int64_t bitrate = 0;
    logged_mpv_get_property(handle, "audio-bitrate", MPV_FORMAT_INT64, &bitrate);
    return (int) bitrate;
}

long Mpv::getDuration() {
    long duration = 0;
    logged_mpv_get_property(handle, "duration", MPV_FORMAT_INT64, &duration);
    return duration;
}

long Mpv::getPosition() {
    long position = 0;
    logged_mpv_get_property(handle, "playback-time", MPV_FORMAT_INT64, &position);
    return position;
}

bool Mpv::isAvailable() {
    return handle != nullptr;
}

bool Mpv::isStopped() {
    int res = 1;
    logged_mpv_get_property(handle, "playback-abort", MPV_FORMAT_FLAG, &res);
    return res == 1;
}

bool Mpv::isPaused() {
    int res = -1;
    logged_mpv_get_property(handle, "pause", MPV_FORMAT_FLAG, &res);
    return res == 1;
}

mpv_event *Mpv::getEvent() {
    return logged_mpv_wait_event(handle, 0);
}

mpv_handle *Mpv::getHandle() {
    return handle;
}

mpv_render_context *Mpv::getContext() {
    return context;
}

MediaInfo Mpv::getMediaInfo(const c2d::Io::File &file) {
    MediaInfo mediaInfo(file);
    std::vector<MediaInfo::Track> streams;

    if (!isAvailable() || isStopped()) {
        return mediaInfo;
    }

    // load track list
    mpv_node node;
    if (logged_mpv_get_property(handle, "track-list", MPV_FORMAT_NODE, &node) >= 0 &&
        node.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < node.u.list->num; i++) {
            if (node.u.list->values[i].format == MPV_FORMAT_NODE_MAP) {
                MediaInfo::Track stream{};
                for (int n = 0; n < node.u.list->values[i].u.list->num; n++) {
                    std::string key = node.u.list->values[i].u.list->keys[n];
                    if (key == "type") {
                        if (node.u.list->values[i].u.list->values[n].format == MPV_FORMAT_STRING) {
                            stream.type = node.u.list->values[i].u.list->values[n].u.string;
                        }
                    } else if (key == "id") {
                        if (node.u.list->values[i].u.list->values[n].format == MPV_FORMAT_INT64) {
                            stream.id = (int) node.u.list->values[i].u.list->values[n].u.int64;
                        }
                    } else if (key == "title") {
                        if (node.u.list->values[i].u.list->values[n].format == MPV_FORMAT_STRING) {
                            stream.title = node.u.list->values[i].u.list->values[n].u.string;
                        }
                    } else if (key == "lang") {
                        if (node.u.list->values[i].u.list->values[n].format == MPV_FORMAT_STRING) {
                            stream.language = node.u.list->values[i].u.list->values[n].u.string;
                        }
                    } else if (key == "codec") {
                        if (node.u.list->values[i].u.list->values[n].format == MPV_FORMAT_STRING) {
                            stream.codec = node.u.list->values[i].u.list->values[n].u.string;
                        }
                    } else if (key == "demux-w") {
                        if (node.u.list->values[i].u.list->values[n].format == MPV_FORMAT_INT64) {
                            stream.width = (int) node.u.list->values[i].u.list->values[n].u.int64;
                        }
                    } else if (key == "demux-h") {
                        if (node.u.list->values[i].u.list->values[n].format == MPV_FORMAT_INT64) {
                            stream.height = (int) node.u.list->values[i].u.list->values[n].u.int64;
                        }
                    } else if (key == "demux-samplerate") {
                        if (node.u.list->values[i].u.list->values[n].format == MPV_FORMAT_INT64) {
                            stream.sample_rate = (int) node.u.list->values[i].u.list->values[n].u.int64;
                        }
                    } else if (key == "demux-channel-count") {
                        if (node.u.list->values[i].u.list->values[n].format == MPV_FORMAT_INT64) {
                            stream.channels = (int) node.u.list->values[i].u.list->values[n].u.int64;
                        }
                    }
                }
                streams.push_back(stream);
            }
        }
    }

    // set media info tracks
    mediaInfo.videos.clear();
    mediaInfo.audios.clear();
    mediaInfo.subtitles.clear();
    for (auto &stream: streams) {
        if (stream.type == "video") {
            mediaInfo.videos.push_back(stream);
        } else if (stream.type == "audio") {
            mediaInfo.audios.push_back(stream);
        } else if (stream.type == "sub") {
            mediaInfo.subtitles.push_back(stream);
        }
    }

    // set duration
    mediaInfo.duration = getDuration();

    return mediaInfo;
}
