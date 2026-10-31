//
// Created by cpasjuste on 02/04/19.
//

#include <SDL_video.h>
#include <cmath>
#include "mpv.h"
#include "utility.h"
#include "io.h"
#ifdef __SMB2__
#include "smb.h"
#endif

#include <vector>
#include <string>
#include <sstream>

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

    handle = mpv_create();
    if (!handle) {
        pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Mpv::Mpv: error: mpv_create");
        return;
    }

    mpv_set_option_string(handle, "config", "yes");
    mpv_set_option_string(handle, "config-dir", configPath.c_str());
#ifdef __LINUX__
    // where Utility::getWatchLater() looks (default: ~/.local/state/mpv)
    mpv_set_option_string(handle, "watch-later-directory", (configPath + "/watch_later").c_str());
#endif
    mpv_set_option_string(handle, "osd-scale", "0.5");
#ifndef NDEBUG
    mpv_set_option_string(handle, "terminal", "yes");
    mpv_set_option_string(handle, "msg-level", "all=v");
#endif

#ifdef __SWITCH__
    mpv_set_option_string(handle, "vd-lavc-threads", "4");
    mpv_set_option_string(handle, "dither", "no");
#else
    mpv_set_option_string(handle, "vd-lavc-threads", "6");
    mpv_set_option_string(handle, "video-sync", "audio");
#endif

    mpv_set_option_string(handle, "audio-channels", "stereo");
    mpv_set_option_string(handle, "audio-normalize-downmix", "yes");
    mpv_set_option_string(handle, "cache-pause", "yes");
    mpv_set_option_string(handle, "cache-secs", "60");
    mpv_set_option_string(handle, "stream-lavf-o", "reconnect_on_network_error=1");

#if defined(__PS4__) || defined(__PS5__)
    mpv_set_option_string(handle, "dither", "no");
    mpv_set_option_string(handle, "ignore-path-in-watch-later-config", "yes");
    mpv_set_option_string(handle, "tls-ca-file", pplay::Utility::getCertificatesPath().c_str());
    mpv_set_option_string(handle, "sub-ass-override", "force");
    mpv_set_option_string(handle, "osd-rendering-mode", "normal");
    mpv_set_option_string(handle, "sub-font-provider", "none");
    mpv_set_option_string(handle, "osd-font-provider", "none");
    mpv_set_option_string(handle, "sub-font", "subfont.ttf");
    mpv_set_option_string(handle, "osd-font", "subfont.ttf");
#endif

#ifdef __PS4__
    mpv_set_option_string(handle, "cscale", "bilinear");
    mpv_set_option_string(handle, "scale", "bilinear");
    mpv_set_option_string(handle, "dscale", "bilinear");
    mpv_set_option_string(handle, "sws-scaler", "bilinear");
    mpv_set_option_string(handle, "sub-fonts-dir", "/data/pplay/mpv");
    mpv_set_option_string(handle, "osd-fonts-dir", "/data/pplay/mpv");
#endif

#ifdef __PS5__
    mpv_set_option_string(handle, "cscale", "bilinear");
    mpv_set_option_string(handle, "scale", "bicubic");
    mpv_set_option_string(handle, "dscale", "bicubic");
    mpv_set_option_string(handle, "sws-scaler", "bicubic");
    mpv_set_option_string(handle, "sub-fonts-dir", "/data/homebrew/pplay/mpv");
    mpv_set_option_string(handle, "osd-fonts-dir", "/data/homebrew/pplay/mpv");
#endif

#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(2, 0)
    mpv_set_option_string(handle, "vo", "libmpv");
#endif

#ifdef __SMB2__
    pplay::Smb::registerStream(handle);
#endif

#if defined(__LINUX__) && defined(NDEBUG)
    mpv_set_option_string(handle, "hwdec", "auto-safe");
