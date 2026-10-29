//
// Embedded covers, read with libavformat (the headers only).
//

#include <chrono>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
}

#include "cover.h"
#include "utility.h"

namespace {
    using Clock = std::chrono::steady_clock;

    // the whole open, whatever the server does
    int interrupted(void *deadline) {
        return Clock::now() > *(Clock::time_point *) deadline ? 1 : 0;
    }

    bool write(const std::string &path, const uint8_t *data, int size) {
        FILE *f = fopen(path.c_str(), "wb");
        bool ok = f != nullptr && fwrite(data, 1, (size_t) size, f) == (size_t) size;
        if (f) {
            fclose(f);
        }
        return ok;
    }
}

bool pplay::Cover::extract(const std::string &url, const std::string &path, std::string *error) {
    Clock::time_point deadline = Clock::now() + std::chrono::seconds(30);
    AVFormatContext *ctx = avformat_alloc_context();
    ctx->interrupt_callback = {interrupted, &deadline};
    AVDictionary *options = nullptr;
    av_dict_set(&options, "rw_timeout", "15000000", 0);
    size_t scheme = url.find("://");
    if (scheme != std::string::npos && url.find('@') != std::string::npos
        && url.find('@') < url.find('/', scheme + 3)) {
        av_dict_set(&options, "auth_type", "basic", 0);
    }
#if defined(__PS4__) || defined(__PS5__)
    av_dict_set(&options, "ca_file", Utility::getCertificatesPath().c_str(), 0);
#endif
    int res = avformat_open_input(&ctx, url.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (res < 0) {
        char text[128];
        av_strerror(res, text, sizeof(text));
        *error = text;
        return false;
    }

    // attached pictures, else image attachments (a "cover" one first)
    const uint8_t *data = nullptr;
    int size = 0;
    bool named = false;
    for (unsigned int i = 0; i < ctx->nb_streams; i++) {
        AVStream *st = ctx->streams[i];
        if ((st->disposition & AV_DISPOSITION_ATTACHED_PIC) && st->attached_pic.size > 0) {
            AVDictionaryEntry *name = av_dict_get(st->metadata, "filename", nullptr, 0);
            bool cover = name != nullptr && std::strstr(name->value, "cover") != nullptr;
            if (data == nullptr || (cover && !named)) {
                data = st->attached_pic.data;
                size = st->attached_pic.size;
                named = cover;
            }
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT && st->codecpar->extradata_size > 0
                   && data == nullptr) {
            AVDictionaryEntry *mime = av_dict_get(st->metadata, "mimetype", nullptr, 0);
            if (mime != nullptr && std::strncmp(mime->value, "image/", 6) == 0) {
                data = st->codecpar->extradata;
                size = st->codecpar->extradata_size;
            }
        }
    }
    bool ok = data != nullptr && write(path, data, size);
    if (data == nullptr) {
        *error = "no cover";
    } else if (!ok) {
        *error = "can't write cache file";
    }
    avformat_close_input(&ctx);
    Utility::log(Utility::LogLevel::Info,
                 "Cover: " + url + (ok ? " " + std::to_string(size) + " bytes" : ": " + *error));
    return ok;
}
