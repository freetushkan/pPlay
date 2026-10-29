//
// What the details panel shows about a file: title, poster, details.
//

#ifndef PPLAY_DETAILS_INFO_H
#define PPLAY_DETAILS_INFO_H

#include <string>
#include "media_file.h"

namespace pplay {

    struct DetailsInfo {
        std::string title;
        std::string poster;
        std::string details;

        static DetailsInfo of(const MediaFile &file);
    };
}

#endif //PPLAY_DETAILS_INFO_H
