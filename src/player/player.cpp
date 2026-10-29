//
// Created by cpasjuste on 03/10/18.
//

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include "main.h"
#include "player.h"
#include "torrserve.h"
#include "player_osd.h"
#include "paths.h"
#include "input_map.h"
#include "video_texture.h"
#include "utility.h"
#include "pplay_config.h"
#include "encodings.h"
#include "i18n.h"
#include "history.h"
#include "filer.h"
#include "io.h"
#include "menu_video_submenu.h"

#ifdef __SMB2__
#include "smb.h"
#endif

using namespace c2d;

Player::Player(Main *_main) : Rectangle(_main->getSize()) {

    main = _main;

    Vector2f pos = main->getSize();
    Player::setPosition(pos);
    Player::setOrigin(Origin::BottomRight);

    tweenScale = new TweenScale({0.25f, 0.25f}, {1.0f, 1.0f}, 0.5f);
    Player::add(tweenScale);
    tweenPosition = new TweenPosition({pos.x - 16, pos.y - 16}, {pos}, 0.5f);
    Player::add(tweenPosition);

    Player::setVisibility(Visibility::Hidden);

    mpv = new Mpv(main->getIo()->getDataPath() + "mpv", true);
    if (mpv->isAvailable()) {
        // mpv.conf languages, used when the settings ones are not set
        for (auto *lang: {&confAudioLang, &confSubLang}) {
            char *value = mpv_get_property_string(mpv->getHandle(), lang == &confAudioLang ? "alang" : "slang");
            *lang = value ? value : "";
            mpv_free(value);
        }
        // last volume, set on the next file
        int volume = main->getConfig()->getOption(OPT_VOLUME)->getInteger();
        if (volume >= 0) {
            mpv->setLastVolume(volume);
        }
    }

    texture = new VideoTexture(main, pos);
    texture->setOutlineColor(COLOR_ACCENT);
    texture->setOutlineThickness(4);
    Player::add(texture);

    osd = new PlayerOSD(main);
    Player::add(osd);

    applyConfig();

    // OSD message
    message_text = new Text("", main->getFontSize(Main::FontSize::Bigger), main->getFont());
    message_text->setFillColor(COLOR_FONT);
    message_text->setOrigin(Origin::Left);
    message_text->setOutlineColor(COLOR_GRAY_DARK);
    message_text->setOutlineThickness(5.0f);
    message_text->setPosition((20 * main->getScaling().x), (20 * main->getScaling().y));
    message_text->setVisibility(Visibility::Hidden);
    message_text->setLayer(3);
    add(message_text);

    info_text = new Text("", main->getFontSize(Main::FontSize::Medium), main->getFont());
    info_text->setFillColor(COLOR_FONT);
    info_text->setOutlineColor(COLOR_GRAY_DARK);
    info_text->setOutlineThickness(3.0f);
    info_text->setLineSpacingModifier((int) (4 * main->getScaling().y));
    info_text->setPosition((20 * main->getScaling().x), (60 * main->getScaling().y));
    info_text->setVisibility(Visibility::Hidden);
    info_text->setLayer(3);
    add(info_text);

    info_keys = new Text("", main->getFontSize(Main::FontSize::Medium), main->getFont());
    info_keys->setFillColor(COLOR_FONT);
    info_keys->setOutlineColor(COLOR_GRAY_DARK);
    info_keys->setOutlineThickness(3.0f);
    info_keys->setLineSpacingModifier((int) (4 * main->getScaling().y));
    info_keys->setOrigin(Origin::TopRight);
    info_keys->setPosition(Player::getSize().x - (20 * main->getScaling().x), (60 * main->getScaling().y));
    info_keys->setVisibility(Visibility::Hidden);
    info_keys->setLayer(3);
    add(info_keys);
}

Player::~Player() {
    if (externalThread != nullptr) {
        externalThread->join();
        delete (externalThread);
    }
    delete (mpv);
}

void Player::showMessage(const std::string &message) {
    message_text->setString(message);
    message_text->setVisibility(Visibility::Visible);
    messageClock.restart();
}

