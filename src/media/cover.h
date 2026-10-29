//
// Embedded cover of a media file: mkv attachment, mp4 / mp3 attached picture.
//

#ifndef PPLAY_COVER_H
#define PPLAY_COVER_H

#include <string>

namespace pplay::Cover {

    // the picture bytes written to path; false (and the reason) if none
    bool extract(const std::string &url, const std::string &path, std::string *error);
}

#endif //PPLAY_COVER_H
