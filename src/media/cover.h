//
// Embedded cover of a media file: mkv attachment, mp4 / mp3 attached picture.
//

#ifndef PPLAY_COVER_H
#define PPLAY_COVER_H

#include <string>

struct AVFormatContext;

namespace pplay::Cover {

    // the posters cache file of an url (poster or "embedded:<media url>")
    std::string cachePath(const std::string &url);

    // the picture of an opened media written to path; false (and the reason) if none
    bool save(AVFormatContext *ctx, const std::string &path, std::string *error);

    // the same, the media opened
    bool extract(const std::string &url, const std::string &path, std::string *error);
}

#endif //PPLAY_COVER_H
