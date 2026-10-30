//
// Embedded cover of a media file: mkv attachment, mp4 / mp3 attached picture.
//

#ifndef PPLAY_COVER_H
#define PPLAY_COVER_H

#include <string>

struct AVFormatContext;

namespace pplay::Cover {

    // the pictures cache url of a media cover
    std::string cacheUrl(const std::string &mediaUrl);

    // the picture of an opened media to the cache; false (and the reason) if none
    bool save(AVFormatContext *ctx, const std::string &mediaUrl, std::string *error);

    // the same, the media opened
    bool extract(const std::string &mediaUrl, std::string *error);
}

#endif //PPLAY_COVER_H
