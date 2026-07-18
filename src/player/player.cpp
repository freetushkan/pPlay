//
// Created by cpasjuste on 03/10/18.
//

#include <sstream>
#include "main.h"
#include "player.h"
#include "torrserve.h"
#include "player_osd.h"
#include "video_texture.h"
#include "utility.h"
#include "pplay_config.h"
#include "encodings.h"
#include "menu_video_submenu.h"

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

#ifdef __SMB2__
    configure_smb_mpv(
        main->getConfig()->getOption(OPT_SMB_READ_BUFFER_MB)->getInteger(),
        main->getConfig()->getOption(OPT_NETWORK_TIMEOUT)->getInteger()
    );
#endif

    mpv = new Mpv(main->getIo()->getDataPath() + "mpv", true);
    mpv_command_string(mpv->getHandle(), ("set network-timeout " +
            std::to_string(main->getConfig()->getOption(OPT_NETWORK_TIMEOUT)->getInteger())).c_str());

#ifndef FULL_TEXTURE_TEST
    texture = new VideoTexture(main, pos);
    texture->setOutlineColor(COLOR_ACCENT);
    texture->setOutlineThickness(4);
#else
    texture = new VideoTexture(main, {64, 64});
    texture->setOrigin(Origin::Center);
    texture->setPosition(Vector2f(Player::getSize().x / 2, Player::getSize().y / 2));
#endif
    Player::add(texture);

    osd = new PlayerOSD(main);
    Player::add(osd);

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
}

Player::~Player() {
    delete (mpv);
}

void Player::showMessage(const std::string &message) {
    message_text->setString(message);
    message_text->setVisibility(Visibility::Visible);
    messageClock.restart();
}

bool Player::load(const MediaFile &f, bool firstTry, const std::string &options) {
    stop();
#ifdef __PS4__
    pausedHttpsStream = false;
#endif
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

    if (firstTry) retryCount = 0;
    lastProgressSave = 0;
    lastKnownDuration = 0;
    lastKnownPosition = 0;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "Player::load path=" + encoding::fix(file.path)
        + " name=" + encoding::fix(file.name)
        + " type=" + std::to_string((int) file.type)
        + " firstTry=" + std::to_string(firstTry ? 1 : 0)
        + " retryCount=" + std::to_string(retryCount));
    std::string path = pplay::TorrServe::toStreamUrl(file.path);
#ifdef __SMB2__
    if (Utility::startWith(path, "smb://")) {
        std::replace(path.begin(), path.end(), '\\', '/');
        path.replace(0, strlen("smb://"), "smb2://");
        opts += ",hr-seek=no";
    }
