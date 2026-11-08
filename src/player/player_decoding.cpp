//
// The decoder and the video quality of the current file: from the settings at its start,
// then the player menu, or Auto: a hardware decoder given up (stuck) -> software with a
// lighter quality, retried later; drops -> a lighter quality. The settings stay as they are.
//

#include <algorithm>
#include <cmath>

#include "main.h"
#include "player.h"
#include "pplay_config.h"
#include "utility.h"

namespace {
    // Hardware decoding setting: 0 off, 1 on, 2 auto; Video quality: 0..2, 3 auto
    constexpr int DecoderAuto = 2;
    constexpr int QualityAuto = 3;
    constexpr int QualityBalanced = 1;
    constexpr int QualityFast = 2;

    // the pause of a switch (the decoder restarted, at the same position)
    constexpr float SwitchSecs = 1.5f;

    const char *hardwareDecoder() {
#ifdef __SWITCH__
        return "nvtegra-copy";
#elif defined(__ANDROID__)
        // copied back: the video drawn in the interface (a texture)
        return "mediacodec-copy";
#else
        return "auto-safe";
#endif
    }

    const char *qualityNames[] = {"Best", "Balanced", "Fast"};
}

bool Player::hasHardwareDecoding() {
#ifdef PPLAY_HW_DECODING
    return true;
#else
    return false;
#endif
}

void Player::startDecoding() {
    PPLAYConfig *config = main->getConfig();
#ifdef PPLAY_HW_DECODING
    int decoder = config->getOption(OPT_HW_DECODING)->getInteger();
#else
    int decoder = 0;
#endif
    decoderAuto = decoder == DecoderAuto;
    decoderHardware = decoder != 0;
    int quality = config->getOption(OPT_VIDEO_QUALITY)->getInteger();
    qualityAuto = quality == QualityAuto;
    // auto: the best one, a balanced one for the software decoder
    videoQuality = qualityAuto ? (decoderHardware ? 0 : QualityBalanced) : std::max(0, std::min(QualityFast, quality));
    qualityBeforeSoftware = qualityAuto ? 0 : videoQuality;
    // the Auto settings
    dropsShare = config->getOption(OPT_AUTO_DROPS)->getInteger() / 100.0;
    dropsWindow = config->getOption(OPT_AUTO_DROPS_TIME)->getInteger();
    qualityDelay = (float) config->getOption(OPT_AUTO_QUALITY_DELAY)->getInteger();
    hardwareRetrySecs = 60.0f * (float) config->getOption(OPT_AUTO_HW_RETRY)->getInteger();
    hardwareRetries = config->getOption(OPT_AUTO_HW_RETRIES)->getInteger();
    switchPauseAuto = config->getOption(OPT_AUTO_SWITCH_PAUSE)->getInteger() != 0;
    qualityHold = false;
    hardwareSeen = false;
    softwareChecks = 0;
    hardwareFallbacks = 0;
    hardwareRetryPending = false;
    switching = false;
    switchPaused = false;
    dropsStart = -1;
    dropsLastTime = -1;

    mpv->setQualityPreset(videoQuality);
#ifdef PPLAY_HW_DECODING
    // auto: given up after one error (mpv's default 3, each one after ffmpeg's timeout)
    mpv_set_property_string(mpv->getHandle(), "hwdec-software-fallback", decoderAuto ? "1" : "3");
    mpv_set_property_string(mpv->getHandle(), "hwdec", decoderHardware ? hardwareDecoder() : "no");
#endif
}

void Player::applyQuality(int quality) {
    videoQuality = quality;
    mpv->setQualityPreset(quality);
}

void Player::switchDecoding(bool hardware, int quality, const std::string &reason) {
    bool decoderChange = hardware != decoderHardware;
    bool qualityChange = quality != videoQuality;
    if (!decoderChange && !qualityChange) {
        return;
    }
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Player: decoding " + std::string(hardware ? "hardware" : "software") + ", quality "
                                + std::to_string(quality) + " (" + reason + ")");
    // paused meanwhile: not that much played without the picture (an automatic switch: if set)
    bool manual = reason == "player menu";
    if ((manual || switchPauseAuto) && !mpv->isStopped() && !mpv->isPaused()) {
        mpv->pause();
        switchPaused = true;
    }
    switching = true;
    switchClock.restart();
    dropsStart = -1;

    if (qualityChange) {
        applyQuality(quality);
    }
    decoderHardware = hardware;
    // the drops of a decoder start (and its catching up) not counted for a while
    if (decoderChange) {
        qualityHold = true;
        qualityHoldClock.restart();
    }
#ifdef PPLAY_HW_DECODING
    if (decoderChange) {
        // mpv restarts the decoder (at the same position)
        mpv_set_property_string(mpv->getHandle(), "hwdec", hardware ? hardwareDecoder() : "no");
    }
