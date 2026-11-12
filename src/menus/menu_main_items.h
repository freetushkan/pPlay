//
// Items of the main menu settings.
//

#ifndef PPLAY_MENU_MAIN_ITEMS_H
#define PPLAY_MENU_MAIN_ITEMS_H

#include <string>
#include <vector>
#include "menu_button.h"

class PPLAYConfig;

namespace pplay::MenuMainItems {

    // value, -, +
    std::vector<MenuItem> makeAdjustItems();

    std::vector<MenuItem> makeIntItems(const std::vector<std::pair<std::string, int>> &values);

    // durations in minutes, named "1 h 15 min"; the special ones before / after
    std::vector<MenuItem> makeMinuteItems(const std::vector<int> &minutes,
                                          const std::vector<std::pair<std::string, int>> &before,
                                          const std::vector<std::pair<std::string, int>> &after);

    // "Network x" menu key
    std::string networkKey(int index);

    // skin icon of a network (by its type, else its url)
    std::string networkIcon(PPLAYConfig *config, int index);

    // mpv language lists (alang / slang), the first item keeps mpv.conf
    std::vector<MenuItem> makeTrackLanguageItems(const std::string &none);

    std::vector<MenuItem> makeAccentColorItems();

    // versions and credits
    std::vector<MenuItem> makeAboutItems(Main *main);
}

#endif //PPLAY_MENU_MAIN_ITEMS_H
