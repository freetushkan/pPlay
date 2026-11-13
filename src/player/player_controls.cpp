//
// Player keys and the information overlay.
//

#include <algorithm>
#include <cmath>
#include <sstream>

#include "main.h"
#include "platform.h"
#include "player.h"
#include "player_osd.h"
#include "video_texture.h"
#include "encodings.h"
#include "input_map.h"
#include "i18n.h"

using namespace c2d;

bool Player::onInput(c2d::Input::Player *players) {
    unsigned int keys = players[0].buttons;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Player::onInput keys=" + pplay::Utility::getKeysString(keys));

    if (reloadPending && mpv->isStopped() && (keys & Input::B)) {
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Player: reload cancelled");
        stop();
        texture->clearFrame();
        setFullscreen(false, true);
        return true;
    }

    // the bumpers and triggers: 0 normal, 1 swapped, 2 off
    int shoulders = shoulderMode();
    bool swap = shoulders == 1;
    int btnSpeedReset = shoulders == 2 ? 0 : swap ? c2d::Input::LT : c2d::Input::LB;
    int btnSpeedUp = shoulders == 2 ? 0 : swap ? c2d::Input::RT : c2d::Input::RB;
    int btnSeekBack = shoulders == 2 ? 0 : swap ? c2d::Input::LB : c2d::Input::LT;
    int btnSeekForward = shoulders == 2 ? 0 : swap ? c2d::Input::RB : c2d::Input::RT;

    // the menus (tracks, settings...): seeking still works (to listen again while choosing)
    bool menu = isMenuVisible();
    bool mainMenu = main->getMenuMain()->isMenuVisible();
    if (((menu && isFullscreen()) || mainMenu) && !mpv->isStopped() && (keys & (btnSeekBack | btnSeekForward))) {
        scrub((keys & btnSeekBack) ? -1 : 1);
        return true;
    }

    // loading: Back minimizes, other keys show the OSD
    if (isFullscreen() && isLoading() && !menu && !mainMenu && !main->getFiler()->isVisible()) {
        if ((keys & Input::B) && !osd->isExpanded()) {
            setFullscreen(false);
            return true;
        }
        loadingOsd = true;
    }

    // minimized: the OSD keeps nothing
    if (mpv->isStopped() || main->getFiler()->isVisible() || menu || mainMenu) {
        return isFullscreen() && !mainMenu && C2DObject::onInput(players);
    }

    if (btnSpeedReset && (keys & btnSpeedReset)) {
        if (mpv->getSpeed() != 1.00) {
            setSpeed(1);
        } else {
            getMpv()->seek(-1.00f);
        }
    } else if (btnSpeedUp && (keys & btnSpeedUp)) {
        double new_speed = mpv->getSpeed() + 0.05;
        if (new_speed <= 3) {
            setSpeed(new_speed);
        }
    } else if (keys & (btnSeekBack | btnSeekForward)) {
        scrub((keys & btnSeekBack) ? -1 : 1);
        return true;
    } else if (keys & Input::Start) {
        setInfoVisible(!isInfoVisible());
    } else if (keys & (Input::Up | Input::Down)) {
        int delta = (keys & Input::Up) ? 1 : -1;
        if (keys & Input::Y) {
            // the screen backlight if any (Switch handheld), else the picture
            if (!changeBacklight(delta)) {
                changeBrightness(delta);
            }
        } else if (!osd->isExpanded()) {
            // the first press shows the panel only
            osd->expand();
            return true;
        } else {
            changeVolume(delta);
        }
    }

    if (osd->isExpanded()) {
        return C2DObject::onInput(players);
    }

    // left / right: the seek (the OSD keeps them for its buttons)
    if (keys & (Input::Left | Input::Right)) {
        scrub((keys & Input::Left) ? -1 : 1);
        return true;
    }

    if (keys & Input::B) {
        setFullscreen(false);
        return true;
    } else if (keys & Input::X) {
        main->getMenuVideo()->setVisibility(Visibility::Visible, true);
        return true;
    } else if (keys & Input::A) {
        bool paused = mpv->isPaused();
        main->getStatus()->show("Info", paused ? "Resuming playback" : "Pausing playback");
        paused ? resume() : pause();
        osd->setPaused(!paused);
    }

    if (keys && !osd->isExpanded()) {
        osd->expand();
    }

    return true;
}

