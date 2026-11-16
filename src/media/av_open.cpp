//
// libavformat open with timeouts.
//

#include <chrono>

#include "av_open.h"
#include "utility.h"
#include "io.h"
#ifdef __SMB2__
#include "smb.h"
#endif

namespace {
    using Clock = std::chrono::steady_clock;

    struct Deadline {
        Clock::time_point at;
        // smb:// read by libsmb2 (no libavformat protocol)
        void *smb = nullptr;
        AVIOContext *io = nullptr;
    };

    // the whole open, whatever the server does
    int interrupted(void *deadline) {
        return Clock::now() > ((Deadline *) deadline)->at ? 1 : 0;
    }

    AVDictionary *networkOptions(const std::string &url) {
        AVDictionary *options = nullptr;
        int timeout = pplay::Io::get() != nullptr ? pplay::Io::get()->networkTimeout(url) : 15;
        av_dict_set(&options, "rw_timeout", std::to_string(timeout * 1000000LL).c_str(), 0);
        bool verify = pplay::Io::get() == nullptr || pplay::Io::get()->tlsVerify(url);
        size_t scheme = url.find("://");
        size_t at = url.find('@');
        if (scheme != std::string::npos && at != std::string::npos && at < url.find('/', scheme + 3)) {
            av_dict_set(&options, "auth_type", "basic", 0);
        }
        // the bundled certificates, checked unless turned off (the setting: the file not read)
        if (verify) {
            std::string certificates = pplay::Utility::getCertificatesPath();
            if (!certificates.empty()) {
                av_dict_set(&options, "ca_file", certificates.c_str(), 0);
            }
        }
        av_dict_set(&options, "tls_verify", verify ? "1" : "0", 0);
        return options;
    }

#ifdef __SMB2__
    const int SMB_IO_SIZE = 256 * 1024;

    struct SmbFile {
        void *file;
        int64_t size;
        int64_t pos;
    };

    int smbRead(void *opaque, uint8_t *buffer, int size) {
        auto *f = (SmbFile *) opaque;
        int64_t res = pplay::Smb::readFile(f->file, (char *) buffer, (uint64_t) size);
        if (res < 0) {
            return AVERROR(EIO);
        }
        if (res == 0) {
            return AVERROR_EOF;
        }
        f->pos += res;
        return (int) res;
    }

    int64_t smbSeek(void *opaque, int64_t offset, int whence) {
        auto *f = (SmbFile *) opaque;
        whence &= ~AVSEEK_FORCE;
        if (whence == AVSEEK_SIZE) {
            return f->size;
        }
        int64_t to = whence == SEEK_CUR ? f->pos + offset : whence == SEEK_END ? f->size + offset : offset;
        if (to < 0 || (whence == SEEK_END && f->size < 0) || !pplay::Smb::seekFile(f->file, to)) {
            return AVERROR(EINVAL);
        }
        f->pos = to;
        return to;
    }

    AVIOContext *smbOpen(const std::string &url, std::string *error) {
        int64_t size = -1;
        void *file = pplay::Smb::openFile(url, &size);
        if (file == nullptr) {
            *error = "SMB: can't open";
            return nullptr;
        }
        auto *f = new SmbFile{file, size, 0};
        auto *buffer = (unsigned char *) av_malloc(SMB_IO_SIZE);
        AVIOContext *io = buffer != nullptr
                                  ? avio_alloc_context(buffer, SMB_IO_SIZE, 0, f, smbRead, nullptr, smbSeek)
                                  : nullptr;
        if (io == nullptr) {
            av_free(buffer);
            pplay::Smb::closeFile(file);
            delete f;
            *error = "out of memory";
        }
        return io;
    }

    void smbClose(AVIOContext **io) {
        if (*io == nullptr) {
            return;
        }
        auto *f = (SmbFile *) (*io)->opaque;
        pplay::Smb::closeFile(f->file);
        delete f;
        av_freep(&(*io)->buffer);
        avio_context_free(io);
    }
#endif

    bool isSmb(const std::string &url) {
#ifdef __SMB2__
        return url.compare(0, 6, "smb://") == 0;
#else
        return false;
#endif
    }

    std::string errorText(int res) {
        char text[128];
        av_strerror(res, text, sizeof(text));
        return text;
    }
}

AVFormatContext *pplay::AvOpen::open(const std::string &address, std::string *error) {
    // the network user and password in it
    std::string url = Io::get() != nullptr ? Io::get()->withCredentials(address) : address;
    // owned by the context (the callback is called while closing too): see close()
    auto *deadline = new Deadline{Clock::now() + std::chrono::seconds(30)};

    AVFormatContext *ctx = avformat_alloc_context();
    ctx->interrupt_callback = {interrupted, deadline};
#ifdef __SMB2__
    if (isSmb(url)) {
        deadline->io = smbOpen(url, error);
        if (deadline->io == nullptr) {
            avformat_free_context(ctx);
            delete (deadline);
            return nullptr;
        }
        ctx->pb = deadline->io;
        ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
    }
#endif
    AVDictionary *options = networkOptions(url);
    // the headers only (a few MB at most through the network)
    av_dict_set(&options, "probesize", "2000000", 0);
    av_dict_set(&options, "analyzeduration", "1000000", 0);
    int res = avformat_open_input(&ctx, url.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (res < 0) {
        // the context freed by avformat_open_input()
#ifdef __SMB2__
        smbClose(&deadline->io);
#endif
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
#ifdef __SMB2__
    smbClose(&deadline->io);
#endif
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

pplay::AvOpen::Stream *pplay::AvOpen::openStream(const std::string &address, int64_t offset,
                                                 const std::atomic<bool> *cancel, std::string *error) {
    std::string url = Io::get() != nullptr ? Io::get()->withCredentials(address) : address;
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

bool pplay::AvOpen::read(const std::string &address, size_t maxSize, std::string *data, std::string *error) {
    std::string url = Io::get() != nullptr ? Io::get()->withCredentials(address) : address;
    Utility::NetworkLock lock;
    Deadline deadline{Clock::now() + std::chrono::seconds(30)};
    AVIOInterruptCB callback = {interrupted, &deadline};

    AVIOContext *io = nullptr;
    int res = 0;
#ifdef __SMB2__
    if (isSmb(url)) {
        io = smbOpen(url, error);
        if (io == nullptr) {
            return false;
        }
    }
#endif
    if (io == nullptr) {
        AVDictionary *options = networkOptions(url);
        res = avio_open2(&io, url.c_str(), AVIO_FLAG_READ, &callback, &options);
        av_dict_free(&options);
        if (res < 0) {
            *error = errorText(res);
            return false;
        }
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
#ifdef __SMB2__
    if (isSmb(url)) {
        smbClose(&io);
    }
#endif
    if (io != nullptr) {
        avio_closep(&io);
    }
    if (res < 0 && res != AVERROR_EOF) {
        *error = errorText(res);
        return false;
    }
    return true;
}
