// Exercises the real Mpv property/event code with headless system libmpv.
#include <chrono>
#include <thread>
#include <iostream>
#include <cassert>
#include <cstring>
#include "mpv.h"
#include "utility.h"
static int synchronousWatchedReads = 0;
extern "C" int __real_mpv_get_property(mpv_handle *, const char *, mpv_format, void *);
extern "C" int __wrap_mpv_get_property(mpv_handle *h, const char *name, mpv_format format, void *data) {
    for (const char *watched: {"pause", "idle-active", "eof-reached", "duration", "playback-time"}) {
        if (std::strcmp(name, watched) == 0) ++synchronousWatchedReads;
    }
    return __real_mpv_get_property(h, name, format, data);
}
// This test deliberately omits renderer/config/IO initialization, not property logic.
Mpv::Mpv(const std::string &, bool) {
    handle = mpv_create();
    assert(handle);
    mpv_set_option_string(handle, "vo", "null");
    mpv_set_option_string(handle, "ao", "null");
    mpv_set_option_string(handle, "keep-open", "yes");
    assert(mpv_initialize(handle) >= 0);
    watchProperties();
}
Mpv::~Mpv() { mpv_terminate_destroy(handle); }
void pplay::Utility::forgetWatchLater(const std::string &) {}
static void pump(Mpv &m, int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
        while (m.getEvent()->event_id != MPV_EVENT_NONE) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < until);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    Mpv m("", false);
    pump(m, 80);
    assert(m.isStopped());
    const char *load[] = {"loadfile", argv[1], nullptr};
    assert(mpv_command(m.getHandle(), load) >= 0);
    pump(m, 160);
    assert(!m.isStopped());
    assert(m.getDuration() > 0);
    assert(m.pause() >= 0);
    pump(m, 550);
    assert(m.isPaused());
    for (int i = 0; i < 4; ++i) {
        assert(m.seekTo(i % 2 ? 3.0 : 1.0, true) >= 0);
        pump(m, 650);
        assert(m.isPaused());
        assert(std::abs(m.getTime() - (i % 2 ? 3.0 : 1.0)) < 0.2);
    }
    // Queue multiple commands before pumping: late replies must not restore pause.
    m.pause(); m.resume(); m.pause(); m.resume();
    pump(m, 600);
    assert(!m.isPaused());
    assert(!m.isStopped());
    // Replace a playing file while reconciliation requests can still be pending.
    m.refresh();
    m.getEvent();
    assert(mpv_command(m.getHandle(), load) >= 0);
    pump(m, 650);
    assert(!m.isStopped());
    assert(!m.isAtEnd());
    assert(m.getDuration() > 0);
    assert(m.stop() >= 0);
    pump(m, 600);
    assert(m.isStopped());
    assert(synchronousWatchedReads == 0);
    std::cout << "PASS actual Mpv resync (zero synchronous watched reads): load, pause, repeated paused seeks, rapid pause/resume, stop\n";
}