#endif

    // disable subtitles if slang option not set in "mpv.conf" (overridden by "watch_later")
    char *slang = mpv_get_property_string(mpv->getHandle(), "slang");
    if (strlen(slang) <= 0) {
        printf("slang not set, disabling subtitles by default");
        mpv_set_option_string(mpv->getHandle(), "sid", "no");
    }

    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "Player::load effective_url=" + path);
    int res = mpv->load(path, Mpv::LoadType::Replace, opts);
    if (res != 0) {
        pplay::Utility::log(pplay::Utility::LogLevel::Error,
            "Player::load error code=" + std::to_string(res)
            + " msg=" + std::string(mpv_error_string(res))
            + " opts=" + opts
            + " try=" + std::to_string(retryCount));
        main->getStatus()->show("Error...", "Could not play file: "
            + std::string(mpv_error_string(res)));
        printf("Player::load: could not play file: %s\n", mpv_error_string(res));
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

    // TODO: is this really needed as it should be extracted from scrapper
    // main->getFiler()->setMediaInfo(file, file.mediaInfo);

    // build video track selection menu
    if (!file.mediaInfo.videos.empty()) {
        // videos menu options
        std::vector<MenuItem> items;
        for (auto &stream: file.mediaInfo.videos) {
            items.emplace_back(stream.title + "\n" + stream.language + " " + stream.codec + " "
                               + std::to_string(stream.width) + "x" + std::to_string(stream.height),
                               "", MenuItem::Position::Top, stream.id);
        }
        menuVideoStreams = new MenuVideoSubmenu(
                main, main->getMenuVideo()->getGlobalBounds(), "Video", items, MENU_VIDEO_TYPE_VID);
        menuVideoStreams->setVisibility(Visibility::Hidden, false);
        menuVideoStreams->setLayer(3);
        add(menuVideoStreams);
    }

    // build audio track selection menu
    if (!file.mediaInfo.audios.empty()) {
        // audios menu options
        std::vector<MenuItem> items;
        for (auto &stream: file.mediaInfo.audios) {
            items.emplace_back(stream.title + "\n" + stream.language + " "
                               + stream.codec + " " + std::to_string(stream.channels) + "ch "
                               + std::to_string(stream.sample_rate / 1000) + " Khz",
                               "", MenuItem::Position::Top, stream.id);
        }
        menuAudioStreams = new MenuVideoSubmenu(
                main, main->getMenuVideo()->getGlobalBounds(), "Audio", items, MENU_VIDEO_TYPE_AUD);
        menuAudioStreams->setVisibility(Visibility::Hidden, false);
        menuAudioStreams->setLayer(3);
        add(menuAudioStreams);
    }

    // build subtitles track selection menu
    if (!file.mediaInfo.subtitles.empty()) {
        // subtitles menu options
        std::vector<MenuItem> items;
        items.emplace_back("None", "", MenuItem::Position::Top, -1);
        for (auto &stream: file.mediaInfo.subtitles) {
            items.emplace_back(stream.title + "\nLang: " + stream.language, "", MenuItem::Position::Top, stream.id);
        }
        menuSubtitlesStreams = new MenuVideoSubmenu(
                main, main->getMenuVideo()->getGlobalBounds(), "Subtitles", items, MENU_VIDEO_TYPE_SUB);
        menuSubtitlesStreams->setVisibility(Visibility::Hidden, false);
        menuSubtitlesStreams->setLayer(3);
        add(menuSubtitlesStreams);
    }

    std::vector<MenuItem> playlistItems;
    if (isPlaylistFile()) {
        for (auto &entry: mpv->getPlaylistItems()) {
            std::string name = std::to_string(entry.first + 1) + ". " + entry.second;
            playlistItems.emplace_back(name, "", MenuItem::Position::Top, entry.first);
        }
    } else {
        for (size_t i = 0; i < autoplayFiles.size(); i++) {
            auto &item = autoplayFiles[i];
            if (!pplay::Utility::isMedia(item)) continue;
            std::string name = std::to_string(i + 1) + ". " + encoding::fix(item.name);
            playlistItems.emplace_back(name, "", MenuItem::Position::Top, (int)i, true, item.name);
        }
    }
    if (!playlistItems.empty()) {
        menuPlaylist = new MenuVideoSubmenu(
            main, main->getMenuVideo()->getGlobalBounds(), "Playlist", playlistItems, MENU_VIDEO_TYPE_PLAYLIST);
        menuPlaylist->setVisibility(Visibility::Hidden, false);
        menuPlaylist->setLayer(3);
        add(menuPlaylist);
    }

    if (menuPlaybackMode == nullptr) {
        menuPlaybackMode = new MenuVideoSubmenu(
            main, main->getMenuVideo()->getGlobalBounds(), "At playback end..",
            {MenuItem("Stop", "", MenuItem::Position::Top, 0),
             MenuItem("Play next", "", MenuItem::Position::Top, 1),
             MenuItem("Loop file", "", MenuItem::Position::Top, 2),
             MenuItem("Loop directory", "", MenuItem::Position::Top, 3)},
            MENU_VIDEO_TYPE_PL_MODE);
        menuPlaybackMode->setLayer(3);
        menuPlaybackMode->setVisibility(Visibility::Hidden, false);
        add(menuPlaybackMode);
    }

#ifdef FULL_TEXTURE_TEST
    texture->resize({file.mediaInfo.videos.at(0).width,
                     file.mediaInfo.videos.at(0).height});
    float scaling = std::min(
            getSize().x / texture->getSize().x,
            getSize().y / texture->getSize().y);
    texture->setScale(scaling, scaling);
#endif

    mpv->restoreVolume();
    mpv->restoreSpeed();
    mpv->saveVolume();
    mpv->saveSpeed();
    resume();
}

