//
// Created by cpasjuste on 12/12/18.
//

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "main.h"
#include "utility.h"
#include "player_osd.h"
#include "audio_view.h"
#include "round_rect.h"
#include "encodings.h"
#include "i18n.h"

using namespace c2d;

// seconds without a key: full -> short look -> hidden (video)
#define OSD_FULL_TIME 4.0f
#define OSD_PEEK_TIME 3.0f

namespace {
    std::string clockText(double seconds) {
        int s = std::max(0, (int) seconds);
        char text[32];
        if (s >= 3600) {
            snprintf(text, sizeof(text), "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
        } else {
            snprintf(text, sizeof(text), "%d:%02d", s / 60, s % 60);
        }
        return text;
    }

    std::string channels(long count) {
        switch (count) {
            case 1:
                return "mono";
            case 2:
                return "stereo";
            case 6:
                return "5.1";
            case 8:
                return "7.1";
            default:
                return count > 0 ? std::to_string(count) + "ch" : "";
        }
    }

    Color faded(const Color &color, float fade) {
        return {color.r, color.g, color.b, (uint8_t) std::lround(color.a * std::min(1.0f, std::max(0.0f, fade)))};
    }

    void fadeText(Text *text, const Color &color, float fade) {
        text->setFillColor(faded(color, fade));
        text->setOutlineColor(faded(Color(0, 0, 0, 120), fade));
    }

    const Color TEXT_DIM(160, 168, 178, 255);
}

PlayerOSD::PlayerOSD(Main *m) : Rectangle(m->getSize()), main(m) {
    const Vector2f size = main->getSize();
    const float sc = main->getScaling().y;

    scrim = new GradientRectangle({0, size.y - 260 * sc, size.x, 260 * sc});
    PlayerOSD::add(scrim);

    panel = new Rectangle(size);
    PlayerOSD::add(panel);

    title = new Text("", main->getFontSize(Main::FontSize::Bigger), main->getFont());
    title->setOutlineThickness(1);
    panel->add(title);

    meta = new Text("", main->getFontSize(Main::FontSize::Small), main->getFont());
    meta->setOutlineThickness(1);
    panel->add(meta);

    float line = std::max(2.0f, std::round(4 * sc));
    track = new RectangleShape({10, line});
    panel->add(track);
    buffered = new RectangleShape({0, line});
    track->add(buffered);
    played = new RectangleShape({0, line});
    track->add(played);
    float knobSize = std::round(13 * sc);
    knob = new RoundRect({knobSize, knobSize});
    knob->setOrigin(Origin::Center);
    track->add(knob);

    elapsed = new Text("0:00", main->getFontSize(Main::FontSize::Small), main->getFont());
    elapsed->setOrigin(Origin::Right);
    panel->add(elapsed);
    remaining = new Text("-0:00", main->getFontSize(Main::FontSize::Small), main->getFont());
    remaining->setOrigin(Origin::Left);
    panel->add(remaining);

    for (int i = 0; i < 4; i++) {
        Chip chip{new RoundRect({10, std::round(22 * sc)}),
                  new Text("", main->getFontSize(Main::FontSize::XS), main->getFont())};
        chip.text->setOrigin(Origin::Center);
        chip.back->add(chip.text);
        chip.back->setVisibility(Visibility::Hidden);
        panel->add(chip.back);
        chips.push_back(chip);
    }

    float focusSize = std::round(44 * sc);
    focus = new RoundRect({focusSize, focusSize});
    focus->setOrigin(Origin::Center);
    panel->add(focus);

    auto addButton = [this, sc](const char *file) {
        auto *button = new C2DTexture(main->getIo()->getDataPath() + "skin/" + file);
        // the icon in the focus circle
        float scale = 40.0f * sc / (float) std::max(1, button->getTextureSize().x);
        button->setScale(scale, scale);
        button->setOrigin(Origin::Center);
        panel->add(button);
        return button;
    };
    btn_pause = addButton("btn_pause.png");
    btn_play = addButton("btn_play.png");
    btn_play->setVisibility(Visibility::Hidden);
    buttons = {addButton("btn_prev.png"), addButton("btn_seek_backward_long.png"),
               addButton("btn_seek_backward_short.png"), btn_pause,
               addButton("btn_seek_forward_short.png"), addButton("btn_seek_forward_long.png"),
               addButton("btn_next.png"), addButton("btn_settings.png"), addButton("btn_stop.png")};

    next = new Text("", main->getFontSize(Main::FontSize::Small), main->getFont());
    next->setOutlineThickness(1);
    panel->add(next);

    edge = new RectangleShape({0, size.y - std::max(2.0f, std::round(3 * sc)), 0, std::max(2.0f, std::round(3 * sc))});
    PlayerOSD::add(edge);

    layout(false);
    applyFade();
}

void PlayerOSD::layout(bool audio) {
    const Vector2f size = main->getSize();
    const float sc = main->getScaling().y;
    audioLayout = audio;
    laidOut = true;

    float trackY, trackX, trackW, buttonsY, titleY;
    if (audio) {
        // under the cover / visualizer zone, centered
        FloatRect zone = AudioView::zone();
        float bottom = (zone.top + zone.height) * size.y;
        titleY = bottom + 38 * sc;
        trackW = size.x * 0.47f;
        trackX = (size.x - trackW) / 2;
        trackY = bottom + 104 * sc;
        buttonsY = trackY + 84 * sc;
        title->setOrigin(Origin::Center);
        title->setPosition(size.x / 2, titleY);
        title->setSizeMax(size.x * 0.8f, 0);
        meta->setOrigin(Origin::Center);
        meta->setPosition(size.x / 2, titleY + 28 * sc);
        peekOffset = 0;
    } else {
        float margin = 54 * sc;
        buttonsY = size.y - 40 * sc;
        trackY = buttonsY - 38 * sc;
        titleY = trackY - 26 * sc;
        trackX = margin + 80 * sc;
        trackW = size.x - 2 * trackX;
        title->setOrigin(Origin::Left);
        title->setPosition(margin, titleY);
        title->setSizeMax(size.x * 0.5f, 0);
        meta->setOrigin(Origin::Left);
        peekOffset = 50 * sc;
    }
    track->setPosition(trackX, trackY - track->getSize().y / 2);
    track->setSize(trackW, track->getSize().y);
    elapsed->setPosition(trackX - 14 * sc, trackY);
    remaining->setPosition(trackX + trackW + 14 * sc, trackY);

    // transport in a row, settings and stop apart
    float step = 52 * sc;
    std::vector<float> xs(buttons.size());
    if (audio) {
        float width = step * 8 + 24 * sc;
        float x = (size.x - width) / 2;
        for (size_t i = 0; i < buttons.size(); i++) {
            xs[i] = x + step * (float) i + (i >= (size_t) ButtonID::Settings ? 24 * sc : 0);
        }
    } else {
        float left = 54 * sc + 22 * sc, right = size.x - 54 * sc - 22 * sc;
        for (size_t i = 0; i <= (size_t) ButtonID::Next; i++) {
            xs[i] = left + step * (float) i;
        }
        xs[(size_t) ButtonID::Settings] = right - step;
        xs[(size_t) ButtonID::Stop] = right;
    }
    for (size_t i = 0; i < buttons.size(); i++) {
        buttons[i]->setPosition(xs[i], buttonsY);
    }
    btn_play->setPosition(xs[(size_t) ButtonID::Pause], buttonsY);
    // video: before settings; audio: under the buttons
    if (audio) {
        next->setOrigin(Origin::Center);
        next->setPosition(size.x / 2, buttonsY + 40 * sc);
        next->setSizeMax(size.x * 0.6f, 0);
    } else {
        next->setOrigin(Origin::Right);
        next->setPosition(xs[(size_t) ButtonID::Settings] - 44 * sc, buttonsY);
        next->setSizeMax(xs[(size_t) ButtonID::Settings] - xs[(size_t) ButtonID::Next] - 100 * sc, 0);
    }
    focus->setPosition(focusX = xs[(size_t) index], buttonsY);
    textKey.clear();
}

void PlayerOSD::expand() {
    if (state != State::Full) {
        index = (int) ButtonID::Pause;
    }
    state = State::Full;
    clock.restart();
}

void PlayerOSD::peek() {
    if (state != State::Full) {
        state = State::Peek;
    }
    clock.restart();
}

void PlayerOSD::hide() {
    state = State::Hidden;
    clock.restart();
}

void PlayerOSD::setPaused(bool paused) {
    btn_play->setVisibility(paused ? Visibility::Visible : Visibility::Hidden);
    btn_pause->setVisibility(paused ? Visibility::Hidden : Visibility::Visible);
}

void PlayerOSD::updateTexts() {
    Player *player = main->getPlayer();
    Mpv *mpv = player->getMpv();
    std::vector<std::string> parts;
    std::string name;

    if (audioLayout) {
        // "Artist — Title", else the player title (file name)
        std::string artist = encoding::fix(mpv->getString("metadata/by-key/artist"));
        name = encoding::fix(mpv->getString("metadata/by-key/title"));
        if (name.empty()) {
            name = encoding::fix(player->getTitle());
        }
        name = artist.empty() ? name : artist + " — " + name;
        std::string codec = c2d::Utility::toUpper(mpv->getString("audio-codec-name"));
        long rate = mpv->getInt("audio-params/samplerate", 0);
        long bitrate = mpv->getInt("audio-bitrate", 0);
        char text[32];
        if (!codec.empty()) {
            parts.push_back(codec);
        }
        if (rate > 0) {
            snprintf(text, sizeof(text), "%g kHz", (double) rate / 1000);
            parts.emplace_back(text);
        }
        if (!channels(mpv->getInt("audio-params/channel-count", 0)).empty()) {
            parts.push_back(channels(mpv->getInt("audio-params/channel-count", 0)));
        }
        if (bitrate > 0) {
            parts.push_back(std::to_string(bitrate / 1000) + " kb/s");
        }
    } else {
        name = encoding::fix(player->getTitle());
        long height = mpv->getInt("video-params/h", 0);
        std::string codec = c2d::Utility::toUpper(mpv->getString("video-codec-name"));
        if (height > 0) {
            parts.push_back(std::to_string(height) + "p");
        }
        if (!codec.empty()) {
            parts.push_back(codec);
        }
        std::string audio = c2d::Utility::toUpper(mpv->getString("audio-codec-name"));
        std::string layout = channels(mpv->getInt("audio-params/channel-count", 0));
        if (!audio.empty()) {
            parts.push_back(audio + (layout.empty() ? "" : " " + layout));
        }
    }
    long duration = mpv->getDuration();
    if (duration > 0) {
        parts.push_back(clockText((double) duration));
    }
    std::string line;
    for (auto &part: parts) {
        line += (line.empty() ? "" : " · ") + part;
    }
    title->setString(name);
    meta->setString(line);
    std::string following = player->nextTitle();
    next->setString(following.empty() ? "" : pplay::tr("Next") + ": " + encoding::fix(following));
    if (!audioLayout) {
        const float sc = main->getScaling().y;
        meta->setPosition(title->getPosition().x + title->getLocalBounds().width + 14 * sc,
                          title->getPosition().y + 2 * sc);
    }
}

void PlayerOSD::updateChips() {
    Player *player = main->getPlayer();
    Mpv *mpv = player->getMpv();
    PPLAYConfig *config = main->getConfig();
    // by priority: the normalization only if there is room left
    std::vector<std::string> labels;
    double speed = mpv->getSpeed();
    if (speed > 0 && std::fabs(speed - 1.0) > 0.001) {
        char text[16];
        snprintf(text, sizeof(text), "%.2fx", speed);
        labels.emplace_back(text);
    }
    long volume = mpv->getInt("volume", 100);
    if (volume != 100) {
        labels.push_back(pplay::tr("Volume") + " " + std::to_string(volume) + "%");
    }
    int mode = config->getOption(OPT_AUTOPLAY_MODE)->getInteger();
    if (mode == 2 || mode == 3) {
        labels.push_back(pplay::tr(mode == 2 ? "Loop file" : "Loop directory"));
    }
    int normalize = config->getOption(OPT_AUDIO_NORMALIZE)->getInteger();
    if (normalize == 1 || normalize == 2) {
        labels.push_back(pplay::tr(normalize == 1 ? "Normalize" : "Night mode"));
    }

    const Vector2f size = main->getSize();
    const float sc = main->getScaling().y;
    float pad = 12 * sc, gap = 8 * sc;
    // video: right of the title line; audio: the bar width
    float room = audioLayout ? track->getSize().x
                             : size.x - 54 * sc - (meta->getPosition().x + meta->getLocalBounds().width + 24 * sc);
    std::vector<size_t> used;
    float width = 0;
    for (size_t i = 0; i < labels.size() && used.size() < chips.size(); i++) {
        Chip &chip = chips[used.size()];
        chip.text->setString(labels[i]);
        float w = std::round(chip.text->getLocalBounds().width + 2 * pad);
        float needed = width + (used.empty() ? 0 : gap) + w;
        if (needed > room) {
            continue;
        }
        chip.back->setSize(w, chip.back->getSize().y);
        chip.text->setPosition(w / 2, chip.back->getSize().y / 2);
        width = needed;
        used.push_back(i);
    }
    float x = audioLayout ? (size.x - width) / 2 : size.x - 54 * sc - width;
    float y = audioLayout ? track->getPosition().y + 32 * sc : title->getPosition().y;
    for (size_t i = 0; i < chips.size(); i++) {
        bool shown = i < used.size();
        chips[i].back->setVisibility(shown ? Visibility::Visible : Visibility::Hidden);
        if (shown) {
            chips[i].back->setOrigin(Origin::Left);
            chips[i].back->setPosition(x, y);
            x += chips[i].back->getSize().x + gap;
        }
    }
}

void PlayerOSD::updateProgress() {
    Mpv *mpv = main->getPlayer()->getMpv();
    double position = std::max(0.0, mpv->getTime());
    double duration = (double) mpv->getDuration();
    float width = track->getSize().x;
    float done = duration > 0 ? (float) std::min(1.0, position / duration) : 0;
    float cache = 0;
    if (duration > 0) {
        cache = (float) std::min(1.0, (position + std::max(0.0, mpv->getCacheDuration())) / duration);
    }
    played->setSize(std::round(done * width), played->getSize().y);
    buffered->setSize(std::round(cache * width), buffered->getSize().y);
    knob->setPosition(std::round(done * width), track->getSize().y / 2);
    elapsed->setString(clockText(position));
    double speed = std::max(0.01, mpv->getSpeed());
    remaining->setString("-" + clockText(std::max(0.0, (duration - position) / speed)));
    edge->setSize(std::round(done * main->getSize().x), edge->getSize().y);
}

void PlayerOSD::applyFade() {
    float base = shown, extra = shown * full;
    float sc = main->getScaling().y;
    panel->setPosition(0, (1 - full) * peekOffset + (1 - shown) * 12 * sc);

    scrim->setVisibility(audioLayout || base <= 0 ? Visibility::Hidden : Visibility::Visible);
    // Up: the first color at the bottom
    scrim->setColor(faded(Color(5, 7, 10, 245), base * (0.7f + 0.3f * full)), Color(5, 7, 10, 0), GradientRectangle::Up);

    fadeText(title, COLOR_FONT, base);
    fadeText(meta, TEXT_DIM, base);
    fadeText(elapsed, TEXT_DIM, base);
    fadeText(remaining, TEXT_DIM, base);
    track->setFillColor(faded(Color(255, 255, 255, 40), base));
    buffered->setFillColor(faded(Color(255, 255, 255, 50), base));
    played->setFillColor(faded(COLOR_ACCENT, base));
    knob->setFillColor(faded(COLOR_WHITE, base));

    Color chipColor = COLOR_ACCENT;
    chipColor.a = 90;
    for (auto &chip: chips) {
        chip.back->setFillColor(faded(chipColor, extra));
        chip.text->setFillColor(faded(COLOR_FONT, extra));
    }
    focus->setFillColor(faded(COLOR_WHITE, extra));
    fadeText(next, TEXT_DIM, extra);
    for (size_t i = 0; i < buttons.size(); i++) {
        bool focused = (int) i == index;
        buttons[i]->setFillColor(faded(focused ? Color(16, 18, 22, 255) : COLOR_FONT, extra));
    }
    btn_play->setFillColor(faded(index == (int) ButtonID::Pause ? Color(16, 18, 22, 255) : COLOR_FONT, extra));

    // hidden video: a thin progress line left
    edge->setFillColor(faded(COLOR_ACCENT, audioLayout ? 0 : (1 - shown) * 0.8f));
}

void PlayerOSD::onUpdate() {
    Player *player = main->getPlayer();
    Mpv *mpv = player->getMpv();
    float dt = std::min(0.1f, frameClock.restart().asSeconds());

    bool active = player->isFullscreen() && !mpv->isStopped();
    bool audio = player->getAudioScreen() != Player::AudioScreen::None;
    if (!laidOut || audio != audioLayout) {
        layout(audio);
    }

    if (mpv->isStopped()) {
        // (re)loading: keep the state and its timer
        clock.restart();
    } else if (!mpv->isPaused()) {
        float elapsedTime = clock.getElapsedTime().asSeconds();
        if (state == State::Full && elapsedTime >= OSD_FULL_TIME) {
            state = State::Peek;
            clock.restart();
        } else if (state == State::Peek && !audio && elapsedTime >= OSD_PEEK_TIME) {
            state = State::Hidden;
        }
    }
    if (audio && state == State::Hidden) {
        state = State::Peek;
    }

    float shownTarget = active && state != State::Hidden ? 1.0f : 0.0f;
    float fullTarget = state == State::Full ? 1.0f : 0.0f;
    float k = std::min(1.0f, dt * 10);
    shown += (shownTarget - shown) * k;
    full += (fullTarget - full) * k;
    if (std::fabs(shown - shownTarget) < 0.01f) shown = shownTarget;
    if (std::fabs(full - fullTarget) < 0.01f) full = fullTarget;
    focusX += (buttons[(size_t) index]->getPosition().x - focusX) * std::min(1.0f, dt * 16);
    focus->setPosition(focusX, buttons[(size_t) index]->getPosition().y);

    // the status bar with the full panel
    if (player->isFullscreen()) {
        bool bar = active && state == State::Full;
        if (main->getStatusBar()->isVisible() != bar) {
            main->getStatusBar()->setVisibility(bar ? Visibility::Visible : Visibility::Hidden, true);
        }
    }

    if (active) {
        if (player->getTitle() != textKey || textClock.getElapsedTime().asSeconds() >= 1) {
            textKey = player->getTitle();
            textClock.restart();
            updateTexts();
            updateChips();
        }
        updateProgress();
    }
    applyFade();
    Rectangle::onUpdate();
}

bool PlayerOSD::onInput(c2d::Input::Player *players) {
    Player *player = main->getPlayer();
    Mpv *mpv = player->getMpv();
    // the menus over it get the keys
    if (state != State::Full || player->isMenuVisible()) {
        return false;
    }

    unsigned int keys = players[0].buttons;
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                        "PlayerOSD::onInput keys=" + pplay::Utility::getKeysString(keys));
    if (!keys) {
        return true;
    }