int Player::shoulderMode() {
#if defined(__ANDROID__) || defined(__LINUX__)
    // no setting there: the default
    return 0;
#else
    return main->getConfig()->getOption(OPT_SWAP_CONTROLS)->getInteger();
#endif
}

void Player::scrub(int direction) {
    double duration = (double) mpv->getDuration();
    double time = mpv->getTime();
    // a new one: another direction, or a pause since the last key
    if (!scrubbing || direction != scrubDirection || scrubLast.getElapsedTime().asSeconds() > 0.6f) {
        scrubbing = true;
        scrubDirection = direction;
        scrubTarget = std::max(0.0, time);
        scrubHeld.restart();
    }
    float held = scrubHeld.getElapsedTime().asSeconds();
    // a tap: about 0.5 % of the file (5..15 s); held: growing; at most a sixth of what is left that way
    double base = duration > 0 ? std::max(5.0, std::min(15.0, duration * 0.005)) : 10.0;
    double left = direction > 0 ? (duration > 0 ? duration - scrubTarget : 600.0) : scrubTarget;
    double step = std::min(base * (1.0 + 0.6 * held * held), std::max(base, left / 6));
    scrubTarget += direction * step;
    scrubTarget = std::max(0.0, duration > 0 ? std::min(scrubTarget, duration - 1) : scrubTarget);
    scrubPending = true;
    scrubLast.restart();
    osd->peek();
    main->getStatus()->show("Info", pplay::Utility::formatTime(scrubTarget) + " / " + pplay::Utility::formatTime(duration));
}

void Player::updateScrub() {
    if (!scrubbing || mpv->isStopped()) {
        scrubbing = false;
        return;
    }
    // one seek at a time (each shows its frame), the last one exact
    if (mpv->isSeeking()) {
        return;
    }
    bool done = scrubLast.getElapsedTime().asSeconds() > 0.4f;
    if (scrubPending || done) {
        mpv->seekTo(scrubTarget, done);
        scrubPending = false;
    }
    if (done) {
        scrubbing = false;
    }
}

void Player::onMediaKeys(unsigned int keys) {
    if (mpv->isStopped()) {
        return;
    }

    if (keys & pplay::InputMap::PlayPause) {
        bool paused = mpv->isPaused();
        main->getStatus()->show("Info", paused ? "Resuming playback" : "Pausing playback");
        paused ? resume() : pause();
        osd->setPaused(!paused);
    } else if (keys & pplay::InputMap::Stop) {
        main->getStatus()->show("Info", "Stopping playback");
        stop();
    } else if (keys & (pplay::InputMap::FastForward | pplay::InputMap::Rewind)) {
        scrub((keys & pplay::InputMap::Rewind) ? -1 : 1);
    } else if (keys & (pplay::InputMap::Next | pplay::InputMap::Previous)) {
        osd->peek();
        playSibling((keys & pplay::InputMap::Previous) ? -1 : 1);
    } else if (keys & (pplay::InputMap::VolumeUp | pplay::InputMap::VolumeDown)) {
        changeVolume((keys & pplay::InputMap::VolumeUp) ? 1 : -1);
    }
}

void Player::setSpeed(double speed) {
    main->getStatus()->show("Info", pplay::tr("Speed") + ": " + mpv->setSpeed(speed));
}

bool Player::changeBacklight(int direction) {
    float light = pplay::Platform::backlight();
    if (light < 0) {
        return false;
    }
    light = std::max(0.0f, std::min(1.0f, std::round(light * 20 + (float) direction) / 20));
    pplay::Platform::setBacklight(light);
    main->getStatus()->show("Info", pplay::tr("Screen brightness") + ": " + std::to_string((int) std::lround(light * 100)) + "%");
    return true;
}

void Player::changeBrightness(double delta) {
    main->getStatus()->show("Info", pplay::tr("Brightness") + ": " + mpv->changeBrightness(delta));
}

