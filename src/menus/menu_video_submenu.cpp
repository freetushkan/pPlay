//
// Created by cpasjuste on 07/12/18.
//

#include <algorithm>
#include <cstdio>

#include "cross2d/c2d.h"
#include "main.h"
#include "menu_video_submenu.h"
#include "pplay_config.h"
#include "utility.h"

using namespace c2d;

namespace {
    struct SubtitleColor { const char *name; const char *value; };
    const SubtitleColor subtitleColors[] = {
        {"Base Blue","#1078C8"},{"Muted Cyan","#008EA0"},{"Muted Crimson","#D64545"},{"Soft Crimson","#EB4D4B"},
        {"Terracotta Orange","#DB6B30"},{"Burnt Orange","#FA8231"},{"Ochre Yellow","#C29346"},{"Olive Gold","#9EA133"},
        {"Olive Lime","#88B04B"},{"Sage Green","#5D9E54"},{"Soft Emerald","#3A9668"},{"Bright Emerald","#20BF6B"},
        {"Deep Turquoise","#2E8B96"},{"Slate Blue","#4378A6"},{"Dark Indigo","#535EA8"},{"Soft Purple","#A55EEA"},
        {"Amethyst Purple","#7A5299"},{"Plum Violet","#9C498D"},{"Dusty Rose","#B5526C"},{"Deep Red","#D63031"},
        {"Classic Red","#FF0000"},{"Black","#000000"},{"White","#FFFFFF"}};
    constexpr int subtitleColorCount = sizeof(subtitleColors) / sizeof(subtitleColors[0]);
    int subtitleColorIndex(const std::string &v) { for(int i=0;i<subtitleColorCount;i++) if(v==subtitleColors[i].value)return i; return -1; }
    std::string subtitleColorLabel(const std::string &v) { int i=subtitleColorIndex(v); return i<0?v:pplay::tr(subtitleColors[i].name); }
}

MenuVideoSubmenu::MenuVideoSubmenu(Main *main, const c2d::FloatRect &rect, const std::string &_title,
                                   const std::vector<MenuItem> &items, int type)
    : Menu(main, rect, _title, items, false) {

    this->type = type;
    updateSelectionHighlight();
}

void MenuVideoSubmenu::setSelection(int streamId) {
    for (auto btn: buttons) {
        if (btn->item.id == streamId) {
            showCurrent(btn);
        }
    }
}

void MenuVideoSubmenu::updateSelectionHighlight() {
    if (type == MENU_VIDEO_TYPE_SUB_SETTINGS) {
        PPLAYConfig *c = main->getConfig();
        for (auto *b: buttons) {
            const std::string &n=b->item.name;
            if(n=="Subtitle position") b->item.value=pplay::tr(c->getOption(OPT_SUB_POSITION)->getInteger()?"Top":"Bottom");
            else if(n=="Subtitle size") b->item.value=std::to_string(c->getOption(OPT_SUB_FONT_SIZE)->getInteger());
            else if(n=="Subtitle color") b->item.value=subtitleColorLabel(c->getOption(OPT_SUB_COLOR)->getString());
            else if(n=="Subtitle outline") b->item.value=subtitleColorLabel(c->getOption(OPT_SUB_OUTLINE_COLOR)->getString());
            b->refreshValue();
        }
        return;
    }
    if (type == MENU_VIDEO_TYPE_SPEED || type == MENU_VIDEO_TYPE_BRIGHTNESS) {
        // no current item: the value line
        hideCurrent();
        char value[32];
        if (type == MENU_VIDEO_TYPE_SPEED) {
            snprintf(value, sizeof(value), "%.2f", main->getPlayer()->getMpv()->getSpeed());
        } else {
            snprintf(value, sizeof(value), "%d", (int) main->getPlayer()->getMpv()->getDouble("brightness", 0));
        }
        if (!buttons.empty() && buttons[0]->item.id == MENU_ADJUST_VALUE) {
            buttons[0]->item.label = pplay::tr(type == MENU_VIDEO_TYPE_SPEED ? "Speed" : "Brightness") + ": " + value;
            buttons[0]->refreshText();
        }
        return;
    }
    current->setVisibility(Visibility::Visible);
    if (type == MENU_VIDEO_TYPE_VID) {
        setSelection(main->getPlayer()->getVideoStream());
    } else if (type == MENU_VIDEO_TYPE_AUD) {
        setSelection(main->getPlayer()->getAudioStream());
    } else if (type == MENU_VIDEO_TYPE_SUB) {
        setSelection(main->getPlayer()->getSubtitleStream());
    } else if (type == MENU_VIDEO_TYPE_PL_MODE) {
        setSelection(main->getConfig()->getOption(OPT_AUTOPLAY_MODE)->getInteger());
    } else if (type == MENU_VIDEO_TYPE_NORMALIZE) {
        setSelection(main->getConfig()->getOption(OPT_AUDIO_NORMALIZE)->getInteger());
    } else if (type == MENU_VIDEO_TYPE_VISUALIZER) {
        setSelection(main->getConfig()->getOption(OPT_AUDIO_VISUALIZER)->getInteger());
    } else if (type == MENU_VIDEO_TYPE_PLAYLIST) {
        // the playing one: marked, its icon too
        int playing = playingId();
        hideCurrent();
        setSelection(playing);
        // its number replaced by the icon: playing or paused
        shownPaused = main->getPlayer()->getMpv()->isPaused();
        for (auto *btn: buttons) {
            btn->setIcon(btn->item.id != playing ? "" : shownPaused ? "list_paused.png" : "list_playing.png");
        }
    }
}