std::string Player::keyOf(const std::string &path) {
    std::string key = pplay::TorrServe::toStreamUrl(pplay::Paths::normalizeSmbPathSeparators(path));
    std::replace(key.begin(), key.end(), '\\', '/');
    while (key.size() > 1 && key.back() == '/') {
        key.pop_back();
    }
    return key;
}

bool Player::isPlaying(const std::string &path) {
    return !playingKey.empty() && !path.empty() && keyOf(path) == playingKey;
}

void Player::forgetProgress() {
    progressForgotten = true;
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Player::forgetProgress: " + playingKey);
}

bool Player::load(const MediaFile &f, bool firstTry, const std::string &options) {
    stop();
    file = f;
    std::string opts = options;
    if (!isPlaylistFile()) {
        bool existsInAutoplay = false;
        for (auto &autoplayFile: autoplayFiles) {
            if (autoplayFile.path == file.path) {
                existsInAutoplay = true;
                break;
            }
        }
        if (!existsInAutoplay) {
            autoplayFiles = main->getFiler()->getFilesSnapshot();
        }
    }

    if (firstTry) {
        retryCount = 0;
        lastTime = 0;
        userPaused = false;
        progressForgotten = false;
    }
    reloadPending = false;
    watchTime = -1;
    watchFrame = -1;
    watchCache = -1;
    stallClock.restart();
    progressClock.restart();
    lastProgressSave = 0;
    lastKnownDuration = 0;
    lastKnownPosition = 0;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                        "Player::load path=" + encoding::fix(file.path) + " name=" + encoding::fix(file.name) + " type="
                                + std::to_string((int) file.type) + " firstTry=" + std::to_string(firstTry ? 1 : 0)
                                + " retryCount=" + std::to_string(retryCount));
    std::string path = pplay::TorrServe::toStreamUrl(file.path);
#ifdef __SMB2__
    if (Utility::startWith(path, "smb://")) {
        std::replace(path.begin(), path.end(), '\\', '/');
        path.replace(0, strlen("smb://"), "smb2://");
        opts += ",hr-seek=no";
    }
#endif
    // http(s) with "user:password@": credentials sent with the first request (ffmpeg
    // waits for the 401 answer otherwise, a request more on each open / seek, or fails
    // when the server sends no challenge). Not set for all: an empty "Basic" header then.
    size_t scheme = path.find("://");
    if (scheme != std::string::npos && (Utility::startWith(path, "http://") || Utility::startWith(path, "https://"))) {
        size_t hostEnd = path.find_first_of("/?#", scheme + 3);
        size_t at = path.find('@', scheme + 3);
        if (at != std::string::npos && (hostEnd == std::string::npos || at < hostEnd)) {
            opts += std::string(opts.empty() ? "" : ",") + "stream-lavf-o-append=auth_type=basic";
        }
    }

    applyLanguages();
    // no subtitles by default without a preferred language (a watch_later choice still applies)
    char *slang = mpv_get_property_string(mpv->getHandle(), "slang");
    mpv_set_property_string(mpv->getHandle(), "sid", slang && slang[0] ? "auto" : "no");
    mpv_free(slang);

    externalGeneration++;
    externalPending = 0;
    externalAdded.clear();
    externalAll.clear();
    externalBindings.clear();
    externalSelect[0].clear();
    externalSelect[1].clear();
    hadWatchLater = pplay::Utility::isWatchLaterExist(path);

    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Player::load effective_url=" + path + " options=" + opts);
    int res = mpv->load(path, Mpv::LoadType::Replace, opts);
    if (res != 0) {
        pplay::Utility::log(pplay::Utility::LogLevel::Error, "Player::load error code=" + std::to_string(res) + " msg="
                                                                     + std::string(mpv_error_string(res)) + " opts="
                                                                     + opts + " try=" + std::to_string(retryCount));
        main->getStatus()->show("Error...", "Could not play file: " + std::string(mpv_error_string(res)));
        return false;
    }

    osd->setVisibility(c2d::Visibility::Visible);
    main->getStatusBar()->setVisibility(Visibility::Visible, true);

    return true;
}