#endif

    if (!initRender) {
        mpv_set_option_string(handle, "vid", "no");
        mpv_set_option_string(handle, "aid", "no");
        mpv_set_option_string(handle, "sid", "no");
        mpv_set_option_string(handle, "vo", "null");
        mpv_set_option_string(handle, "ao", "null");
    }

    int res = mpv_initialize(handle);
    if (res) {
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
                            std::string("Mpv::Mpv: error: mpv_initialize:\n") + mpv_error_string(res));

        mpv_terminate_destroy(handle);
        handle = nullptr;
        return;
    }

    // pPlay sets these itself (audio filter, volume, speed): not per file
    for (const char *name: {"af", "volume", "speed"}) {
        const char *cmd[] = {"change-list", "watch-later-options", "remove", name, nullptr};
        mpv_command(handle, cmd);
    }

    char *mpvVersion = mpv_get_property_string(handle, "mpv-version");
    std::string versionStr = mpvVersion ? mpvVersion : "unknown";
    pplay::Utility::setMpvVersion(versionStr);
    mpv_node node;
    std::string protoList = "unknown";
    if (mpv_get_property(handle, "protocol-list", MPV_FORMAT_NODE, &node) >= 0) {
        if (node.format == MPV_FORMAT_NODE_ARRAY && node.u.list) {
            protoList.clear();
            for (int i = 0; i < node.u.list->num; i++) {
                const mpv_node &v = node.u.list->values[i];
                if (v.format == MPV_FORMAT_STRING && v.u.string) {
                    if (!protoList.empty()) {
                        protoList += " ";
                    }
                    protoList += v.u.string;
                }
            }
        }
        mpv_free_node_contents(&node);
    }
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Mpv::Mpv: version: " + versionStr + " | protocols: " + protoList);
    if (mpvVersion) {
        mpv_free(mpvVersion);
    }

    // quality preset "best": the values after mpv.conf
    for (const char *name: qualityOptions()) {
        char *value = mpv_get_property_string(handle, name);
        if (value) {
            qualityDefaults[name] = value;
            mpv_free(value);
        }
    }

    if (initRender) {
#if MPV_CLIENT_API_VERSION < MPV_MAKE_VERSION(2, 0)
        mpv_opengl_init_params gl_init_params{get_proc_address_mpv, nullptr, nullptr};
#else
        mpv_opengl_init_params gl_init_params{get_proc_address_mpv, nullptr};
#endif
        mpv_render_param params[]{{MPV_RENDER_PARAM_API_TYPE, (void *) MPV_RENDER_API_TYPE_OPENGL},
                                  {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
                                  {MPV_RENDER_PARAM_INVALID, nullptr}};

        int renderRes = mpv_render_context_create(&context, handle, params);
        if (renderRes < 0) {
            pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                                std::string("Mpv::Mpv: error: mpv_render_context_create: ")
                                        + mpv_error_string(renderRes));
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
#if MPV_CLIENT_API_VERSION < MPV_MAKE_VERSION(2, 3)
        const char *cmd[] = {"loadfile", file.c_str(), type.c_str(), options.c_str(), nullptr};
#else
        const char *cmd[] = {"loadfile", file.c_str(), type.c_str(), "-1", options.c_str(), nullptr};
#endif
        return mpv_command(handle, cmd);
    }

    return -1;
}

const std::vector<const char *> &Mpv::qualityOptions() {
    static const std::vector<const char *> names = {"scale",
                                                    "dscale",
                                                    "cscale",
                                                    "correct-downscaling",
                                                    "linear-downscaling",
                                                    "sigmoid-upscaling",
                                                    "deband",
                                                    "hdr-compute-peak",
                                                    "vd-lavc-skiploopfilter",
                                                    "vd-lavc-fast",
                                                    "framedrop"};
    return names;
}

void Mpv::setQualityPreset(int preset) {
    if (!isAvailable()) {
        return;
    }
    // balanced: cheap scaling (the renderer may be software: PS5 OSMesa), loop filter skipped on
    // non reference frames; fast: loop filter skipped, fast decoding, late frames not decoded
    std::map<std::string, std::string> values = qualityDefaults;
    if (preset >= 1) {
        for (const char *name: {"scale", "dscale", "cscale"}) {
            values[name] = "bilinear";
        }
        for (const char *name:
             {"correct-downscaling", "linear-downscaling", "sigmoid-upscaling", "deband", "hdr-compute-peak"}) {
            values[name] = "no";
        }
        values["vd-lavc-skiploopfilter"] = preset >= 2 ? "all" : "nonref";
    }
    if (preset >= 2) {
        values["vd-lavc-fast"] = "yes";
        values["framedrop"] = "decoder+vo";
    }
    for (auto &value: values) {
        int res = mpv_set_property_string(handle, value.first.c_str(), value.second.c_str());
        if (res < 0) {
            pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Mpv::setQualityPreset: " + value.first + "="
                                                                         + value.second + ": " + mpv_error_string(res));
        }
    }
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Mpv::setQualityPreset: " + std::to_string(preset));
}

void Mpv::setCache(int seconds, int megabytes) {
    if (!isAvailable()) {
        return;
    }
    mpv_set_property_string(handle, "cache-secs", std::to_string(seconds).c_str());
    mpv_set_property_string(handle, "demuxer-max-bytes", (std::to_string(megabytes) + "MiB").c_str());
}
