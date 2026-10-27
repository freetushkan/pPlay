//
// Image files to textures (see image.h).
//

#include <algorithm>

#include "image.h"
#include "utility.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

using namespace c2d;

namespace {

    c2d::Texture *loadFfmpeg(const std::string &path, const c2d::Vector2i &maxSize, std::string *error) {
        AVFormatContext *fmt = nullptr;
        AVCodecContext *ctx = nullptr;
        AVPacket *pkt = nullptr;
        AVFrame *frame = nullptr;
        SwsContext *sws = nullptr;
        c2d::Texture *texture = nullptr;
        bool got = false;
        int index;
        const AVCodec *codec;

        if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
            if (error) *error = "unknown image format";
            return nullptr;
        }
        avformat_find_stream_info(fmt, nullptr);
        index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (index < 0 || !(codec = avcodec_find_decoder(fmt->streams[index]->codecpar->codec_id))) {
            if (error) *error = "no image decoder";
            goto end;
        }
        if (!(ctx = avcodec_alloc_context3(codec))
            || avcodec_parameters_to_context(ctx, fmt->streams[index]->codecpar) < 0
            || avcodec_open2(ctx, codec, nullptr) < 0) {
            if (error) *error = std::string("can't open the ") + codec->name + " decoder";
            goto end;
        }

        pkt = av_packet_alloc();
        frame = av_frame_alloc();
        while (!got && av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == index && avcodec_send_packet(ctx, pkt) >= 0) {
                got = avcodec_receive_frame(ctx, frame) == 0;
            }
            av_packet_unref(pkt);
        }
        if (!got) {
            // flush (decoders with delay)
            avcodec_send_packet(ctx, nullptr);
            got = avcodec_receive_frame(ctx, frame) == 0;
        }
        if (!got || frame->width <= 0 || frame->height <= 0) {
            if (error) *error = std::string("can't decode the ") + codec->name + " image";
            goto end;
        }

        {
            // fit in maxSize, keep the aspect ratio
            float scale = std::min(1.0f, std::min((float) maxSize.x / (float) frame->width,
                                                  (float) maxSize.y / (float) frame->height));
            int w = std::max(1, (int) ((float) frame->width * scale));
            int h = std::max(1, (int) ((float) frame->height * scale));
            sws = sws_getContext(frame->width, frame->height, (AVPixelFormat) frame->format,
                                 w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws) {
                if (error) *error = "can't convert the image";
                goto end;
            }
            texture = new C2DTexture(c2d::Vector2i{w, h}, c2d::Texture::Format::RGBA8);
            uint8_t *pixels = nullptr;
            int pitch = 0;
            if (!texture->available || texture->lock(&pixels, &pitch) != 0 || pixels == nullptr) {
                delete (texture);
                texture = nullptr;
                if (error) *error = "can't create the texture";
                goto end;
            }
            uint8_t *dst[4] = {pixels, nullptr, nullptr, nullptr};
            int dstStride[4] = {pitch, 0, 0, 0};
            sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
            texture->unlock();
        }

        end:
        if (sws) sws_freeContext(sws);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (ctx) avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return texture;
    }
}

namespace pplay::Image {

    c2d::Texture *load(const std::string &path, const c2d::Vector2i &maxSize, std::string *error) {
        auto *texture = new C2DTexture(path);
        if (texture->available && texture->getTextureSize().x > 0 && texture->getTextureSize().y > 0) {
            return texture;
        }
        delete (texture);
        return loadFfmpeg(path, maxSize, error);
    }
}
