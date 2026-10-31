//
// Player: end of playback (autoplay), reloads on errors / stalls, progress saving,
// mpv log.
//

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "main.h"
#include "player.h"
#include "player_osd.h"
#include "video_texture.h"
#include "encodings.h"
#include "history.h"
#include "i18n.h"
#include "filer.h"

using namespace c2d;

void Player::onMpvEvent(mpv_event *event) {
    switch (event->event_id) {
        case MPV_EVENT_START_FILE:
            pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Player::onUpdate::event MPV_EVENT_START_FILE");
            texture->clearFrame();
            texture->setWaiting(true);
            // the previous file filter may not fit (channels): set again on audio start
            if (!appliedAudioFilter.empty()) {
                const char *remove[] = {"af", "remove", "@pplaynorm", nullptr};
                mpv_command(mpv->getHandle(), remove);
            }
            appliedAudioFilter = AudioFilterUnknown;
            main->getStatus()->show("Please Wait...", pplay::tr("Loading file")
                                                    + " \"" + encoding::fix(file.name) + "\"", true);
            startPlaybackPosition = -1;
            frameDrawn = false;
            playbackStarted = false;
            playingKey = keyOf(file.path);
            main->getFiler()->refresh();
            break;
        case MPV_EVENT_FILE_LOADED:
            pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Player::onUpdate::event MPV_EVENT_FILE_LOADED");
            onLoadEvent();
            pplay::History::touch(playingKey);
            // "Loading file..." kept until the playback starts (resume position may be long)
            break;
        case MPV_EVENT_END_FILE: {
            auto *end = (mpv_event_end_file *) event->data;
            bool failed = end->reason == MPV_END_FILE_REASON_ERROR;
            pplay::Utility::log(failed ? pplay::Utility::LogLevel::Error : pplay::Utility::LogLevel::Debug,
                                std::string("Player::onUpdate::event MPV_EVENT_END_FILE")
                                        + (failed ? std::string(": ") + mpv_error_string(end->error) : ""));
            playingKey.clear();
            clearAudioScreen();
            onStopEvent(end->reason);
            main->getFiler()->refresh();
            break;
        }
        case MPV_EVENT_AUDIO_RECONFIG:
            applyAudioFilter();
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
            // the new file first frame is shown (mpv waits for it): visible again
            texture->setWaiting(false);
            if (!playbackStarted) {
                playbackStarted = true;
                main->getStatus()->hide();
            }
            break;
        case MPV_EVENT_COMMAND_REPLY:
            if (event->reply_userdata == ExternalReply + (uint64_t) externalGeneration) {
                if (event->error < 0) {
                    pplay::Utility::log(pplay::Utility::LogLevel::Error,
                                        std::string("Player: external track not loaded: ")
                                                + mpv_error_string(event->error));
                }
                if (--externalPending <= 0) {
                    externalPending = 0;
                    onExternalTracksAdded();
                }
            }
            break;
        case MPV_EVENT_LOG_MESSAGE:
            onMpvLog((mpv_event_log_message *) event->data);
            break;
        default:
            break;
    }
}

