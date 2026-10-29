//
// Settings > Edit files.
//

#include "main.h"
#include "menu_file_editor.h"
#include "keyboard.h"
#include "i18n.h"

using namespace c2d;

#define ID_EDIT_ALL (-100)
#define ID_ADD_LINE (-101)
#define ID_FILE (-102)
#define ID_EXTERNAL (-103)

#ifdef __LINUX__
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
extern char **environ;
#endif

namespace {
    const char *g_files[] = {"pplay.cfg", "mpv/mpv.conf", "mpv/input.conf"};

    std::vector<std::string> splitLines(const std::string &text) {
        std::vector<std::string> out;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos) {
                eol = text.size();
            }
            std::string line = text.substr(pos, eol - pos);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            out.push_back(line);
            pos = eol + 1;
        }
        return out;
    }

    std::string joinLines(const std::vector<std::string> &lines) {
        std::string out;
        for (const auto &line: lines) {
            out += line;
            out += '\n';
        }
        return out;
    }
}

MenuFileEditor::MenuFileEditor(Main *main, const c2d::FloatRect &rect)
    : Menu(main, rect, "Edit files", {MenuItem("", "", MenuItem::Position::Top)}, true) {
    showFiles();
}

std::string MenuFileEditor::fullPath(const std::string &relativePath) const {
    return main->getIo()->getDataPath() + relativePath;
}

void MenuFileEditor::showFiles() {
    currentFile.clear();
    lines.clear();
    setTitle("Edit files");
    setCompact(false);
    std::vector<MenuItem> items;
    for (const char *file: g_files) {
        MenuItem item(file, "", MenuItem::Position::Top, ID_FILE);
        item.label = file;
        items.push_back(item);
    }
    setItems(items);
}

void MenuFileEditor::openFile(const std::string &relativePath, int selectLine) {
    std::string text;
    auto *io = (pplay::Io *) main->getIo();
    if (!io->readText(fullPath(relativePath), text)) {
        text.clear(); // new file
    }
    currentFile = relativePath;
    lines = splitLines(text);
    setTitle(relativePath);
    // more lines on screen
    setCompact(true);

    std::vector<MenuItem> items;
    std::string whole = text;
    if (!whole.empty() && whole.back() == '\n') {
        whole.pop_back();
    }
    const bool canEditAll = pplay::Keyboard::length(whole) <= pplay::Keyboard::maxLength(true);
#ifdef __LINUX__
    items.emplace_back("Open in a text editor", "", MenuItem::Position::Top, ID_EXTERNAL);
#endif
    if (canEditAll) {
        items.emplace_back("Edit whole file", "", MenuItem::Position::Top, ID_EDIT_ALL);
    }
    for (size_t i = 0; i < lines.size(); i++) {
        MenuItem item("line", "", MenuItem::Position::Top, (int) i);
        item.label = lines[i].empty() ? pplay::tr("<empty line>") : lines[i];
        items.push_back(item);
    }
    items.emplace_back("Add a line", "", MenuItem::Position::Top, ID_ADD_LINE);
    setItems(items);

    if (selectLine >= 0) {
        int first = 0;
#ifdef __LINUX__
        first++;
#endif
        int target = std::min(selectLine + first + (canEditAll ? 1 : 0), (int) buttons.size() - 1);
        index = target;
        updateSelectionState();
        ensureSelectionVisible();
        highlight->setPosition(0, buttons[index]->getPosition().y);
    }
}

bool MenuFileEditor::save(const std::vector<std::string> &newLines) {
    auto *io = (pplay::Io *) main->getIo();
    const std::string path = fullPath(currentFile);
    std::string oldText;
    bool existed = io->readText(path, oldText);

    if (currentFile.find('/') != std::string::npos) {
        std::string dir = path.substr(0, path.find_last_of('/'));
        if (!io->exist(dir)) {
            io->create(dir);
        }
    }
    if (!io->writeText(path, joinLines(newLines))) {
        main->showMessage(pplay::tr("Error"), pplay::tr("Could not write") + " " + path, pplay::tr("OK"));
        return false;
    }

    if (currentFile == "pplay.cfg") {
        if (!main->getConfig()->reload()) {
            // invalid: restore the previous file
            if (existed) {
                io->writeText(path, oldText);
            }
            main->getConfig()->reload();
            main->showMessage(pplay::tr("Error"), pplay::tr("Invalid configuration, the changes were discarded"),
                              pplay::tr("OK"));
            return false;
        }
        main->applyConfig();
        main->getStatus()->show(pplay::tr("Info..."), pplay::tr("Configuration applied"));
    } else {
        main->getStatus()->show(pplay::tr("Info..."), pplay::tr("Saved, restart pPlay to apply mpv changes"));
    }
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "MenuFileEditor: saved " + path);
    return true;
}