void Player::onStopEvent(int reason) {
    main->getStatus()->hide();
    main->getMenuVideo()->reset();
    osd->reset();

    if (menuAudioStreams != nullptr) {
        delete (menuAudioStreams);
        menuAudioStreams = nullptr;
    }
    if (menuVideoStreams != nullptr) {
        delete (menuVideoStreams);
        menuVideoStreams = nullptr;
    }
    if (menuSubtitlesStreams != nullptr) {
        delete (menuSubtitlesStreams);
        menuSubtitlesStreams = nullptr;
    }
    if (menuPlaylist != nullptr) {
        delete (menuPlaylist);
        menuPlaylist = nullptr;
    }

    if (main->isExiting()) {
        if (menuPlaybackMode != nullptr) {
            delete (menuPlaybackMode);
            menuPlaybackMode = nullptr;
        }
        main->setRunningStop();
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
    bool playbackCompleted = duration > 0 && ((double)position / duration) >= 0.98;
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
        "Player::onStopEvent reason=" + std::to_string(reason)
        + " duration=" + std::to_string(duration)
        + " position=" + std::to_string(position)
        + " delta=" + std::to_string(duration - position)
        + " playbackCompleted=" + std::to_string(playbackCompleted ? 1 : 0)
        + " isPlaylist=" + std::to_string(isPlaylistFile() ? 1 : 0)
        + " retries=" + std::to_string(retryCount));

    if (reason == MPV_END_FILE_REASON_ERROR) {
        int retries = main->getConfig()->getOption(OPT_NETWORK_RETRIES)->getInteger();
        if (retries == 0 || retryCount < retries) {
            retryCount++;
            if (load(file, false)) {
                return;
            }
        }
        main->getStatus()->show("Error...", "Could not load file");
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
            "Player::onStopEvent could not load file");
        printf("Player::load: could not load file\n");
    } else if (reason == MPV_END_FILE_REASON_EOF && playbackCompleted && !isPlaylistFile()) {
        const int autoplayMode = main->getConfig()->getOption(OPT_AUTOPLAY_MODE)->getInteger();
        if (autoplayMode == 2) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                "Player::onStopEvent loopFile current=" + file.path);
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
        int retries = main->getConfig()->getOption(OPT_NETWORK_RETRIES)->getInteger();
        if (retries == 0 || retryCount < retries) {
            retryCount++;
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "Player::onStopEvent earlyEOF");
            std::string opts = "pause=yes,start=" + std::to_string(lastKnownPosition);
            if (load(file, false, opts)) {
                retryCount = 0;
                return;
            }
        }
        main->getStatus()->show("Error...", "Unexpected end of file");
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
            "Player::onStopEvent Unexpected end of file");
    }

    if (mpv->isStopped()) {
        texture->clearFrame();
        setFullscreen(false, true);
#ifdef __SWITCH__
        pplay::Utility::setCpuClock(pplay::Utility::CpuClock::Min);
        appletSetMediaPlaybackState(false);
#endif
    }
}

