//
// Settings > Edit files: pplay.cfg and mpv files, edited with the system keyboard
// (whole file or line by line).
//

#ifndef PPLAY_MENU_FILE_EDITOR_H
#define PPLAY_MENU_FILE_EDITOR_H

#include <string>
#include <vector>
#include "menu.h"

class MenuFileEditor : public Menu {

public:

    MenuFileEditor(Main *main, const c2d::FloatRect &rect);

    void showFiles();

    void setVisibility(c2d::Visibility visibility, bool tweenPlay = true) override;

    bool onInput(c2d::Input::Player *players) override;

    void onUpdate() override;

private:

    void onOptionSelection(MenuItem *item) override;

    void openFile(const std::string &relativePath, int selectLine = -1);

    void editAll();

    void editLine(int line);

    // write and apply, false (nothing changed) on errors
    bool save(const std::vector<std::string> &newLines);

    std::string fullPath(const std::string &relativePath) const;

    // Linux: the desktop text editor (xdg-open), the file applied when saved
    void openExternal();

    std::string currentFile; // "" when the files list is shown
    std::vector<std::string> lines;
    std::string externalFile;
    long long externalTime = -1;
    int externalPid = -1;
    c2d::C2DClock checkClock;
};

#endif //PPLAY_MENU_FILE_EDITOR_H
