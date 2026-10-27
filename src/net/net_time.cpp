//
// System clock check (see net_time.h).
//

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "net_time.h"
#include "net_http.h"
#include "i18n.h"
#include "utility.h"

namespace {

    // plain HTTP, small answers with a "Date" header
    const char *g_urls[] = {
            "http://connectivitycheck.gstatic.com/generate_204",
            "http://www.msftconnecttest.com/connecttest.txt",
            "http://captive.apple.com/hotspot-detect.html",
    };

    c2d::C2DThread *g_thread = nullptr;
    std::atomic<bool> g_mismatch{false};
    time_t g_local = 0;
    time_t g_network = 0;

    int checkThread(void *) {
        for (const char *url: g_urls) {
            pplay::Http::Request request;
            request.url = url;
            request.timeout = 5;
            request.maxSize = 64 * 1024;
            time_t before = time(nullptr);
            pplay::Http::Response response = pplay::Http::perform(request);
            time_t after = time(nullptr);
            if (response.date <= 0) {
                continue;
            }
            // local time at the middle of the request
            time_t local = before + (after - before) / 2;
            long long drift = (long long) local - (long long) response.date;
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "Clock: system " + std::to_string((long long) local) + ", " + url + " "
                                + std::to_string((long long) response.date) + ", drift "
                                + std::to_string(drift) + " s");
            if (llabs(drift) > CLOCK_MAX_DRIFT) {
                g_local = local;
                g_network = response.date;
                g_mismatch = true;
            }
            return 0;
        }
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Clock: no network time, not checked");
        return 0;
    }

    std::string formatTime(time_t t) {
        struct tm tm{};
        localtime_r(&t, &tm);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
        return buf;
    }

    std::string formatDrift(long long seconds) {
        seconds = llabs(seconds);
        long long days = seconds / 86400;
        long long hours = seconds % 86400 / 3600;
        long long minutes = seconds % 3600 / 60;
        std::vector<std::string> parts;
        if (days > 0) {
            parts.push_back(std::to_string(days) + pplay::tr("d"));
        }
        if (hours > 0) {
            parts.push_back(std::to_string(hours) + pplay::tr("h"));
        }
        if (days == 0 && (minutes > 0 || parts.empty())) {
            parts.push_back(std::to_string(minutes) + pplay::tr("min"));
        }
        std::string out;
        for (size_t i = 0; i < parts.size() && i < 2; i++) {
            out += (i > 0 ? " " : "") + parts[i];
        }
        return out;
    }
}

namespace pplay::NetTime {

    void start() {
        if (g_thread == nullptr) {
            g_thread = new c2d::C2DThread(checkThread, nullptr);
        }
    }

    void stop() {
        if (g_thread != nullptr) {
            g_thread->join();
            delete (g_thread);
            g_thread = nullptr;
        }
    }

    bool takeWarning(std::string *message) {
        if (!g_mismatch.exchange(false)) {
            return false;
        }
        *message = pplay::tr("The console clock is off by") + " "
                   + formatDrift((long long) g_local - (long long) g_network) + ".\n"
                   + pplay::tr("Console") + ": " + formatTime(g_local) + "\n"
                   + pplay::tr("Network") + ": " + formatTime(g_network) + "\n"
                   + pplay::tr("Secure connections (HTTPS) may fail: check the date, time and time zone in the system settings.");
        return true;
    }
}