void Player::onStopEvent(int reason) {
    bool reloading = skipStopEvent && reason == MPV_END_FILE_REASON_STOP;
    skipStopEvent = false;
    if (!reloading) {
        main->getStatus()->hide();
    }
    main->getMenuVideo()->reset();
    osd->reset();

    deleteMenus(main->isExiting());

    if (main->isExiting()) {
        if (volumeDirty) {
            saveVolume();
        }
        main->setRunningStop();
        return;
    }

    if (reloading) {
        return;
    }

    long duration = mpv->getDuration();
    long position = mpv->getPosition();
    if (duration <= 0) {
        duration = lastKnownDuration;
    }
    if (position <= 0) {
        position = lastKnownPosition;
    }
    bool playbackCompleted = duration > 0 && ((double) position / duration) >= 0.98;
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Player::onStopEvent reason=" + std::to_string(reason) + " duration=" + std::to_string(duration)
                                + " position=" + std::to_string(position)
                                + " delta=" + std::to_string(duration - position)
                                + " playbackCompleted=" + std::to_string(playbackCompleted ? 1 : 0) + " isPlaylist="
                                + std::to_string(isPlaylistFile() ? 1 : 0) + " retries=" + std::to_string(retryCount));

    if (stalledStop) {
        stalledStop = false;
        main->getStatus()->show("Error...", "Playback stalled");
    }

    if (reason == MPV_END_FILE_REASON_ERROR) {
        if (scheduleReload("error", true)) {
            return;
        }
        main->getStatus()->show("Error...", "Could not load file");
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Player::onStopEvent could not load file");
    } else if (reason == MPV_END_FILE_REASON_EOF && playbackCompleted && !isPlaylistFile()) {
        const int autoplayMode = main->getConfig()->getOption(OPT_AUTOPLAY_MODE)->getInteger();
        if (autoplayMode == 2) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "Player::onStopEvent loopFile current=" + file.path);
            load(file, true, "pause=yes,start=0");
            return;
        }
        if ((autoplayMode == 1 || autoplayMode == 3) && !autoplayFiles.empty()) {
            texture->clearFrame();
            int currentIndex = -1;
            for (size_t i = 0; i < autoplayFiles.size(); i++) {
                if (autoplayFiles[i].path == file.path) {
                    currentIndex = (int) i;
                    break;
                }
            }
            if (currentIndex >= 0) {
                for (size_t idx = (size_t) currentIndex + 1; idx < autoplayFiles.size(); idx++) {
                    if (pplay::Utility::isMedia(autoplayFiles[idx])) {
                        pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                            "Player::onStopEvent autoplayNext current=" + file.path
                                                    + " next=" + autoplayFiles[idx].path);
                        load(autoplayFiles[idx], true, "pause=yes,start=0");
                        return;
                    }
                }
                if (autoplayMode == 3) {
                    if (autoplayFiles.size() == 1) {
                        load(file, true, "pause=yes,start=0");
                        return;
                    }
                    for (size_t idx = 0; idx < (size_t) currentIndex; idx++) {
                        if (pplay::Utility::isMedia(autoplayFiles[idx])) {
                            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                                "Player::onStopEvent autoplayLoop current=" + file.path
                                                        + " next=" + autoplayFiles[idx].path);
                            load(autoplayFiles[idx], true, "pause=yes,start=0");
                            return;
                        }
                    }
                }
            }
        }
    } else if (reason == MPV_END_FILE_REASON_EOF && !isPlaylistFile()) {
        if (scheduleReload("early end of file", true)) {
            return;
        }
        main->getStatus()->show("Error...", "Unexpected end of file");
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Player::onStopEvent Unexpected end of file");
    }

    if (mpv->isStopped()) {
        texture->clearFrame();
        setFullscreen(false, true);
#ifdef __SWITCH__
        if (main->getConfig()->getOption(OPT_CPU_BOOST)->getString() == "Enabled") {
            pplay::Utility::setCpuClock(pplay::Utility::CpuClock::Min);
        }
        appletSetMediaPlaybackState(false);
#endif
    }
}

bool Player::scheduleReload(const std::string &reason, bool counted) {
    int retries = main->getConfig()->getOption(OPT_NETWORK_RETRIES)->getInteger();
    if (counted) {
        if (retries != 0 && retryCount >= retries) {
            return false;
        }
        retryCount++;
    }
    reloadPending = true;
    // a sleeping source (TorrServer torrent) takes a while to wake up
    reloadDelayMs = counted ? std::min(5000 * retryCount, 30000) : 500;
    reloadPosition = lastTime;
    reloadPaused = userPaused;
    reloadClock.restart();

    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Player: reload (" + reason + ") at " + std::to_string((int) reloadPosition) + " s in "
                                + std::to_string(reloadDelayMs) + " ms, try " + std::to_string(retryCount) + "/"
                                + (retries ? std::to_string(retries) : "-"));
    std::string text = pplay::tr("Reconnecting...");
    if (counted) {
        text += " (" + std::to_string(retryCount) + (retries ? "/" + std::to_string(retries) : "") + ")";
    }
    main->getStatus()->show("Please Wait...", text, true);
    return true;
}

void Player::saveProgress() {
    // external tracks ids depend on the order they are added: not in watch later
    // (a stale id leaves the file without audio / with other subtitles)
    std::vector<Mpv::TrackEntry> tracks = mpv->getTrackList();
    for (int t = 0; t < 2; t++) {
        const std::string type = t == 0 ? "audio" : "sub";
        bool external = false;
        for (auto &track: tracks) {
            external |= track.type == type && track.selected && track.external;
        }
        if (external != watchLaterSkip[t]) {
            watchLaterSkip[t] = external;
            const char *cmd[] = {"change-list", "watch-later-options", external ? "remove" : "append",
                                 t == 0 ? "aid" : "sid", nullptr};
            mpv_command(mpv->getHandle(), cmd);
        }
    }
    mpv->save();
}