    if (keys & Input::B) {
        hide();
    } else if (keys & Input::X) {
        hide();
        main->getMenuVideo()->setVisibility(Visibility::Visible, true);
    } else if (keys & (Input::Left | Input::Right)) {
        int dir = (keys & Input::Right) ? 1 : -1;
        index = (index + dir + (int) buttons.size()) % (int) buttons.size();
        clock.restart();
    } else if (keys & Input::A) {
        auto cfg = main->getConfig();
        float s_short = cfg->getOption(OPT_SEEK_SHORT_SEC)->getFloat();
        float s_long = cfg->getOption(OPT_SEEK_LONG_SEC)->getFloat();

        switch ((ButtonID) index) {
            case ButtonID::Pause: {
                bool pause = !mpv->isPaused();
                setPaused(pause);
                main->getStatus()->show("Info...", pause ? "Pausing playback..." : "Resuming playback...");
                pause ? player->pause() : player->resume();
                break;
            }
            case ButtonID::SeekForwardShort:
                mpv->seek(s_short);
                break;
            case ButtonID::SeekForwardLong:
                mpv->seek(s_long);
                break;
            case ButtonID::SeekBackwardShort:
                mpv->seek(-s_short);
                break;
            case ButtonID::SeekBackwardLong:
                mpv->seek(-s_long);
                break;
            case ButtonID::Previous:
            case ButtonID::Next:
                peek();
                player->playSibling((ButtonID) index == ButtonID::Next ? 1 : -1);
                break;
            case ButtonID::Settings:
                hide();
                main->getMenuVideo()->setVisibility(Visibility::Visible, true);
                break;
            case ButtonID::Stop:
                hide();
                main->getStatus()->show("Info...", "Stopping playback...");
                player->stop();
                break;
        }
        clock.restart();
    }

    return true;
}

void PlayerOSD::reset() {
    index = (int) ButtonID::Pause;
    setPaused(false);
    clock.restart();
}