int MenuVideoSubmenu::playingId() const {
    Player *player = main->getPlayer();
    if (player->isPlaylistFile()) {
        return player->getMpv()->getPlaylistPos();
    }
    for (auto *btn: buttons) {
        if (btn->item.id >= 0 && btn->item.data_str == player->getFile().path) {
            return btn->item.id;
        }
    }
    return -1;
}

void MenuVideoSubmenu::onOptionSelection(MenuItem *item) {
    Player *player = main->getPlayer();
    if (type == MENU_VIDEO_TYPE_SUB_SETTINGS) return;
    if (type == MENU_VIDEO_TYPE_SPEED) {
        double speed = item->id == 0 ? 1.0 : player->getMpv()->getSpeed() + 0.05 * item->id;
        player->setSpeed(std::max(0.25, std::min(3.0, speed)));
        updateSelectionHighlight();
        return;
    }
    if (type == MENU_VIDEO_TYPE_BRIGHTNESS) {
        item->id == 0 ? player->resetBrightness() : player->changeBrightness(item->id);
        updateSelectionHighlight();
        return;
    }
    if (item->id == MENU_TRACK_ADD && (type == MENU_VIDEO_TYPE_AUD || type == MENU_VIDEO_TYPE_SUB)) {
        setVisibility(Visibility::Hidden, true);
        main->getPlayer()->pickExternalTrack(type == MENU_VIDEO_TYPE_AUD);
        return;
    }
    if (item->position == MenuItem::Position::Top) {
        std::string name = item->name;
        std::replace(name.begin(), name.end(), '\n', ' ');
        if (type == MENU_VIDEO_TYPE_VID && main->getPlayer()->getVideoStream() != item->id) {
            main->getStatus()->show("Wait", pplay::tr("Loading video") + " \"" + name + "\". "
                                          + pplay::tr("This can take a few seconds"));
            main->getPlayer()->setVideoStream(item->id);
        } else if (type == MENU_VIDEO_TYPE_AUD && main->getPlayer()->getAudioStream() != item->id) {
            main->getStatus()->show("Wait", pplay::tr("Loading audio") + " \"" + name + "\". "
                                          + pplay::tr("This can take a few seconds"));
            main->getPlayer()->setAudioStream(item->id);
        } else if (type == MENU_VIDEO_TYPE_SUB && main->getPlayer()->getSubtitleStream() != item->id) {
            main->getStatus()->show("Wait", pplay::tr("Loading subtitles") + " \"" + name + "\". "
                                          + pplay::tr("This can take a few seconds"));
            main->getPlayer()->setSubtitleStream(item->id);
        } else if (type == MENU_VIDEO_TYPE_PL_MODE) {
            auto *option = main->getConfig()->getOption(OPT_AUTOPLAY_MODE);
            const std::string oldValue = option->getString();
            option->setType(c2d::config::Option::Type::Integer);
            option->setInteger(item->id);
            main->getConfig()->save();
            pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                "Options: " + std::string(OPT_AUTOPLAY_MODE)
                            + " changed from " + oldValue + " to " + option->getString());
        } else if (type == MENU_VIDEO_TYPE_NORMALIZE) {
            main->getPlayer()->setAudioNormalize(item->id);
        } else if (type == MENU_VIDEO_TYPE_VISUALIZER) {
            main->getPlayer()->setAudioScreenMode(item->id);
        } else if (type == MENU_VIDEO_TYPE_PLAYLIST && item->id == playingId()) {
            // the playing one: paused, resumed
            main->getPlayer()->togglePause();
        } else if (type == MENU_VIDEO_TYPE_PLAYLIST) {
            main->getStatus()->show("Wait", "Switching playlist item");
            if (main->getPlayer()->isPlaylistFile()) {
                main->getPlayer()->getMpv()->setPlaylistPos(item->id);
            } else {
                auto files = main->getPlayer()->getAutoplayFiles();
                if (item->id >= 0 && item->id < (int) files.size()) {
                    main->getPlayer()->load(files[item->id]);
                }
            }
        }
        updateSelectionHighlight();
    }
}

