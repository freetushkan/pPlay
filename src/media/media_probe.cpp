//
// Media information without playing (see media_probe.h).
//

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "av_open.h"
#include "cover.h"
#include "io.h"
#include "media_probe.h"
#include "torrserve.h"
#include "utility.h"

#define SETTLE_MS 400
// TorrServer marks the streamed files viewed
#define UNVIEW_DELAY_MS 2000

namespace {
    std::string tag(AVDictionary *metadata, const char *key) {
        AVDictionaryEntry *entry = av_dict_get(metadata, key, nullptr, 0);
        return entry != nullptr ? entry->value : "";
    }

    // mpv names: "mkv", "mp4"...
    std::string formatName(const char *name) {
        std::string format = name != nullptr ? name : "";
        format = format.substr(0, format.find(','));
        return format == "matroska" ? "mkv" : format == "mov" ? "mp4" : format;
    }

    std::string channelLayout(const AVCodecParameters *par, int *channels) {
        char text[64] = {};
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
        *channels = par->ch_layout.nb_channels;
        av_channel_layout_describe(&par->ch_layout, text, sizeof(text));
#else
        *channels = par->channels;
        av_get_channel_layout_string(text, sizeof(text), par->channels, par->channel_layout);
#endif
        return text;
    }

    MediaInfo read(AVFormatContext *ctx) {
        MediaInfo info;
        info.known = true;
        info.duration = ctx->duration > 0 ? (long) (ctx->duration / AV_TIME_BASE) : 0;
        info.format = formatName(ctx->iformat != nullptr ? ctx->iformat->name : nullptr);
        info.title = tag(ctx->metadata, "title");
        int ids[3] = {0, 0, 0};
        for (unsigned int i = 0; i < ctx->nb_streams; i++) {
            AVStream *st = ctx->streams[i];
            AVCodecParameters *par = st->codecpar;
            MediaInfo::Track t;
            t.title = tag(st->metadata, "title");
            t.language = tag(st->metadata, "language");
            t.codec = avcodec_get_name(par->codec_id);
            t.isDefault = (st->disposition & AV_DISPOSITION_DEFAULT) != 0;
            t.forced = (st->disposition & AV_DISPOSITION_FORCED) != 0;
            t.bit_rate = (long) par->bit_rate;
            if (t.bit_rate <= 0) {
                // mkv statistics tags ("BPS", "BPS-eng")
                AVDictionaryEntry *bps = av_dict_get(st->metadata, "BPS", nullptr, AV_DICT_IGNORE_SUFFIX);
                t.bit_rate = bps != nullptr ? std::atol(bps->value) : 0;
            }
            bool imageAttachment =
                    par->codec_type == AVMEDIA_TYPE_ATTACHMENT && tag(st->metadata, "mimetype").rfind("image/", 0) == 0;
            if (par->codec_type == AVMEDIA_TYPE_VIDEO || imageAttachment) {
                t.type = "video";
                t.id = ++ids[0];
                t.width = par->width;
                t.height = par->height;
                AVRational rate = st->avg_frame_rate.num > 0 ? st->avg_frame_rate : st->r_frame_rate;
                t.albumart = imageAttachment || (st->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
                t.fps = rate.num > 0 && rate.den > 0 && !t.albumart ? av_q2d(rate) : 0;
                if (t.albumart) {
                    t.title = t.title.empty() ? tag(st->metadata, "filename") : t.title;
                    t.bit_rate = 0;
                }
                info.videos.push_back(t);
            } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
                t.type = "audio";
                t.id = ++ids[1];
                t.sample_rate = par->sample_rate;
                t.layout = channelLayout(par, &t.channels);
                info.audios.push_back(t);
            } else if (par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                t.type = "sub";
                t.id = ++ids[2];
                t.bit_rate = 0;
                info.subtitles.push_back(t);
            }
        }
        return info;
    }
}

MediaProbe::~MediaProbe() {
    if (thread != nullptr) {
        thread->join();
        delete (thread);
    }
}

void MediaProbe::request(const MediaFile &file) {
    std::string url = pplay::TorrServe::toStreamUrl(file.path);
    // smb:// is opened by mpv only, playlists are text
    if (!pplay::Utility::isMedia(file) || file.mediaInfo.known || c2d::Utility::startWith(url, "smb")
        || c2d::Utility::endsWith(file.name, ".m3u", false) || c2d::Utility::endsWith(file.name, ".m3u8", false)
        || tried.count(file.path) > 0) {
        pending = false;
        return;
    }
    if (!pending || wanted.path != file.path) {
        wanted = file;
        pending = true;
        requestClock.restart();
    }
}

void MediaProbe::reset() {
    tried.clear();
}

bool MediaProbe::poll(MediaFile *file) {
    bool finished = false;
    if (thread != nullptr && probed && !delivered) {
        delivered = true;
        *file = job;
        finished = true;
    }
    if (thread != nullptr && done) {
        thread->join();
        delete (thread);
        thread = nullptr;
        *file = job;
        finished = true;
    }
    if (thread == nullptr && pending && requestClock.getElapsedTime().asMilliseconds() > SETTLE_MS) {
        pending = false;
        tried.insert(wanted.path);
        job = wanted;
        probed = done = delivered = false;
        thread = new c2d::C2DThread(run, this);
    }
    return finished;
}

int MediaProbe::run(void *data) {
    auto *self = (MediaProbe *) data;
    std::string url = pplay::TorrServe::toStreamUrl(self->job.path);
    bool torrent = pplay::Io::get()->getDeviceType(self->job.path) == pplay::Io::DeviceType::TorrServe;
    bool viewed = torrent && pplay::TorrServe::isFileViewed(self->job.path);
    std::string error;
    MediaInfo info;
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "MediaProbe: start " + url);
    {
        // one at a time with the other network threads (Switch)
        pplay::Utility::NetworkLock lock;
        AVFormatContext *ctx = pplay::AvOpen::open(url, &error);
        if (ctx != nullptr) {
            std::vector<AVDictionary *> options(ctx->nb_streams, nullptr);
            for (auto &option: options) {
                av_dict_set(&option, "threads", "1", 0);
            }
            avformat_find_stream_info(ctx, options.empty() ? nullptr : options.data());
            for (auto &option: options) {
                av_dict_free(&option);
            }
            info = read(ctx);
            if (info.hasCover()) {
                std::string coverError;
                pplay::Cover::save(ctx, url, &coverError);
            }
            avformat_close_input(&ctx);
        }
    }
    // known even on errors: not probed again (cache cleared, or played)
    info.known = true;
    info.save(self->job);
    self->job.mediaInfo = info;
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "MediaProbe: " + url + (error.empty() ? "" : ": " + error)
                                                                + " (" + std::to_string(info.videos.size()) + "v "
                                                                + std::to_string(info.audios.size()) + "a "
                                                                + std::to_string(info.subtitles.size()) + "s)");
    self->probed = true;
    if (torrent && !viewed) {
        std::this_thread::sleep_for(std::chrono::milliseconds(UNVIEW_DELAY_MS));
        pplay::TorrServe::remFileViewed(self->job.path);
    }
    self->done = true;
    return 0;
}