void Player::onLoadEvent() {
    // load/update media information
    file.mediaInfo = mpv->getMediaInfo(file);
    file.mediaInfo.save(file);
    // the details panel shows it
    main->getFiler()->setMediaInfo(file, file.mediaInfo);

    buildMenus();

    mpv->restoreVolume();
    mpv->restoreSpeed();
    mpv->saveVolume();
    mpv->saveSpeed();
    resume();

    // the tracks chosen for the previous file: mpv drops them when the tracks differ,
    // and they do (the external tracks of the previous file)
    applyTrackChoices();
    startExternalTracks();
    if (externalPending == 0) {
        selectAudioIfNone();
    }
}

void Player::onUpdate() {
    updateExternalTracks();
    if (volumeDirty && volumeClock.getElapsedTime().asSeconds() >= 2) {
        saveVolume();
    }
    if (info_text->isVisible()) {
        if (mpv->isStopped()) {
            info_text->setVisibility(Visibility::Hidden);
            info_keys->setVisibility(Visibility::Hidden);
        } else if (infoClock.getElapsedTime().asMilliseconds() >= 500) {
            updateInfo();
        }
    }

    if (reloadPending && reloadClock.getElapsedTime().asMilliseconds() >= reloadDelayMs) {
        reloadPending = false;
        // the stalled file stop event is not an end of playback
        skipStopEvent = !mpv->isStopped();
        std::string opts;
        if (reloadPosition > 0) {
            opts = "start=" + std::to_string(reloadPosition);
        }
        if (reloadPaused) {
            opts += std::string(opts.empty() ? "" : ",") + "pause=yes";
        }
        load(file, false, opts);
    }

    if (mpv->isAvailable()) {
        long position = mpv->getPosition();
        long duration = mpv->getDuration();
        if (duration > 0.0) {
            lastKnownDuration = duration;
        }
        if (position > 0.0) {
            lastKnownPosition = position;
        }
        if (position > 0.0 && duration > 300 && !progressForgotten && (position - lastProgressSave) >= 10
            && (duration - position) >= 60) {
            saveProgress();
            pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                                "Player::onUpdate::saveProgress position=" + std::to_string(position)
                                        + " duration=" + std::to_string(duration));
            lastProgressSave = position;
        }
        updateMpvLog();
        // all the queued events (log messages may come by many)
        for (int count = 0; count < 128; count++) {
            mpv_event *event = mpv->getEvent();
            if (event == nullptr || event->event_id == MPV_EVENT_NONE) {
                break;
            }
            onMpvEvent(event);
        }
        if (!mpv->isStopped()) {
            if (!reloadPending) {
                updateWatchdog();
            }
            if (!frameDrawn && mpv->isAvailable()) {
                if (position > 0.0 && startPlaybackPosition == -1) {
                    startPlaybackPosition = position;
                }
                if (startPlaybackPosition != -1 && position >= (startPlaybackPosition + 0.5)) {
                    texture->drawFrame();
                    frameDrawn = true;
                }
            }
        }
    }

    if (message_text->isVisible() && messageClock.getElapsedTime().asSeconds() >= 2.0f) {
        message_text->setVisibility(Visibility::Hidden);
    }

    if (isVisible()) {
        texture->setOutlineColor(COLOR_ACCENT);
        Rectangle::onUpdate();
    }
}

void Player::pause() {
    mpv->pause();
    if (lastKnownPosition > 0 && lastKnownDuration > 300 && !progressForgotten
        && (lastKnownDuration - lastKnownPosition) >= 60) {
        saveProgress();
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
                            "Player::pause::saveProgress lastKnownPosition=" + std::to_string(lastKnownPosition)
                                    + " lastKnownDuration=" + std::to_string(lastKnownDuration));
    }
#ifdef __SWITCH__
    if (main->getConfig()->getOption(OPT_CPU_BOOST)->getString() == "Enabled") {
        pplay::Utility::setCpuClock(pplay::Utility::CpuClock::Min);
    }
    appletSetMediaPlaybackState(false);
