//
// Created by cpasjuste on 04/12/18.
//

#ifndef PPLAY_MEDIAFILE_H
#define PPLAY_MEDIAFILE_H

#include <vector>

#include "cross2d/skeleton/io.h"
#include "media_info.h"

class MediaFile : public c2d::Io::File {

public:

    MediaFile() = default;

    explicit MediaFile(const c2d::Io::File &file, const MediaInfo &media) {
        name = file.name;
        path = file.path;
        type = file.type;
        size = file.size;
        mediaInfo = media;
    }

    MediaInfo mediaInfo;
    // shown instead of the name (a name met twice in a list: its directory too)
    std::string label;
};

#endif //PPLAY_MEDIAFILE_H
