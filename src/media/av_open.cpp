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
}

AVFormatContext *pplay::AvOpen::open(const std::string &url, std::string *error) {
    // static: the callback is called during avformat_close_input() too
    static thread_local Deadline deadline;
    deadline.at = Clock::now() + std::chrono::seconds(30);

    AVFormatContext *ctx = avformat_alloc_context();
    ctx->interrupt_callback = {interrupted, &deadline};
    AVDictionary *options = nullptr;
    av_dict_set(&options, "rw_timeout", "15000000", 0);
    // the headers only (a few MB at most through the network)
    av_dict_set(&options, "probesize", "2000000", 0);
    av_dict_set(&options, "analyzeduration", "1000000", 0);
    size_t scheme = url.find("://");
    size_t at = url.find('@');
    if (scheme != std::string::npos && at != std::string::npos && at < url.find('/', scheme + 3)) {
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
        return nullptr;
    }
    return ctx;
}
