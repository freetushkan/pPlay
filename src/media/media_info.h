//
// Created by cpasjuste on 13/11/18.
//

#ifndef PPLAY_MEDIA_INFO_H
#define PPLAY_MEDIA_INFO_H

#include <string>
#include <vector>

#include "cross2d/skeleton/io.h"

class MediaInfo {

public:

    struct Track {
        int id = 0;
        std::string type;
        std::string title;
        std::string language;
        std::string codec;
        // audio
        int channels = 0;
        std::string layout;
        int sample_rate = 0;
        // video
        int width = 0;
        int height = 0;
        double fps = 0;
        // bits per second, 0: unknown
        long bit_rate = 0;
        bool isDefault = false;
        bool forced = false;
        bool external = false;
        // embedded cover (attached picture)
        bool albumart = false;
    };

    // "448 kb/s", "8.5 Mb/s"
    static std::string formatBitrate(long bitsPerSecond);

    // one line: language, codec, channels / resolution, bitrate, «title»...
    static std::string describe(const Track &track);

    bool hasCover() const;

    MediaInfo() = default;

    // the cached information, if any
    explicit MediaInfo(const c2d::Io::File &file);

    // cached (the file own tracks)
    void save(const c2d::Io::File &file);

    // read from the cache (or probed): not probed again
    bool known = false;
    long duration = 0;
    std::string format;
    // the file metadata title
    std::string title;
    std::vector<Track> videos;
    std::vector<Track> audios;
    std::vector<Track> subtitles;
};

#endif //PPLAY_MEDIA_INFO_H