void Player::updateInfo() {
    infoClock.restart();
    std::ostringstream out;
    out.setf(std::ios::fixed);
    auto bitrate = [](double bps) {
        std::ostringstream b;
        b.setf(std::ios::fixed);
        if (bps >= 1000000) {
            b.precision(1);
            b << bps / 1000000 << " Mb/s";
        } else {
            b.precision(0);
            b << bps / 1000 << " kb/s";
        }
        return b.str();
    };

    out << encoding::fix(file.name) << "\n";
    out << pplay::tr("State") << ": " << stateText() << "\n";
    if (isSavedCopy()) {
        out << pplay::tr("Source") << ": " << pplay::tr("Saved copy") << "\n";
    }
    std::string vcodec = mpv->getString("current-tracks/video/codec");
    if (!vcodec.empty() && hasVideo()) {
        out.precision(3);
        out << pplay::tr("Video") << ": " << vcodec << " " << mpv->getInt("video-params/w", 0) << "x"
            << mpv->getInt("video-params/h", 0);
        double fps = mpv->getDouble("container-fps", 0);
        if (fps > 0) {
            out << " " << fps << " fps";
        }
        std::string hwdec = mpv->getString("hwdec-current");
        out << ", hwdec: " << (hwdec.empty() ? "no" : hwdec);
        double vbit = mpv->getDouble("video-bitrate", 0);
        if (vbit > 0) {
            out << ", " << bitrate(vbit);
        }
        out << "\n    " << pplay::tr("Dropped frames") << ": " << mpv->getInt("frame-drop-count", 0) << " / "
            << mpv->getInt("decoder-frame-drop-count", 0) << ", " << pplay::tr("Render") << ": " << mpv->getOsdWidth()
            << "x" << mpv->getOsdHeight() << "\n";
        static const char *qualities[] = {"Best", "Balanced", "Fast"};
        // the current file's one (the setting: its start)
        out << "    " << pplay::tr("Video quality") << ": "
            << (isQualityAuto() ? pplay::tr("Auto") + " · " : "") << pplay::tr(qualities[getVideoQuality()]) << "\n";
    }
    std::string acodec = mpv->getString("current-tracks/audio/codec");
    if (!acodec.empty()) {
        out.precision(0);
        out << pplay::tr("Audio") << ": " << acodec << " " << mpv->getString("audio-params/channels") << " "
            << mpv->getInt("audio-params/samplerate", 0) / 1000.0 << " kHz -> "
            << mpv->getString("audio-out-params/channels");
        double abit = mpv->getDouble("audio-bitrate", 0);
        if (abit > 0) {
            out << ", " << bitrate(abit);
        }
        static const char *modes[] = {"Off", "Normalize", "Night mode"};
        int mode = std::max(0, std::min(2, main->getConfig()->getOption(OPT_AUDIO_NORMALIZE)->getInteger()));
        out << "\n    " << pplay::tr("Track") << ": " << trackInfo("audio") << "\n    " << pplay::tr("Volume") << ": "
            << (int) mpv->getDouble("volume", 0) << "%, " << pplay::tr("Normalization") << ": "
            << pplay::tr(modes[mode]) << "\n";
    }
    out << pplay::tr("Subtitles") << ": " << trackInfo("sub") << "\n";
    out.precision(1);
    out << pplay::tr("Cache") << ": " << std::max(0.0, mpv->getDouble("demuxer-cache-duration", 0)) << " s, "
        << mpv->getCacheSpeed() / 1048576.0 << " MB/s, " << pplay::tr("Speed") << ": ";
    out.precision(2);
    out << mpv->getSpeed();
    info_text->setString(out.str());
}

std::string Player::stateText() {
    auto yes = [this](const char *name) { return mpv->getString(name) == "yes"; };
    if (mpv->isStopped() || yes("idle-active")) {
        return pplay::tr("Idle");
    }
    if (yes("eof-reached")) {
        return pplay::tr("Ended");
    }
    if (yes("seeking")) {
        return pplay::tr("Seeking");
    }
    if (yes("paused-for-cache")) {
        long percent = mpv->getInt("cache-buffering-state", -1);
        return pplay::tr("Buffering") + (percent >= 0 ? " " + std::to_string(percent) + "%" : "");
    }
    if (yes("pause")) {
        return pplay::tr("Paused");
    }
    // not paused, nothing played yet: opening, waiting for data
    if (yes("core-idle")) {
        return pplay::tr("Loading");
    }
    return pplay::tr("Playing");
}

