//
// libavformat open of a media url (local or network) with timeouts: covers and media
// information read without mpv.
//

#ifndef PPLAY_AV_OPEN_H
#define PPLAY_AV_OPEN_H

#include <string>

extern "C" {
#include <libavformat/avformat.h>
}

namespace pplay::AvOpen {

    // the headers read; nullptr (and the reason) on errors. close() it.
    AVFormatContext *open(const std::string &url, std::string *error);

    void close(AVFormatContext **ctx);

    // a whole (small) file, up to maxSize bytes
    bool read(const std::string &url, size_t maxSize, std::string *data, std::string *error);
}

#endif //PPLAY_AV_OPEN_H
