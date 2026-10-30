//
// Audio files screen: the cover (AudioView), the visualizer (mpv lavfi-complex), or a note.
//

#include <algorithm>
#include <cstdio>

#include "main.h"
#include "player.h"
#include "audio_view.h"
#include "cover.h"
#include "filer.h"
#include "torrserve.h"

using namespace c2d;

std::string Player::visualizerGraph() const {
    // 32 bands of 24 segments (60 dB), drawn small then scaled to the zone (1024x576 at
    // 1080p: no scaling by mpv). Peaks: the bands kept a bit (lagfun), their top segment.
    char accent[160];
    snprintf(accent, sizeof(accent), "lutrgb=r='val*%d/255':g='val*%d/255':b='val*%d/255'", COLOR_ACCENT.r,
             COLOR_ACCENT.g, COLOR_ACCENT.b);
    return "aformat=channel_layouts=mono,"
           "showfreqs=s=32x40:mode=bar:ascale=log:fscale=log:win_size=2048:overlap=0.5:averaging=2:colors=white,"
           "crop=32:24:0:0,format=gray,split[bands][held];"
           "[held]lagfun=decay=0.95,lut=y='if(gt(val,40),255,0)',split[peaks][below];"
           "[below]pad=32:25:0:1,crop=32:24:0:0[shifted];"
           "[peaks][shifted]blend=all_mode=subtract,format=gbrp,"
           + std::string(accent)
           + "[top];"
             "[bands]format=gbrp,lutrgb=r='val*208/255':g='val*208/255':b='val*208/255'[gray];"
             "[gray][top]blend=all_expr='if(gt(B,0),B,A)',scale=128:96:flags=neighbor,format=yuv444p,"
             "drawgrid=w=4:h=4:t=1:c=black,scale=1024:576:flags=neighbor,format=yuv420p";
}

void Player::clearAudioScreen() {
    if (audioScreen == AudioScreen::None && appliedVisualizer.empty()) {
        return;
    }
    audioScreen = AudioScreen::None;
    appliedVisualizer.clear();
    mpv->setVisualizer("");
    mpv->setFrameLayout(0, 0, 0, 0);
}

std::vector<std::string> Player::pictureUrlsOf(const MediaFile &f) {
    // the file own cover, the directory one, the torrent one (as the details panel)
    std::vector<std::string> urls;
    std::string url = pplay::TorrServe::toStreamUrl(f.path);
    bool embedded = false;
    for (auto &video: f.mediaInfo.videos) {
        embedded |= video.albumart && !video.external;
    }
    if (embedded && !Utility::startWith(url, "smb")) {
        urls.push_back(pplay::Cover::cacheUrl(url));
    }
    std::string folder = main->getFiler()->folderCoverOf(f.path);
    if (!folder.empty()) {
        urls.push_back(folder);
    }
    pplay::TorrServe::Info info;
    if (pplay::TorrServe::getInfo(f.path, info) && !info.poster.empty()) {
        urls.push_back(info.poster);
    }
    return urls;
}

void Player::updateAudioScreen() {
    if (!mpv->isAvailable() || mpv->isStopped()) {
        clearAudioScreen();
        return;
    }
    bool moving = false;
    for (auto &video: file.mediaInfo.videos) {
        moving |= !video.albumart;
    }
    // cover > visualizer > icon, each one if possible and wanted
    int mode = main->getConfig()->getOption(OPT_AUDIO_VISUALIZER)->getInteger();
    AudioScreen screen = AudioScreen::None;
    if (!moving) {
        if (mode == 0 && !pictureUrls.empty() && !picturesFailed) {
            screen = AudioScreen::Picture;
        } else if (mode <= 1 && !file.mediaInfo.audios.empty()) {
            screen = AudioScreen::Visualizer;
        } else {
            screen = AudioScreen::Note;
        }
    }
    if (screen == AudioScreen::None) {
        clearAudioScreen();
        return;
    }

    std::string graph = screen == AudioScreen::Visualizer ? visualizerGraph() : "";
    if (screen == audioScreen && graph == appliedVisualizer) {
        return;
    }
    // mpv draws the visualizer only (the covers: AudioView)
    FloatRect zone = AudioView::zone();
    if (graph.empty()) {
        mpv->setFrameLayout(0, 0, 0, 0);
    } else {
        mpv->setFrameLayout(zone.left, zone.top, zone.left, 1 - zone.top - zone.height);
    }
    audioScreen = screen;
    appliedVisualizer = graph;
    mpv->setVisualizer(graph);
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Player::updateAudioScreen: " + std::to_string((int) screen));
}

void Player::onPicturesFailed() {
    picturesFailed = true;
    updateAudioScreen();
}

void Player::setAudioScreenMode(int mode) {
    auto *option = main->getConfig()->getOption(OPT_AUDIO_VISUALIZER);
    if (option->getInteger() == mode) {
        return;
    }
    option->setInteger(mode);
    main->getConfig()->save();
    updateAudioScreen();
}
