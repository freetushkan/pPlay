//
// What the preview panel shows about a file: title, poster, details.
//

#ifndef PPLAY_PREVIEW_INFO_H
#define PPLAY_PREVIEW_INFO_H

#include <string>
#include "media_file.h"

namespace pplay {

    struct PreviewInfo {
        std::string title;
        std::string poster;
        std::string details;

        static PreviewInfo of(const MediaFile &file);
    };
}

#endif //PPLAY_PREVIEW_INFO_H
