//
// Created by cpasjuste on 07/12/18.
//

#ifndef PPLAY_MENU_VIDEO_SUBMENU_H
#define PPLAY_MENU_VIDEO_SUBMENU_H

#include "menu.h"

#define MENU_VIDEO_TYPE_VID 0
#define MENU_VIDEO_TYPE_AUD 1
#define MENU_VIDEO_TYPE_SUB 2
#define MENU_VIDEO_TYPE_PLAYLIST 3
#define MENU_VIDEO_TYPE_PL_MODE 4
#define MENU_VIDEO_TYPE_NORMALIZE 5
#define MENU_VIDEO_TYPE_SPEED 6
#define MENU_VIDEO_TYPE_BRIGHTNESS 7
#define MENU_VIDEO_TYPE_VISUALIZER 8
// the value line of the speed / brightness menus
#define MENU_ADJUST_VALUE 100
// "Add file..." item of the audio / subtitles menus
#define MENU_TRACK_ADD (-100)

class MenuVideoSubmenu : public Menu {

public:

    MenuVideoSubmenu(Main *main, const c2d::FloatRect &rect, const std::string &_title,
                     const std::vector<MenuItem> &items, int type);

    bool onInput(c2d::Input::Player *players) override;

    void setSelection(int streamId);

    void setVisibility(c2d::Visibility visibility, bool tweenPlay = true) override;

    // the current values marked again (the playlist: the playing file)
    void updateSelectionHighlight() override;

    int type;
    // opened from the OSD (its button): back there, not to the player menu
    bool fromOsd = false;

private:

    void onOptionSelection(MenuItem *item) override;

    // the playlist item playing, -1 if none
    int playingId() const;
};

#endif //PPLAY_MENU_VIDEO_SUBMENU_H
