//
// Created by cpasjuste on 12/12/18.
//

#ifndef PPLAY_PLAYER_OSD_H
#define PPLAY_PLAYER_OSD_H

#include <string>
#include <vector>

#include "cross2d/c2d.h"
#include "clip_rect.h"
#include "icon_sprite.h"

class Main;

class RoundRect;

// The player panel, video and audio: full (buttons, modes), a short look (title and
// progress), or hidden (not for audio files)
class PlayerOSD : public c2d::Rectangle {

public:

    // the navigation order
    enum class ButtonID {
        Previous,
        SeekBackward,
        Pause,
        SeekForward,
        Next,
        Stop,
        FrameStep,
        Playlist,
        PlaybackMode,
        Settings
    };

    explicit PlayerOSD(Main *main);

    void reset();

    // keepFocus: on the button it was (back from what it opened)
    void expand(bool keepFocus = false);

    // title and progress for a while (new file, seek)
    void peek();

    void hide();

    bool isExpanded() const { return state == State::Full; }

    void setPaused(bool paused);

    // the volume chip for a while, even at 100%
    void showVolume();

private:

    enum class State {
        Hidden, Peek, Full
    };

    struct Chip {
        RoundRect *back;
        c2d::Text *text;
    };

    void onUpdate() override;

    bool onInput(c2d::Input::Player *players) override;

    void layout(bool audio);

    void updateTexts();

    void placeTitle();

    void scrollTitle();

    void updateChips();

    void updatePlaybackMode();

    void updateProgress();

    void applyFade();

    Main *main;
    State state = State::Hidden;
    bool audioLayout = false;
    bool laidOut = false;
    // animated: the panel shown, its full part shown
    float shown = 0, full = 0;
    // the thin progress line (setting, a file playing)
    bool lineShown = false;
    float peekOffset = 0;
    c2d::C2DClock clock, frameClock, textClock, volumeClock;
    bool volumeShown = false;
    std::string textKey;

    c2d::GradientRectangle *scrim = nullptr;
    c2d::Rectangle *panel = nullptr;
    pplay::ClipRect *titleClip = nullptr;
    c2d::Text *title = nullptr;
    std::string titleShown;
    float titleMax = 0, titleY = 0, metaY = 0;
    // the progress line: its room (the times aside) and the times format it was placed for
    float trackX = 0, trackY = 0, trackW = 0;
    std::string timePattern;
    // the read ahead end (s), -1: none
    double cacheEnd = -1;
    c2d::C2DClock cacheClock;

    void placeTrack();
    c2d::C2DClock titleClock;
    c2d::Text *meta = nullptr;
    c2d::RectangleShape *track = nullptr;
    c2d::RectangleShape *buffered = nullptr;
    c2d::RectangleShape *played = nullptr;
    RoundRect *knob = nullptr;
    c2d::Text *elapsed = nullptr;
    c2d::Text *remaining = nullptr;
    std::vector<Chip> chips;
    RoundRect *focus = nullptr;
    std::vector<pplay::IconSprite *> buttons;
    std::vector<float> buttonScales;
    pplay::IconSprite *btn_play = nullptr;
    pplay::IconSprite *btn_pause = nullptr;
    c2d::Text *next = nullptr;
    c2d::RectangleShape *edge = nullptr;
    int index = (int) ButtonID::Pause;
    int playbackModeShown = -1;
    float focusX = 0;
};

#endif //PPLAY_PLAYER_OSD_H