void Player::onUpdate() {
    //TODO: cache-buffering-state
    if (mpv->isAvailable()) {
        long position = mpv->getPosition();
        long duration = mpv->getDuration();
        if (duration > 0.0) {
            lastKnownDuration = duration;
        }
        if (position > 0.0) {
            lastKnownPosition = position;
        }
        try {
            if (position > 0.0 && duration > 300
                && (position - lastProgressSave) >= 10
                && (duration - position) >= 60) {
                mpv->save();
                pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                    "Player::onUpdate::saveProgress position=" + std::to_string(position)
                    + " duration=" + std::to_string(duration));
                lastProgressSave = position;
            }
        } catch (...) {
            pplay::Utility::log(pplay::Utility::LogLevel::Error,
                "Player::onUpdate::saveProgress failed");
        }
        mpv_event *event = mpv->getEvent();
        if (event != nullptr) {
            switch (event->event_id) {
                case MPV_EVENT_START_FILE:
                    printf("MPV_EVENT_START_FILE\n");
                    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                        "Player::onUpdate::event MPV_EVENT_START_FILE");
                    texture->clearFrame();
                    main->getStatus()->show("Please Wait...",
                        "Loading... " + encoding::fix(file.name), true);
                    startPlaybackPosition = -1;
                    frameDrawn = false;
                    break;
                case MPV_EVENT_FILE_LOADED:
                    printf("MPV_EVENT_FILE_LOADED\n");
                    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                        "Player::onUpdate::event MPV_EVENT_FILE_LOADED");
                    onLoadEvent();
                    main->getStatus()->hide();
                    break;
                case MPV_EVENT_END_FILE:
                    printf("MPV_EVENT_END_FILE\n");
                    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                        "Player::onUpdate::event MPV_EVENT_END_FILE");
                    onStopEvent(((mpv_event_end_file *) event->data)->reason);
                    break;
                default:
                    break;
            }
        }
        if (!mpv->isStopped()) {
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

bool Player::onInput(c2d::Input::Player *players) {
    unsigned int keys = players[0].buttons;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "Player::onInput keys=" + pplay::Utility::getKeysString(keys));

    if (mpv->isStopped()
        || main->getFiler()->isVisible()
        || main->getMenuVideo()->isVisible()
        || (getMenuVideoStreams() != nullptr && getMenuVideoStreams()->isVisible())
        || (getMenuAudioStreams() != nullptr && getMenuAudioStreams()->isVisible())
        || (getMenuSubtitlesStreams() != nullptr && getMenuSubtitlesStreams()->isVisible())
        || (getMenuPlaylist() != nullptr && getMenuPlaylist()->isVisible())
        || (getMenuPlaybackMode() != nullptr && getMenuPlaybackMode()->isVisible())) {
        return C2DObject::onInput(players);
    }

    int swap = main->getConfig()->getOption(OPT_SWAP_CONTROLS)->getInteger();
    int btnSpeedReset = swap ? c2d::Input::LT : c2d::Input::LB;
    int btnSpeedUp = swap ? c2d::Input::RT : c2d::Input::RB;
    int btnSeekBack = swap ? c2d::Input::LB : c2d::Input::LT;
    int btnSeekForward = swap ? c2d::Input::RB : c2d::Input::RT;

    float seek = main->getConfig()->getOption(OPT_SEEK_SHORT_SEC)->getFloat();
    if (keys & btnSpeedReset) {
        if (mpv->getSpeed() != 1.00) setSpeed(1);
        else getMpv()->seek(-1.00f);
    } else if (keys & btnSpeedUp) {
        double new_speed = mpv->getSpeed() + 0.05;
        if (new_speed <= 3) setSpeed(new_speed);
    } else if (keys & (btnSeekBack | btnSeekForward)) {
        osd->setVisibility(c2d::Visibility::Visible);
        getMpv()->seek(((keys & btnSeekBack) ? -seek : seek));
    } else if (keys & Input::Start) {
        int width = getMpv()->getOsdWidth();
        int height = getMpv()->getOsdHeight();
        std::string message = "Render: " +
                        std::to_string(width) + "x" + std::to_string(height);
        showMessage(message);
    } else if (keys & (Input::Up | Input::Down)) {
        int delta = (keys & Input::Up) ? 1 : -1;
        if (keys & Input::Y)
            changeBrightness(delta);
        else changeVolume(delta);
    } 

    if (osd->isVisible()) {
        return C2DObject::onInput(players);
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
        main->getStatus()->show("Info...",
            paused ? "Resuming playback..." : "Pausing playback...");
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

void Player::setVideoStream(int streamId) {
    printf("Player::setVideoStream: %i\n", streamId);
    mpv->setVid(streamId);
}

void Player::setAudioStream(int streamId) {
    printf("Player::setAudioStream: %i\n", streamId);
    mpv->setAid(streamId);
}

void Player::setSubtitleStream(int streamId) {
    printf("Player::setSubtitleStream: %i\n", streamId);
    mpv->setSid(streamId);
}

int Player::getVideoStream() {
    return mpv->getVid();
}

int Player::getAudioStream() {
    return mpv->getAid();
}

int Player::getSubtitleStream() {
    return mpv->getSid();
}

void Player::setSpeed(double speed) {
    showMessage(mpv->setSpeed(speed));
}

void Player::changeVolume(double delta) {
    showMessage(mpv->changeVolume(delta));
}

void Player::changeBrightness(double delta) {
    showMessage(mpv->changeBrightness(delta));
}

void Player::pause() {
#ifdef __PS4__
    std::string streamUrl = pplay::TorrServe::toStreamUrl(mpv->getCurrentPath());
    pausedHttpsStream = Utility::startWith(streamUrl, "https://");
    if (pausedHttpsStream) {
        pauseClock.restart();
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
            "Player::pause https stream detected, pause timer started");
    }
#endif
    mpv->pause();
    if (lastKnownPosition > 0 && lastKnownDuration > 300
        && (lastKnownDuration - lastKnownPosition) >= 60) {
        mpv->save();
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
            "Player::pause::saveProgress lastKnownPosition=" + std::to_string(lastKnownPosition)
            + " lastKnownDuration=" + std::to_string(lastKnownDuration));
    }
#ifdef __SWITCH__
    pplay::Utility::setCpuClock(pplay::Utility::CpuClock::Min);
    appletSetMediaPlaybackState(false);
#endif
}