bool MenuVideoSubmenu::onOptionChange(MenuItem *item, int direction) {
    if(type!=MENU_VIDEO_TYPE_SUB_SETTINGS || direction==0) return false;
    Player *p=main->getPlayer(); if(!p || !p->getMpv()) return false;
    PPLAYConfig *c=main->getConfig();
    if(item->name=="Subtitle position") {
        auto *o=c->getOption(OPT_SUB_POSITION); int v=(o->getInteger()+direction+2)%2; o->setInteger(v);
        if(!p->getMpv()->isAssSubtitle()) p->getMpv()->setSubPos(v?0.0:100.0);
    } else if(item->name=="Subtitle size") {
        auto *o=c->getOption(OPT_SUB_FONT_SIZE); int v=std::max(20,std::min(120,o->getInteger()+direction*2)); o->setInteger(v); p->getMpv()->setSubFontSize(v);
    } else if(item->name=="Subtitle color" || item->name=="Subtitle outline") {
        bool outline=item->name=="Subtitle outline"; auto *o=c->getOption(outline?OPT_SUB_OUTLINE_COLOR:OPT_SUB_COLOR);
        int i=subtitleColorIndex(o->getString()); if(i<0)i=subtitleColorIndex(outline?"#000000":"#FFFFFF");
        i=(i+direction+subtitleColorCount)%subtitleColorCount; std::string v=subtitleColors[i].value; o->setString(v);
        if(outline)p->getMpv()->setSubOutlineColor(v); else p->getMpv()->setSubColor(v);
    } else return false;
    c->save(); updateSelectionHighlight(); return true;
}

void MenuVideoSubmenu::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    // the playlist opened on the playing file
    if (type == MENU_VIDEO_TYPE_PLAYLIST && visibility == Visibility::Visible && !isVisible()) {
        selectId(playingId());
    }
    Menu::setVisibility(visibility, tweenPlay);
    updateSelectionHighlight();
}

void MenuVideoSubmenu::onUpdate() {
    // paused or resumed elsewhere: its icon
    if (type == MENU_VIDEO_TYPE_PLAYLIST && isVisible() && main->getPlayer()->getMpv()->isPaused() != shownPaused) {
        updateSelectionHighlight();
    }
    Menu::onUpdate();
}

bool MenuVideoSubmenu::onInput(c2d::Input::Player *players) {
    if (players[0].buttons & Input::B) {
        setVisibility(Visibility::Hidden, true);
        if (fromOsd) {
            main->getPlayer()->showOsd();
        } else {
            main->getMenuVideo()->setVisibility(Visibility::Visible, true);
        }
        return true;
    }

    bool result = Menu::onInput(players);
    updateSelectionHighlight();
    return result;
}
