//
// libavformat open of a media url (local or network) with timeouts: covers and media
// information read without mpv.
//

#ifndef PPLAY_AV_OPEN_H
#define PPLAY_AV_OPEN_H

#include <atomic>
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

    struct Stream;

    // a file read in parts from offset (the rest if the server can't seek: see offset()),
    // stopped once *cancel is set; nullptr on errors
    Stream *openStream(const std::string &url, int64_t offset, const std::atomic<bool> *cancel, std::string *error);

    // -1 unknown
    int64_t size(Stream *stream);

    // where the next read starts
    int64_t offset(Stream *stream);

    // < 0 on errors, 0 at the end
    int read(Stream *stream, uint8_t *buffer, int size, std::string *error);

    void close(Stream **stream);
}

#endif //PPLAY_AV_OPEN_H