void Player::resume() {
#ifdef __PS4__
    if (pausedHttpsStream && pauseClock.getElapsedTime().asSeconds() >= 300) {
        pausedHttpsStream = false;
        load(file, false, "pause=yes");
        return;
    }
    pausedHttpsStream = false;
#endif
    mpv->resume();
#ifdef __SWITCH__
    if (main->getConfig()->getOption(OPT_CPU_BOOST)->getString() == "Enabled") {
        pplay::Utility::setCpuClock(pplay::Utility::CpuClock::Max);
    }
    appletSetMediaPlaybackState(true);
#endif
}

void Player::stop() {
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
        main->getMenuVideo()->setVisibility(Visibility::Hidden, true);
        if (menuVideoStreams != nullptr) {
            menuVideoStreams->setVisibility(Visibility::Hidden, true);
        }
        if (menuAudioStreams != nullptr) {
            menuAudioStreams->setVisibility(Visibility::Hidden, true);
        }
        if (menuSubtitlesStreams != nullptr) {
            menuSubtitlesStreams->setVisibility(Visibility::Hidden, true);
        }
        if (menuPlaylist != nullptr) {
            menuPlaylist->setVisibility(Visibility::Hidden, true);
        }
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

MenuVideoSubmenu *Player::getMenuVideoStreams() {
    return menuVideoStreams;
}

MenuVideoSubmenu *Player::getMenuAudioStreams() {
    return menuAudioStreams;
}

MenuVideoSubmenu *Player::getMenuSubtitlesStreams() {
    return menuSubtitlesStreams;
}

MenuVideoSubmenu *Player::getMenuPlaylist() {
    return menuPlaylist;
}

MenuVideoSubmenu *Player::getMenuPlaybackMode() {
    return menuPlaybackMode;
}

const std::string &Player::getTitle() const {
    if (isPlaylistFile()) {
        displayTitle = mpv->getPlaylistCurrentTitle();
        if (displayTitle.empty()) {
            displayTitle = file.name;
        }
        displayTitle = "#" + std::to_string(mpv->getPlaylistPos() + 1)
            + ". " + displayTitle;
    } else {
        displayTitle = file.name;
    }
    return displayTitle;
}

bool Player::hasVideo() const {
    return !file.mediaInfo.videos.empty();
}

bool Player::isPlaylistFile() const {
    return file.name.size() >= 4 &&
        (file.name.compare(file.name.size() - 4, 4, ".m3u") == 0 ||
         file.name.compare(file.name.size() - 5, 5, ".m3u8") == 0);
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