#endif
}

void Player::resume() {
    mpv->resume();
#ifdef __SWITCH__
    if (main->getConfig()->getOption(OPT_CPU_BOOST)->getString() == "Enabled") {
        pplay::Utility::setCpuClock(pplay::Utility::CpuClock::Max);
    }
    appletSetMediaPlaybackState(true);
#endif
}

void Player::stop() {
    if (reloadPending) {
        reloadPending = false;
        main->getStatus()->hide();
    }
    mpv->stop();
}

bool Player::isFullscreen() {
    return fullscreen;
}

void Player::setFullscreen(bool fs, bool hide) {

    if (fs == fullscreen) {
        if (hide) {
            setVisibility(Visibility::Hidden, true);
        }
        return;
    }

    fullscreen = fs;

    if (!fullscreen) {
        if (hide) {
            setVisibility(Visibility::Hidden, true);
        } else {
            tweenScale->play(TweenDirection::Backward);
            tweenPosition->play(TweenDirection::Backward);
        }
        texture->showFade();
        hideMenus();
        main->getFiler()->setVisibility(Visibility::Visible, true);
        main->getStatusBar()->setVisibility(Visibility::Visible, true);
    } else {
        main->getFiler()->setVisibility(Visibility::Hidden, true);
        main->getStatusBar()->setVisibility(Visibility::Visible, true);
        setVisibility(Visibility::Visible, true);
        tweenScale->play(TweenDirection::Forward);
        tweenPosition->play(TweenDirection::Forward);
        texture->hideFade();
        osd->setVisibility(c2d::Visibility::Visible);
    }
}

const std::string &Player::getTitle() const {
    if (isPlaylistFile()) {
        displayTitle = mpv->getPlaylistCurrentTitle();
        if (displayTitle.empty()) {
            displayTitle = file.name;
        }
        displayTitle = "#" + std::to_string(mpv->getPlaylistPos() + 1) + ". " + displayTitle;
    } else {
        displayTitle = file.name;
    }
    return displayTitle;
}

bool Player::hasVideo() const {
    return !file.mediaInfo.videos.empty();
}

bool Player::isPlaylistFile() const {
    return file.name.size() >= 4
           && (file.name.compare(file.name.size() - 4, 4, ".m3u") == 0
               || file.name.compare(file.name.size() - 5, 5, ".m3u8") == 0);
}

const std::vector<MediaFile> &Player::getAutoplayFiles() const {
    return autoplayFiles;
}

PlayerOSD *Player::getOSD() {
    return osd;
}

Mpv *Player::getMpv() {
    return mpv;
}

void Player::applyConfig() {
    applyLanguages();
    applyAudioFilter();
    const int timeout = main->getConfig()->getOption(OPT_NETWORK_TIMEOUT)->getInteger();
#ifdef __SMB2__
    pplay::Smb::configureStream(main->getConfig()->getOption(OPT_SMB_READ_BUFFER_MB)->getInteger(), timeout);
#endif
    mpv_command_string(mpv->getHandle(), ("set network-timeout " + std::to_string(timeout)).c_str());
    mpv->setCache(main->getConfig()->getOption(OPT_CACHE_SECS)->getInteger(),
                  main->getConfig()->getOption(OPT_CACHE_MB)->getInteger());
    const int quality = main->getConfig()->getOption(OPT_VIDEO_QUALITY)->getInteger();
    if (quality != appliedQuality) {
        const bool restart = appliedQuality >= 0;
        appliedQuality = quality;
        mpv->setQualityPreset(quality);
        // decoder options: restart the video decoder of the playing file
        long vid = mpv->getInt("vid", -1);
        if (restart && !mpv->isStopped() && vid > 0) {
            mpv_set_property_string(mpv->getHandle(), "vid", "no");
            mpv_set_property_string(mpv->getHandle(), "vid", std::to_string(vid).c_str());
        }
    }
    texture->setOutlineColor(COLOR_ACCENT);
}
