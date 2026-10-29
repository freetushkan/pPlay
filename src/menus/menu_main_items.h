//
// Items of the main menu settings.
//

#ifndef PPLAY_MENU_MAIN_ITEMS_H
#define PPLAY_MENU_MAIN_ITEMS_H

#include <string>
#include <vector>
#include "menu_button.h"

namespace pplay::MenuMainItems {

    // value, -, +
    std::vector<MenuItem> makeAdjustItems();

    std::vector<MenuItem> makeIntItems(const std::vector<std::pair<std::string, int>> &values);

    // "Network x" menu key
    std::string networkKey(int index);

    // mpv language lists (alang / slang), the first item keeps mpv.conf
    std::vector<MenuItem> makeTrackLanguageItems(const std::string &none);

    std::vector<MenuItem> makeAccentColorItems();

    // versions and credits
    std::vector<MenuItem> makeAboutItems(Main *main);
}

#endif //PPLAY_MENU_MAIN_ITEMS_H