void MenuFileEditor::editAll() {
    std::string text = joinLines(lines);
    if (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    if (pplay::Keyboard::length(text) > pplay::Keyboard::maxLength(true)) {
        main->showMessage(pplay::tr("Info..."),
                          pplay::tr("The file is too long for the keyboard, edit it line by line"), pplay::tr("OK"));
        return;
    }
    if (!pplay::Keyboard::edit(currentFile, text, true)) {
        return;
    }
    std::string file = currentFile;
    if (save(splitLines(text))) {
        openFile(file);
    }
}

void MenuFileEditor::editLine(int line) {
    std::vector<std::string> newLines = lines;
    std::string text = line < (int) lines.size() ? lines[line] : std::string();
    if (pplay::Keyboard::length(text) > pplay::Keyboard::maxLength(false)) {
        main->showMessage(pplay::tr("Info..."), pplay::tr("The line is too long for the keyboard"), pplay::tr("OK"));
        return;
    }
    if (!pplay::Keyboard::edit(currentFile + ": " + std::to_string(line + 1), text, false)) {
        return;
    }
    if (line < (int) newLines.size()) {
        if (text == newLines[line]) {
            return;
        }
        newLines[line] = text;
    } else {
        newLines.push_back(text);
    }
    std::string file = currentFile;
    if (save(newLines)) {
        openFile(file, line + 1);
    }
}

void MenuFileEditor::onOptionSelection(MenuItem *item) {
    // copies: the buttons are rebuilt below
    const int id = item->id;
    const std::string name = item->name;

    if (currentFile.empty()) {
        if (id == ID_FILE) {
            openFile(name);
        }
        return;
    }
    if (id == ID_EXTERNAL) {
        openExternal();
    } else if (id == ID_EDIT_ALL) {
        editAll();
    } else if (id == ID_ADD_LINE) {
        editLine((int) lines.size());
    } else if (id >= 0) {
        editLine(id);
    }
}

#ifdef __LINUX__
static long long fileTime(const std::string &path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) {
        return -1;
    }
    return (long long) st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
}
#endif

void MenuFileEditor::openExternal() {
#ifdef __LINUX__
    const std::string path = fullPath(currentFile);
    auto *io = (pplay::Io *) main->getIo();
    if (!io->exist(path)) {
        io->writeText(path, "");
    }
    // detached editor
    pid_t pid;
    std::string arg = path;
    char *argv[] = {(char *) "xdg-open", (char *) arg.c_str(), nullptr};
    if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ) != 0) {
        main->showMessage(pplay::tr("Error"), pplay::tr("No text editor found, the file is") + "\n" + path,
                          pplay::tr("OK"));
        return;
    }
    externalPid = pid;
    externalFile = currentFile;
    externalTime = fileTime(path);
    main->getStatus()->show(pplay::tr("Info..."), pplay::tr("Opened in the text editor, applied when saved"));
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "MenuFileEditor: xdg-open " + path);
#endif
}

void MenuFileEditor::onUpdate() {
    Menu::onUpdate();
#ifdef __LINUX__
    if (externalPid > 0) {
        // xdg-open exits once the editor is started
        if (waitpid(externalPid, nullptr, WNOHANG) == externalPid) {
            externalPid = -1;
        }
    }
    if (externalFile.empty() || checkClock.getElapsedTime().asMilliseconds() < 1000) {
        return;
    }
    checkClock.restart();
    const std::string path = fullPath(externalFile);
    long long time = fileTime(path);
    if (time == externalTime) {
        return;
    }
    // saved in the text editor
    externalTime = time;
    if (externalFile == "pplay.cfg") {
        if (main->getConfig()->reload()) {
            main->applyConfig();
            main->getStatus()->show(pplay::tr("Info..."), pplay::tr("Configuration applied"));
        } else {
            main->showMessage(pplay::tr("Error"), pplay::tr("Invalid configuration, fix the file"), pplay::tr("OK"));
        }
    } else {
        main->getStatus()->show(pplay::tr("Info..."), pplay::tr("Saved, restart pPlay to apply mpv changes"));
    }
    if (currentFile == externalFile && isVisible()) {
        openFile(currentFile);
    }
#endif
}

void MenuFileEditor::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    if (visibility == Visibility::Visible && !isVisible()) {
        showFiles();
    }
    Menu::setVisibility(visibility, tweenPlay);
}

bool MenuFileEditor::onInput(c2d::Input::Player *players) {
    if (players[0].buttons & Input::Right || players[0].buttons & Input::B) {
        if (!currentFile.empty()) {
            showFiles();
            Menu::setVisibility(Visibility::Visible, false);
            return true;
        }
        setVisibility(Visibility::Hidden, true);
        main->getMenuMain()->showParent("Edit files");
        return true;
    }
    return Menu::onInput(players);
}