void Player::updateWatchdog() {
    double time = mpv->getTime();
    if (time < 0) {
        return;
    }
    bool paused = mpv->isPaused();
    bool cachePaused = mpv->isPausedForCache();
    double cache = mpv->getCacheDuration();
    long frame = mpv->getFrameNumber();
    bool video = mpv->hasMovingVideo();
    userPaused = paused;

    auto state = [&]() {
        char buf[192];
        snprintf(buf, sizeof(buf), "time=%.2f frame=%ld video=%d pause=%d cache-pause=%d cache=%.1fs cache-speed=%ld",
                 time, frame, video ? 1 : 0, paused ? 1 : 0, cachePaused ? 1 : 0, cache, mpv->getCacheSpeed());
        return std::string(buf);
    };
    if (watchLogClock.getElapsedTime().asSeconds() >= 5) {
        watchLogClock.restart();
        pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Player: watch " + state());
    }

    // progress: the time moves, and the frames too for a video (a stuck decoder
    // or video output can leave the time running)
    bool timeMoved = std::fabs(time - watchTime) > 0.05;
    bool frameMoved = !video || frame != watchFrame;
    if (timeMoved) {
        watchTime = time;
        lastTime = time;
    }
    watchFrame = frame;
    if (timeMoved && frameMoved) {
        watchCache = cache;
        stallClock.restart();
        if (retryCount > 0 && progressClock.getElapsedTime().asSeconds() >= 10) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "Player: playback recovered");
            retryCount = 0;
        }
        return;
    }
    if (paused && !cachePaused) {
        stallClock.restart();
        return;
    }

    // data still coming (slow source): the buffer grows. Not "cache-speed": the
    // demuxer thread updates it, it keeps its last value while a read hangs.
    if (cache > watchCache + 0.1) {
        watchCache = cache;
        stallClock.restart();
        return;
    }
    // not started yet: the first position can take long (a mkv without index (cues)
    // is read up to the resume position, through the network), a reload starts it again
    int timeout = main->getConfig()->getOption(OPT_NETWORK_TIMEOUT)->getInteger();
    float stallSec = playbackStarted ? (float) std::max(timeout * 2, 20) : (float) std::max(timeout * 10, 300);
    if (stallClock.getElapsedTime().asSeconds() >= stallSec) {
        stallClock.restart();
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Player: stalled, " + state());
        // counted: "recovered" (progress) resets the count, a file never playing ends
        if (!scheduleReload("stalled " + std::to_string((int) stallSec) + " s", true)) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "Player: stalled, no retry left");
            stalledStop = true;
            stop();
        }
    }
}

void Player::updateMpvLog() {
    auto level = (int) pplay::Utility::getLogLevel();
    if (level == mpvLogLevel) {
        return;
    }
    mpvLogLevel = level;
    // Error: errors, Info: warnings, Debug: verbose (network, streams), Trace: debug
    static const char *levels[] = {"no", "error", "warn", "v", "debug"};
    mpv_request_log_messages(mpv->getHandle(), levels[std::max(0, std::min(level, 4))]);
}

void Player::onMpvLog(const mpv_event_log_message *message) {
    std::string text = message->text ? message->text : "";
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    auto level = message->log_level <= MPV_LOG_LEVEL_ERROR  ? pplay::Utility::LogLevel::Error
                 : message->log_level <= MPV_LOG_LEVEL_WARN ? pplay::Utility::LogLevel::Info
                 : message->log_level <= MPV_LOG_LEVEL_V    ? pplay::Utility::LogLevel::Debug
                                                            : pplay::Utility::LogLevel::Trace;
    pplay::Utility::log(level, std::string("mpv[") + (message->prefix ? message->prefix : "") + "] "
                                       + (message->level ? message->level : "") + ": " + text);
}

bool Player::playSibling(int direction) {
    if (mpv->isStopped()) {
        return false;
    }
    if (isPlaylistFile()) {
        const char *cmd[] = {direction > 0 ? "playlist-next" : "playlist-prev", nullptr};
        return mpv_command(mpv->getHandle(), cmd) >= 0;
    }
    int current = -1;
    for (size_t i = 0; i < autoplayFiles.size(); i++) {
        if (autoplayFiles[i].path == file.path) {
            current = (int) i;
            break;
        }
    }
    if (current < 0) {
        return false;
    }
    for (int i = current + direction; i >= 0 && i < (int) autoplayFiles.size(); i += direction) {
        if (pplay::Utility::isMedia(autoplayFiles[i])) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "Player: " + std::string(direction > 0 ? "next" : "previous") + " "
                                        + autoplayFiles[i].path);
            MediaFile next = autoplayFiles[i];
            return load(next);
        }
    }
    showMessage(pplay::tr(direction > 0 ? "Next" : "Previous") + ": -");
    return false;
}
