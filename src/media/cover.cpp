//
// Embedded covers, read with libavformat (the headers only).
//

#include <cstdio>
#include <cstring>

#include "av_open.h"
#include "cover.h"
#include "poster_cache.h"
#include "utility.h"

std::string pplay::Cover::cacheUrl(const std::string &mediaUrl) {
    return "embedded:" + mediaUrl;
}

bool pplay::Cover::save(AVFormatContext *ctx, const std::string &mediaUrl, std::string *error) {
    // attached pictures (a "cover" one first), else image attachments
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
    if (data == nullptr) {
        *error = "no cover";
        return false;
    }
    return PosterCache::store(cacheUrl(mediaUrl), std::string((const char *) data, (size_t) size), error);
}

bool pplay::Cover::extract(const std::string &mediaUrl, std::string *error) {
    Utility::NetworkLock lock;
    AVFormatContext *ctx = AvOpen::open(mediaUrl, error);
    if (ctx == nullptr) {
        return false;
    }
    bool ok = save(ctx, mediaUrl, error);
    AvOpen::close(&ctx);
    Utility::log(Utility::LogLevel::Info, "Cover: " + mediaUrl + (ok ? "" : ": " + *error));
    return ok;
}
