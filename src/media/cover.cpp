//
// Embedded covers, read with libavformat (the headers only).
//

#include <cstdio>
#include <cstring>

#include "av_open.h"
#include "cover.h"
#include "io.h"
#include "utility.h"

std::string pplay::Cover::cachePath(const std::string &url) {
    return Utility::getDataPath() + "cache/posters/" + Utility::md5hash(url);
}

bool pplay::Cover::save(AVFormatContext *ctx, const std::string &path, std::string *error) {
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
    std::string dir = Utility::getDataPath() + "cache/posters";
    if (Io::get() != nullptr && !Io::get()->exist(dir)) {
        Io::get()->create(dir);
    }
    FILE *f = fopen(path.c_str(), "wb");
    bool ok = f != nullptr && fwrite(data, 1, (size_t) size, f) == (size_t) size;
    if (f) {
        fclose(f);
    }
    if (!ok) {
        *error = "can't write cache file";
    }
    return ok;
}

bool pplay::Cover::extract(const std::string &url, const std::string &path, std::string *error) {
    AVFormatContext *ctx = AvOpen::open(url, error);
    if (ctx == nullptr) {
        return false;
    }
    bool ok = save(ctx, path, error);
    avformat_close_input(&ctx);
    Utility::log(Utility::LogLevel::Info, "Cover: " + url + (ok ? "" : ": " + *error));
    return ok;
}
