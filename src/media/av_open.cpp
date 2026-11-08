//
// libavformat open with timeouts.
//

#include <chrono>

#include "av_open.h"
#include "utility.h"

namespace {
    using Clock = std::chrono::steady_clock;

    struct Deadline {
        Clock::time_point at;
    };

    // the whole open, whatever the server does
    int interrupted(void *deadline) {
        return Clock::now() > ((Deadline *) deadline)->at ? 1 : 0;
    }

    AVDictionary *networkOptions(const std::string &url) {
        AVDictionary *options = nullptr;
        av_dict_set(&options, "rw_timeout", "15000000", 0);
        size_t scheme = url.find("://");
        size_t at = url.find('@');
        if (scheme != std::string::npos && at != std::string::npos && at < url.find('/', scheme + 3)) {
            av_dict_set(&options, "auth_type", "basic", 0);
        }
        // the bundled certificates (verified: ffmpeg's default)
#if defined(__PS4__) || defined(__PS5__) || defined(__ANDROID__)
        av_dict_set(&options, "ca_file", pplay::Utility::getCertificatesPath().c_str(), 0);
#endif
        return options;
    }

    std::string errorText(int res) {
        char text[128];
        av_strerror(res, text, sizeof(text));
        return text;
    }
}

AVFormatContext *pplay::AvOpen::open(const std::string &url, std::string *error) {
    // owned by the context (the callback is called while closing too): see close()
    auto *deadline = new Deadline{Clock::now() + std::chrono::seconds(30)};

    AVFormatContext *ctx = avformat_alloc_context();
    ctx->interrupt_callback = {interrupted, deadline};
    AVDictionary *options = networkOptions(url);
    // the headers only (a few MB at most through the network)
    av_dict_set(&options, "probesize", "2000000", 0);
    av_dict_set(&options, "analyzeduration", "1000000", 0);
    int res = avformat_open_input(&ctx, url.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (res < 0) {
        // the context freed by avformat_open_input()
        delete (deadline);
        *error = errorText(res);
        return nullptr;
    }
    return ctx;
}

void pplay::AvOpen::close(AVFormatContext **ctx) {
    if (ctx == nullptr || *ctx == nullptr) {
        return;
    }
    auto *deadline = (Deadline *) (*ctx)->interrupt_callback.opaque;
    avformat_close_input(ctx);
    delete (deadline);
}

struct pplay::AvOpen::Stream {
    AVIOContext *io = nullptr;
    const std::atomic<bool> *cancel = nullptr;
    int64_t offset = 0;
};

static int cancelled(void *stream) {
    auto *s = (pplay::AvOpen::Stream *) stream;
    return s->cancel != nullptr && s->cancel->load() ? 1 : 0;
}

pplay::AvOpen::Stream *pplay::AvOpen::openStream(const std::string &url, int64_t offset,
                                                 const std::atomic<bool> *cancel, std::string *error) {
    auto *s = new Stream();
    s->cancel = cancel;
    AVIOInterruptCB callback = {cancelled, s};
    AVDictionary *options = networkOptions(url);
    int res = avio_open2(&s->io, url.c_str(), AVIO_FLAG_READ, &callback, &options);
    av_dict_free(&options);
    if (res < 0) {
        *error = errorText(res);
        delete s;
        return nullptr;
    }
    // http: a range request, ftp: REST
    if (offset > 0 && avio_seek(s->io, offset, SEEK_SET) == offset) {
        s->offset = offset;
    } else if (offset > 0) {
        avio_closep(&s->io);
        delete s;
        return openStream(url, 0, cancel, error);
    }
    return s;
}

int64_t pplay::AvOpen::size(Stream *stream) {
    int64_t size = avio_size(stream->io);
    return size > 0 ? size : -1;
}

int64_t pplay::AvOpen::offset(Stream *stream) {
    return stream->offset;
}

int pplay::AvOpen::read(Stream *stream, uint8_t *buffer, int size, std::string *error) {
    int res = avio_read(stream->io, buffer, size);
    if (res == AVERROR_EOF) {
        return 0;
    }
    if (res < 0) {
        *error = errorText(res);
        return -1;
    }
    stream->offset += res;
    return res;
}

void pplay::AvOpen::close(Stream **stream) {
    if (stream == nullptr || *stream == nullptr) {
        return;
    }
    avio_closep(&(*stream)->io);
    delete *stream;
    *stream = nullptr;
}

bool pplay::AvOpen::read(const std::string &url, size_t maxSize, std::string *data, std::string *error) {
    Utility::NetworkLock lock;
    Deadline deadline{Clock::now() + std::chrono::seconds(30)};
    AVIOInterruptCB callback = {interrupted, &deadline};

    AVIOContext *io = nullptr;
    AVDictionary *options = networkOptions(url);
    int res = avio_open2(&io, url.c_str(), AVIO_FLAG_READ, &callback, &options);
    av_dict_free(&options);
    if (res < 0) {
        *error = errorText(res);
        return false;
    }
    data->clear();
    unsigned char buffer[16384];
    while ((res = avio_read(io, buffer, sizeof(buffer))) > 0) {
        data->append((const char *) buffer, (size_t) res);
        if (data->size() > maxSize) {
            res = AVERROR(EFBIG);
            break;
        }
    }
    avio_closep(&io);
    if (res < 0 && res != AVERROR_EOF) {
        *error = errorText(res);
        return false;
    }
    return true;
}
