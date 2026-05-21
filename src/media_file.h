//
// Created by cpasjuste on 04/12/18.
//

#ifndef PPLAY_MEDIAFILE_H
#define PPLAY_MEDIAFILE_H

#include <vector>

#ifdef PPLAY_ENABLE_SCRAPPING
#include "p_movie.h"
#endif
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
#ifdef PPLAY_ENABLE_SCRAPPING
    std::vector<pscrap::Movie> movies;
#endif
};

#endif //PPLAY_MEDIAFILE_H
