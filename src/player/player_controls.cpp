//
// Player keys and the information overlay.
//

#include <algorithm>
#include <sstream>

#include "main.h"
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

    int swap = main->getConfig()->getOption(OPT_SWAP_CONTROLS)->getInteger();
    int btnSpeedReset = swap ? c2d::Input::LT : c2d::Input::LB;
    int btnSpeedUp = swap ? c2d::Input::RT : c2d::Input::RB;
    int btnSeekBack = swap ? c2d::Input::LB : c2d::Input::LT;
    int btnSeekForward = swap ? c2d::Input::RB : c2d::Input::RT;
    float seek = main->getConfig()->getOption(OPT_SEEK_SHORT_SEC)->getFloat();

    // without the OSD: the target time as a message
    auto quietSeek = [this](float delta) {
        double duration = (double) mpv->getDuration();
        double target = std::max(0.0, mpv->getTime() + delta);
        target = duration > 0 ? std::min(target, duration) : target;
        mpv->seek(delta);
        showMessage(pplay::Utility::formatTime(target) + " / " + pplay::Utility::formatTime(duration));
    };

    // the player menus (tracks...): seeking still works (to listen again while choosing)
    bool menu = isMenuVisible();
    if (menu && !mpv->isStopped() && isFullscreen() && (keys & (btnSeekBack | btnSeekForward))) {
        quietSeek((keys & btnSeekBack) ? -seek : seek);
        return true;
    }

    if (mpv->isStopped() || main->getFiler()->isVisible() || menu) {
        return C2DObject::onInput(players);
    }

    if (keys & btnSpeedReset) {
        if (mpv->getSpeed() != 1.00) {
            setSpeed(1);
        } else {
            getMpv()->seek(-1.00f);
        }
    } else if (keys & btnSpeedUp) {
        double new_speed = mpv->getSpeed() + 0.05;
        if (new_speed <= 3) {
            setSpeed(new_speed);
        }
    } else if (keys & (btnSeekBack | btnSeekForward)) {
        osd->setVisibility(c2d::Visibility::Visible);
        getMpv()->seek(((keys & btnSeekBack) ? -seek : seek));
    } else if (keys & Input::Start) {
        setInfoVisible(!isInfoVisible());
    } else if (keys & (Input::Up | Input::Down)) {
        int delta = (keys & Input::Up) ? 1 : -1;
        if (keys & Input::Y) {
            changeBrightness(delta);
        } else {
            changeVolume(delta);
        }
    }

    if (osd->isVisible()) {
        return C2DObject::onInput(players);
    }

    // left / right: the short seek (the OSD keeps them for its buttons)
    if (keys & (Input::Left | Input::Right)) {
        quietSeek((keys & Input::Left) ? -seek : seek);
        return true;
    }

    //////////////////
    /// handle inputs
    //////////////////
    if (keys & Input::B) {
        setFullscreen(false);
    } else if (keys & Input::X) {
        main->getMenuVideo()->setVisibility(Visibility::Visible, true);
        return true;
    } else if (keys & Input::A) {
        bool paused = mpv->isPaused();
        main->getStatus()->show("Info...", paused ? "Resuming playback..." : "Pausing playback...");
        paused ? resume() : pause();
        osd->btn_play->setVisibility(paused ? Visibility::Hidden : Visibility::Visible);
        osd->btn_pause->setVisibility(paused ? Visibility::Visible : Visibility::Hidden);
    }

    if (keys && !osd->isVisible()) {
        osd->setVisibility(Visibility::Visible, true);
        main->getStatusBar()->setVisibility(Visibility::Visible, true);
    }

    return true;
}

void Player::onMediaKeys(unsigned int keys) {
    if (mpv->isStopped()) {
        return;
    }
    const float seekShort = main->getConfig()->getOption(OPT_SEEK_SHORT_SEC)->getFloat();
    const float seekLong = main->getConfig()->getOption(OPT_SEEK_LONG_SEC)->getFloat();

    if (keys & pplay::InputMap::PlayPause) {
        bool paused = mpv->isPaused();
        main->getStatus()->show("Info...", paused ? "Resuming playback..." : "Pausing playback...");
        paused ? resume() : pause();
        osd->btn_play->setVisibility(paused ? Visibility::Hidden : Visibility::Visible);
        osd->btn_pause->setVisibility(paused ? Visibility::Visible : Visibility::Hidden);
    } else if (keys & pplay::InputMap::Stop) {
        main->getStatus()->show("Info...", "Stopping playback...");
        stop();
    } else if (keys & (pplay::InputMap::FastForward | pplay::InputMap::Rewind)) {
        osd->setVisibility(Visibility::Visible, true);
        mpv->seek((keys & pplay::InputMap::Rewind) ? -seekShort : seekShort);
    } else if (keys & (pplay::InputMap::Next | pplay::InputMap::Previous)) {
        osd->setVisibility(Visibility::Visible, true);
        mpv->seek((keys & pplay::InputMap::Previous) ? -seekLong : seekLong);
    } else if (keys & (pplay::InputMap::VolumeUp | pplay::InputMap::VolumeDown)) {
        changeVolume((keys & pplay::InputMap::VolumeUp) ? 1 : -1);
    }
}

void Player::setSpeed(double speed) {
    showMessage(mpv->setSpeed(speed));
}

void Player::changeBrightness(double delta) {
    showMessage(mpv->changeBrightness(delta));
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
        int quality = std::max(0, std::min(2, (int) main->getConfig()->getOption(OPT_VIDEO_QUALITY)->getInteger()));
        out << "    " << pplay::tr("Video quality") << ": " << pplay::tr(qualities[quality]) << "\n";
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
    int swap = main->getConfig()->getOption(OPT_SWAP_CONTROLS)->getInteger();
    unsigned int seekBack = swap ? Input::LB : Input::LT;
    unsigned int seekForward = swap ? Input::RB : Input::RT;
    unsigned int speedReset = swap ? Input::LT : Input::LB;
    unsigned int speedUp = swap ? Input::RT : Input::RB;
    std::ostringstream seek;
    seek << main->getConfig()->getOption(OPT_SEEK_SHORT_SEC)->getFloat();
    auto line = [](const std::string &key, const std::string &action) { return key + ": " + action + "\n"; };
    std::string out;
    out += line(buttonName(Input::A), pplay::tr("Pause"));
    out += line(buttonName(Input::B), pplay::tr("Minimize"));
    out += line(buttonName(Input::X), pplay::tr("Menu"));
    out += line(buttonName(seekBack) + " / " + buttonName(seekForward), "-/+ " + seek.str() + " s");
    out += line(buttonName(speedReset), pplay::tr("Speed") + " 1.00");
    out += line(buttonName(speedUp), pplay::tr("Speed") + " +0.05");
    out += line(buttonName(Input::Up) + buttonName(Input::Down), pplay::tr("Volume"));
    out += line(buttonName(Input::Y) + " + " + buttonName(Input::Up) + buttonName(Input::Down),
                pplay::tr("Brightness"));
    out += line(buttonName(Input::Start), pplay::tr("Information"));
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
