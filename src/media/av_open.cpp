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
#if defined(__PS4__) || defined(__PS5__)
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
    // static: the callback is called during avformat_close_input() too
    static thread_local Deadline deadline;
    deadline.at = Clock::now() + std::chrono::seconds(30);

    AVFormatContext *ctx = avformat_alloc_context();
    ctx->interrupt_callback = {interrupted, &deadline};
    AVDictionary *options = networkOptions(url);
    // the headers only (a few MB at most through the network)
    av_dict_set(&options, "probesize", "2000000", 0);
    av_dict_set(&options, "analyzeduration", "1000000", 0);
    int res = avformat_open_input(&ctx, url.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (res < 0) {
        *error = errorText(res);
        return nullptr;
    }
    return ctx;
}

bool pplay::AvOpen::read(const std::string &url, size_t maxSize, std::string *data, std::string *error) {
    Utility::NetworkLock lock;
    static thread_local Deadline deadline;
    deadline.at = Clock::now() + std::chrono::seconds(30);
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
