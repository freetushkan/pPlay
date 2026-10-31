//
// Created by cpasjuste on 02/04/19.
//

#ifndef PPLAY_MPV_H
#define PPLAY_MPV_H

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <mpv/client.h>
#include <mpv/render_gl.h>
#include "media_info.h"

#ifdef __SMB2__
#include <mpv/stream_cb.h>
#endif

class Mpv {

public:

    enum class LoadType {
        Replace,
        Append,
        AppendPlay
    };

    explicit Mpv(const std::string &configPath, bool initRender);

    ~Mpv();

    int load(const std::string &file, LoadType loadType, const std::string &options);

    int save();

    int pause();

    int resume();

    int stop();

    std::string changeBrightness(double delta);

    std::string changeVolume(double delta);

    void saveVolume();

    // restored on the next file (-1: mpv.conf one)
    void setLastVolume(double volume) { lastVolume = volume; }

    double getLastVolume() const { return lastVolume; }

    int restoreVolume();

    int getOsdWidth();

    int getOsdHeight();

    int seek(double position);

    std::string setSpeed(double speed);

    double getSpeed();

    // the last speed set, kept for the next files
    int restoreSpeed();

    int setVid(int id);

    int getVid();

    int setAid(int id);

    int getAid();

    int setSid(int id);

    int setPlaylistPos(int id);

    int getSid();
    int getPlaylistPos();
    int getPlaylistCount();
    std::string getPlaylistCurrentTitle();
    std::string getCurrentPath();
    std::vector<std::pair<int, std::string>> getPlaylistItems();

    int getVideoBitrate();

    int getAudioBitrate();

    long getDuration();

    long getPosition();

    double getTime();

    bool isPausedForCache();

    long getCacheSpeed();

    // buffered ahead (seconds), -1 if none
    double getCacheDuration();

    // displayed video frame, -1 if none
    long getFrameNumber();

    // a video track, not a cover picture
    bool hasMovingVideo();

    // audio visualizer: a lavfi graph from the audio track to the video output (the
    // track then changed with setAid()); "" for none, aid then the track to keep
    void setVisualizer(const std::string &graph, int aid);

    bool hasVisualizer() const { return !visualGraph.empty(); }

    // the picture in a part of the frame (ratios of the borders)
    void setFrameLayout(float left, float top, float right, float bottom);

    bool isStopped();

    bool isPaused();

    bool isAvailable();

    mpv_event *getEvent();

    mpv_handle *getHandle();

    mpv_render_context *getContext();

    MediaInfo getMediaInfo(const c2d::Io::File &file);

    struct TrackEntry {
        int id = -1;
        std::string type; // "video", "audio", "sub"
        std::string lang;
        bool external = false;
        bool selected = false;
        std::string filename; // external file url
    };

    std::vector<TrackEntry> getTrackList();

    // any property ("" / fallback if unavailable)
    std::string getString(const char *name);

    double getDouble(const char *name, double fallback = -1);

    long getInt(const char *name, long fallback = -1);

    // 0 best (mpv.conf values), 1 balanced, 2 fast; decoder options apply on decoder start
    void setQualityPreset(int preset);

    // network cache: read ahead seconds, memory
    void setCache(int seconds, int megabytes);

private:

    static const std::vector<const char *> &qualityOptions();

    std::map<std::string, std::string> qualityDefaults;

    void applyVisualizer();

    std::string visualGraph;
    int visualAid = -1;

    mpv_handle *handle = nullptr;
    mpv_render_context *context = nullptr;
    double lastVolume = -1;
    double lastSpeed = -1;
};

#endif //PPLAY_MPV_H