#endif
    // the decoder options of a quality: a decoder restart (done by a decoder change)
    long vid = mpv->getInt("vid", -1);
    if (!decoderChange && !mpv->isStopped() && vid > 0) {
        mpv_set_property_string(mpv->getHandle(), "vid", "no");
        mpv_set_property_string(mpv->getHandle(), "vid", std::to_string(vid).c_str());
    }

    std::string text = hardware ? "Hardware decoding" : "Software decoding";
    if (!decoderChange) {
        text = "Video quality";
    }
    main->getStatus()->show("Info", pplay::tr(text) + ": " + pplay::tr(qualityNames[quality]));
}

void Player::setHardwareDecoding(bool hardware) {
    if (!hasHardwareDecoding()) {
        return;
    }
    decoderAuto = false;
    hardwareRetryPending = false;
    int quality = videoQuality;
    if (qualityAuto) {
        if (!hardware) {
            qualityBeforeSoftware = videoQuality;
            quality = std::max(videoQuality, QualityBalanced);
        } else {
            quality = qualityBeforeSoftware;
        }
    }
    // a hardware decoder failing again: mpv's usual retries
#ifdef PPLAY_HW_DECODING
    mpv_set_property_string(mpv->getHandle(), "hwdec-software-fallback", "3");
#endif
    softwareChecks = 0;
    switchDecoding(hardware, quality, "player menu");
}

void Player::setVideoQuality(int quality) {
    qualityAuto = false;
    switchDecoding(decoderHardware, std::max(0, std::min(QualityFast, quality)), "player menu");
}

void Player::updateDecoding() {
    if (mpv->isStopped()) {
        return;
    }
    // the end of a switch: played again
    if (switching) {
        if (switchClock.getElapsedTime().asSeconds() < SwitchSecs) {
            return;
        }
        switching = false;
        if (switchPaused) {
            switchPaused = false;
            if (mpv->isPaused()) {
                mpv->resume();
            }
        }
        decodingClock.restart();
        return;
    }
    // once a second
    if (decodingClock.getElapsedTime().asSeconds() < 1) {
        return;
    }
    decodingClock.restart();
    if (!playbackStarted || !mpv->hasMovingVideo()) {
        return;
    }

#ifdef PPLAY_HW_DECODING
    if (decoderHardware) {
        // given up by mpv (an error, a stuck decoder: ffmpeg's timeout), or none for the codec
        std::string current = mpv->getString("hwdec-current");
        if (!current.empty() && current != "no") {
            hardwareSeen = true;
            softwareChecks = 0;
        } else if (current == "no" && ++softwareChecks >= 2) {
            softwareChecks = 0;
            int quality = videoQuality;
            if (qualityAuto) {
                qualityBeforeSoftware = videoQuality;
                quality = std::max(videoQuality, QualityBalanced);
            }
            if (hardwareSeen) {
                hardwareFallbacks++;
                hardwareRetryPending = decoderAuto && hardwareRetrySecs > 0 && hardwareFallbacks <= hardwareRetries;
                hardwareRetryClock.restart();
            }
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                std::string("Player: hardware decoder ")
                                        + (hardwareSeen ? "given up (" + std::to_string(hardwareFallbacks) + ")"
                                                        : "not available for this video"));
            switchDecoding(false, quality, "hardware decoder failed");
            return;
        }
    } else if (hardwareRetryPending
               && hardwareRetryClock.getElapsedTime().asSeconds()
                          >= hardwareRetrySecs * (float) (1 << std::min(hardwareFallbacks - 1, 10))) {
        hardwareRetryPending = false;
        softwareChecks = 0;
        switchDecoding(true, qualityAuto ? qualityBeforeSoftware : videoQuality, "hardware decoder retried");
        return;
    }
#endif

    // drops: the playing time only (no pause, no buffering, no seek)
    if (!qualityAuto || videoQuality >= QualityFast) {
        return;
    }
    if (qualityHold) {
        if (qualityHoldClock.getElapsedTime().asSeconds() < qualityDelay) {
            return;
        }
        qualityHold = false;
        dropsStart = -1;
    }
    double time = mpv->getTime();
    if (mpv->isPaused() || mpv->isPausedForCache() || time < 0) {
        dropsStart = -1;
        return;
    }
    long drops = mpv->getInt("frame-drop-count", 0) + mpv->getInt("decoder-frame-drop-count", 0);
    long frames = mpv->getFrameNumber();
    bool seeked = dropsLastTime >= 0 && std::fabs(time - dropsLastTime) > 3;
    dropsLastTime = time;
    if (dropsStart < 0 || seeked || frames < framesStart) {
        dropsStart = drops;
        framesStart = frames;
        dropsTime = time;
        return;
    }
    if (time - dropsTime < dropsWindow) {
        return;
    }
    // (the estimated frame number: the frames of the time played, dropped or not)
    long dropped = drops - dropsStart;
    long total = std::max(1L, frames - framesStart);
    dropsStart = -1;
    if (dropped > 0 && (double) dropped > dropsShare * (double) total) {
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
                            "Player: " + std::to_string(dropped) + " frames dropped of " + std::to_string(total));
        switchDecoding(decoderHardware, videoQuality + 1, "drops");
    }
}