std::string Player::trackInfo(const std::string &type) {
    const std::string prefix = "current-tracks/" + type + "/";
    long id = mpv->getInt((prefix + "id").c_str(), -1);
    if (id < 0) {
        return pplay::tr("Off");
    }
    std::string out = "#" + std::to_string(id);
    std::string lang = mpv->getString((prefix + "lang").c_str());
    std::string title = mpv->getString((prefix + "title").c_str());
    if (!lang.empty()) {
        out += " " + lang;
    }
    if (!title.empty()) {
        out += " \u00AB" + encoding::fix(title) + "\u00BB";
    }
    if (mpv->getString((prefix + "external").c_str()) == "yes") {
        out += " (" + pplay::tr("external") + ")";
    }
    return out;
}

void Player::updateKeysInfo() {
    using pplay::InputMap::buttonName;
    int shoulders = shoulderMode();
    bool swap = shoulders == 1;
    unsigned int seekBack = swap ? Input::LB : Input::LT;
    unsigned int seekForward = swap ? Input::RB : Input::RT;
    unsigned int speedReset = swap ? Input::LT : Input::LB;
    unsigned int speedUp = swap ? Input::RT : Input::RB;
    std::string seek = pplay::tr("Seek");
    auto line = [](const std::string &key, const std::string &action) { return key + ": " + action + "\n"; };
    std::string out = "\n";
#ifdef __ANDROID__
    // a TV remote: its keys (and the media ones), the rest in the player menu
    (void) seekBack, (void) seekForward, (void) speedReset, (void) speedUp, (void) shoulders;
    out += line(buttonName(Input::A), pplay::tr("Select"));
    out += line(buttonName(Input::B), pplay::tr("Back/Minimize"));
    out += line(buttonName(Input::X), pplay::tr("Player menu"));
    out += line(buttonName(Input::Left) + " / " + buttonName(Input::Right), seek);
    out += line(buttonName(Input::Up) + buttonName(Input::Down), pplay::tr("Volume"));
    out += line("\u23EF", pplay::tr("Play/Pause"));
    out += line("\u23EA / \u23E9", seek);
    out += line("\u23EE / \u23ED", pplay::tr("Previous") + " / " + pplay::tr("Next"));
#else
    out += line(buttonName(Input::A), pplay::tr("Select"));
    out += line(buttonName(Input::B), pplay::tr("Back/Minimize"));
    out += line(buttonName(Input::X), pplay::tr("Player menu"));
    out += line(buttonName(Input::Left) + " / " + buttonName(Input::Right), seek);
    if (shoulders != 2) {
        out += line(buttonName(seekBack) + " / " + buttonName(seekForward), seek);
        out += line(buttonName(speedReset), pplay::tr("Speed") + " 1.00");
        out += line(buttonName(speedUp), pplay::tr("Speed") + " +0.05");
    }
    out += line(buttonName(Input::Up) + buttonName(Input::Down), pplay::tr("Volume"));
    out += line(buttonName(Input::Y) + " + " + buttonName(Input::Up) + buttonName(Input::Down),
                pplay::tr(pplay::Platform::backlight() >= 0 ? "Screen brightness" : "Brightness"));
    out += line(buttonName(Input::Start), pplay::tr("Information"));
#endif
    out.pop_back();
    info_keys->setString(out);
}

bool Player::isInfoVisible() {
    return info_text->isVisible();
}

void Player::setInfoVisible(bool visible) {
    if (visible) {
        updateInfo();
        updateKeysInfo();
    }
    info_text->setVisibility(visible ? Visibility::Visible : Visibility::Hidden);
    info_keys->setVisibility(visible ? Visibility::Visible : Visibility::Hidden);
}

void Player::resetBrightness() {
    double brightness = mpv->getDouble("brightness", 0);
    if (brightness != 0) {
        changeBrightness(-brightness);
    }
}
