//
// Created by cpasjuste on 02/10/18.
//
#include <cstdlib>
#include <sstream>
#include "main.h"
#include "io.h"
#include "filer.h"
#include "menu_main.h"
#include "menu_video.h"
#include "scrapper.h"
#include "utility.h"

#ifdef __SWITCH__

static AppletHookCookie applet_hook_cookie;

static void on_applet_hook(AppletHookType hook, void *arg) {

    Main *main = (Main *) arg;

    switch (hook) {
        case AppletHookType_OnExitRequest:
            main->quit();
            break;
        case AppletHookType_OnFocusState:
            if (appletGetFocusState() == AppletFocusState_InFocus) {
                if (main->getPlayer()->getMpv()->isPaused()) {
                    main->getPlayer()->resume();
                }
            } else {
                if (!main->getPlayer()->getMpv()->isPaused()) {
                    main->getPlayer()->pause();
                }
            }
            break;
        default:
            break;
    }
}

#elif __PS4__

#include <orbis/Sysmodule.h>

extern "C" int sceSystemServiceLoadExec(const char *path, const char *args[]);
#endif

using namespace c2d;
using namespace c2d::config;
using namespace pplay;


static int parseNetworkModule(const std::string &module) {
    if (module.rfind("NETWORK", 0) != 0) {
        return 0;
    }
    if (module.size() == 7) {
        return 1;
    }
    int index = std::atoi(module.substr(7).c_str());
    return index >= 1 && index <= 5 ? index : 1;
}

static std::string networkModuleName(int index) {
    return "NETWORK" + std::to_string(index);
}

static std::string ensureTrailingSlash(const std::string &url) {
    if (url.empty() || c2d::Utility::endsWith(url, "/")) {
        return url;
    }
    return url + "/";
}

static std::string normalizePath(const std::string &path) {
    if (path.empty()) return path;
    size_t schemePos = path.find("://");
    std::string prefix;
    std::string rest = path;
    if (schemePos != std::string::npos) {
        size_t firstSlash = path.find('/', schemePos + 3);
        if (firstSlash == std::string::npos) {
            return ensureTrailingSlash(path);
        }
        prefix = path.substr(0, firstSlash);
        rest = path.substr(firstSlash);
    }
    std::vector<std::string> out;
    std::stringstream ss(rest);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (!out.empty()) out.pop_back();
            continue;
        }
        out.push_back(part);
    }
    std::string normalized = schemePos == std::string::npos ? "/" : prefix + "/";
    for (size_t i = 0; i < out.size(); i++) {
        normalized += out[i];
        if (i + 1 < out.size()) normalized += "/";
    }
    return normalized.empty() ? "/" : normalized;
}

static std::string getParentPath(const std::string &path) {
    std::string p = normalizePath(path);
    if (p.empty() || p == "/") return "/";
    size_t schemePos = p.find("://");
    size_t minPos = 0;
    if (schemePos != std::string::npos) {
        size_t afterHost = p.find('/', schemePos + 3);
        if (afterHost == std::string::npos) return ensureTrailingSlash(p);
        minPos = afterHost + 1;
    }
    size_t pos = p.find_last_of('/');
    if (pos == std::string::npos || pos < minPos) return p;
    if (pos == 0) return "/";
    return p.substr(0, pos);
}

static std::string getLeafName(const std::string &path) {
    if (path.empty() || path == "/") return "";
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    if (pos + 1 >= path.size()) return "";
    return path.substr(pos + 1);
}

static bool startsWithPath(const std::string &path, const std::string &prefix) {
    if (prefix.empty()) return true;
    if (path.size() < prefix.size()) return false;
    if (path.compare(0, prefix.size(), prefix) != 0) return false;
    return path.size() == prefix.size() || path[prefix.size()] == '/';
}

static std::string clampNetworkPathToBase(const std::string &path, const std::string &baseUrl) {
    std::string base = normalizePath(ensureTrailingSlash(baseUrl));
    std::string normalized = normalizePath(path);
    if (!startsWithPath(normalized, base)) {
        return base;
    }
    return normalized;
}

Main::Main(const c2d::Vector2f &size) : C2DRenderer(size) {

#ifndef NDEBUG
    Renderer::setPrintStats(true);
#endif

    // custom io
    pplayIo = new pplay::Io();
    Main::setIo(pplayIo);
    setClearColor(COLOR_BG);

    // create pplay data directory
    pplayIo->create(pplayIo->getDataPath() + "mpv");

    // configure input
    Main::getInput()->setRepeatDelay(INPUT_DELAY);

    // create a timer
    timer = new C2DClock();

    // init/load config file
    config = new PPLAYConfig(this);
    pplay::Utility::setLogLevel((pplay::Utility::LogLevel) config->getOption(OPT_LOG_LEVEL)->getInteger());

    // scaling
    scaling = {size.x / 1280.0f, size.y / 720.0f};

    // font
    font = new Font();
    std::string customFont = Main::getIo()->getDataPath() + "font.ttf";
    std::string defaultFont = Main::getIo()->getRomFsPath() + "skin/font.ttf";
    std::string fontPath = Main::getIo()->exist(customFont) ? customFont : defaultFont;
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Main::font path=" + fontPath);
    font->loadFromFile(fontPath);
    font->setFilter(Texture::Filter::Point);
    font->setOffset({0, -4.0f});

    statusBox = new StatusBox(this, {0, Main::getSize().y - 16});  // TODO: Change position?
    statusBox->setOrigin(Origin::BottomLeft);
    statusBox->setLayer(10);
    Main::add(statusBox);

    // media information cache
    Main::getIo()->create(Main::getIo()->getDataPath() + "cache");

    // create filer
    FloatRect filerRect = {0, 0, Main::getSize().x, Main::getSize().y};
    filer = new Filer(this, "/", filerRect);
    filer->setLayer(1);
    Main::add(filer);
    currentNetworkIndex = parseNetworkModule(config->getOption(OPT_LAST_MODULE)->getString());
    currentMenuType = currentNetworkIndex > 0 ? MenuType::Network : MenuType::Local;
    if (currentMenuType == MenuType::Network) {
        std::string network = config->getOption(PPLAYConfig::networkOption(currentNetworkIndex))->getString();
        if (network.empty()) {
            currentMenuType = MenuType::Local;
        } else {
            std::string root = ensureTrailingSlash(network);
            std::string path = clampNetworkPathToBase(
                    config->getOption(PPLAYConfig::networkLastOption(currentNetworkIndex))->getString(),
                    network);
            if (pplayIo->getDeviceType(path) == pplay::Io::DeviceType::Local || path.empty()) {
                path = root;
            }
            std::string dirPath = getParentPath(path);
            if (!filer->getDir(dirPath)) {
                filer->getDir(root);
            } else if (!filer->selectByPath(path)) {
                filer->getDir(root);
            } else {
                filer->clearHistory();
            }
        }
    }
    else if (currentMenuType == MenuType::Local) {
        std::string path = normalizePath(config->getOption(OPT_LAST_LOCAL_PATH)->getString());
        if (path.empty()) {
            path = config->getOption(OPT_HOME_PATH)->getString();
        }
        std::string dirPath = getParentPath(path);
        if (!filer->getDir(dirPath)) {
            filer->getDir(config->getOption(OPT_HOME_PATH)->getString());
        } else {
            filer->selectByPath(path);
        }
    }

    // status bar
    statusBar = new StatusBar(this);
    statusBar->setLayer(10);
    statusBar->setVisibility(Visibility::Visible, true);
    Main::add(statusBar);

    // ffmpeg player
    player = new Player(this);
    player->setLayer(2);
    Main::add(player);

    // main menu
    std::vector<MenuItem> items;
    items.emplace_back("Local", "home.png", MenuItem::Position::Top);
#ifdef __SWITCH__
    items.emplace_back("Usb", "usb.png", MenuItem::Position::Top);
#endif
    for (int i = 1; i <= 5; i++) {
        if (!config->getOption(PPLAYConfig::networkOption(i))->getString().empty()) {
            items.emplace_back("Network " + std::to_string(i), "network.png", MenuItem::Position::Top, i);
        }
    }
#ifdef __SWITCH__
    items.emplace_back("Options", "options.png", MenuItem::Position::Top);
#endif
    items.emplace_back("Exit", "exit.png", MenuItem::Position::Bottom);
    menu_main = new MenuMain(this, {-250 * scaling.x, 0, 250 * scaling.x, Main::getSize().y}, items);
    menu_main->setVisibility(Visibility::Hidden, false);
    menu_main->setLayer(3);
    Main::add(menu_main);

    // video menu
    items.clear();
    items.emplace_back("Video", "video.png", MenuItem::Position::Top);
    items.emplace_back("Audio", "audio.png", MenuItem::Position::Top);
    items.emplace_back("Subtitles", "subtitles.png", MenuItem::Position::Top);
    items.emplace_back("Stop", "exit.png", MenuItem::Position::Bottom);
    menu_video = new MenuVideo(this, {Main::getSize().x, 0, 250 * scaling.x, Main::getSize().y}, items);
    menu_video->setVisibility(Visibility::Hidden, false);
    menu_video->setLayer(3);
    Main::add(menu_video);

    // a messagebox...
    float w = Main::getSize().x / 3;
    float h = Main::getSize().y / 3;
    messageBox = new MessageBox({Main::getSize().x / 2, Main::getSize().y / 2, w, h},
                                Main::getInput(), Main::getFont(), (int) getFontSize(Main::FontSize::Medium));
    messageBox->setOrigin(Origin::Center);
    messageBox->setFillColor(COLOR_BG);
    messageBox->setAlpha(240);
    messageBox->setOutlineColor(COLOR_ACCENT);
    messageBox->setOutlineThickness(2);
    messageBox->getTitleText()->setOutlineThickness(0);
    messageBox->getMessageText()->setOutlineThickness(0);
    messageBox->getButton(0)->setOutlineThickness(3);
    messageBox->getButton(1)->setOutlineThickness(3);
    Main::add(messageBox);

    scrapper = new Scrapper(this);
}

Main::~Main() {
    delete (scrapper);
    delete (config);
    delete (timer);
    delete (font);
}

bool Main::onInput(c2d::Input::Player *players) {

    if (messageBox->isVisible()) {
        // don't handle input if message box is visible
        return false;
    }

    unsigned int keys = players[0].buttons;

    if (keys & Input::Quit) {
        if (player->isFullscreen()) {
            player->setFullscreen(false);
            filer->setVisibility(Visibility::Visible, true);
        } else {
            quit();
        }
    }

    return Renderer::onInput(players);
}

void Main::onUpdate() {
    unsigned int keys = getInput()->getButtons();
    if (keys != Input::Delay) {
        bool changed = (oldKeys ^ keys) != 0;
        oldKeys = keys;
        if (!changed) {
            if (timer->getElapsedTime().asSeconds() > 5) {
                getInput()->setRepeatDelay(INPUT_DELAY / 20);
            } else if (timer->getElapsedTime().asSeconds() > 3) {
                getInput()->setRepeatDelay(INPUT_DELAY / 8);
            } else if (timer->getElapsedTime().asSeconds() > 1) {
                getInput()->setRepeatDelay(INPUT_DELAY / 4);
            }
        } else {
            getInput()->setRepeatDelay(INPUT_DELAY);
            timer->restart();
        }
    }

    C2DRenderer::onUpdate();
}

void Main::show(MenuType type) {
    if (type == MenuType::Current) {
        type = currentMenuType;
    } else {
        currentMenuType = type;
    }

    if (player->getMpv()->isStopped() && player->isFullscreen()) {
        player->setFullscreen(false);
    }

    filer->setVisibility(Visibility::Visible, true);
    if (type == MenuType::Local) {
#ifdef __SWITCH__
        usbHsFsExit();
#endif
        std::string path = normalizePath(config->getOption(OPT_LAST_LOCAL_PATH)->getString());
        if (path.empty()) {
            path = config->getOption(OPT_HOME_PATH)->getString();
        }
        std::string dirPath = getParentPath(path);
        if (!filer->getDir(dirPath)) {
            if (filer->getDir(config->getOption(OPT_HOME_PATH)->getString())) {
                filer->clearHistory();
            }
        } else {
            filer->selectByPath(path);
        }
#ifdef __SWITCH__
        } else if (type == MenuType::Usb) {
            usbInit();
            filer->getDir(config->getOption(OPT_UMS_DEVICE)->getString());
#endif
    } else {
#ifdef __SWITCH__
        usbHsFsExit();
#endif
        std::string network = config->getOption(PPLAYConfig::networkOption(currentNetworkIndex))->getString();
        if (network.empty()) {
            show(MenuType::Local);
            return;
        }
        std::string root = ensureTrailingSlash(network);
        std::string path = clampNetworkPathToBase(
                config->getOption(PPLAYConfig::networkLastOption(currentNetworkIndex))->getString(),
                network);
        if (pplayIo->getDeviceType(path) == pplay::Io::DeviceType::Local || path.empty()) {
            path = root;
        }
        std::string dirPath = getParentPath(path);
        if (!filer->getDir(dirPath)) {
            if (!filer->getDir(root)) {
                messageBox->show("OOPS", filer->getError(), "OK");
                show(MenuType::Local);
            }
        } else if (!filer->selectByPath(path)) {
            filer->getDir(root);
        } else {
            filer->clearHistory();
        }
    }
}

bool Main::isExiting() {
    return exit;
}

bool Main::isRunning() {
    return running;
}

void Main::setRunningStop() {
    printf("Main::setRunningStop()\n");
    running = false;
}

void Main::quit() {
    syncLastLocation();
    config->save();
    exit = true;
    if (player->getMpv()->isStopped()) {
        running = false;
    } else {
        player->stop();
    }
}

void Main::syncLastLocation() {
    MediaFile selected = filer->getSelection();
    std::string selectedPath = selected.path.empty() ? filer->getPath() : selected.path;
    if (selected.name == "..") {
        selectedPath = filer->getPath() + "/..";
    }
    selectedPath = normalizePath(selectedPath);
    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Main::syncLastLocation module="
                        + std::string(currentMenuType == MenuType::Network ? networkModuleName(currentNetworkIndex) : "LOCAL")
                        + " selected=" + selectedPath
                        + " leaf=" + getLeafName(selectedPath));
    if (currentMenuType == MenuType::Network) {
        config->getOption(OPT_LAST_MODULE)->setString(networkModuleName(currentNetworkIndex));
        if (pplayIo->getDeviceType(selectedPath) != pplay::Io::DeviceType::Local) {
            selectedPath = clampNetworkPathToBase(
                    selectedPath,
                    config->getOption(PPLAYConfig::networkOption(currentNetworkIndex))->getString());
            const char *lastOption = PPLAYConfig::networkLastOption(currentNetworkIndex);
            if (getLeafName(selectedPath).empty()) {
                config->getOption(lastOption)->setString(ensureTrailingSlash(selectedPath));
            } else {
                config->getOption(lastOption)->setString(selectedPath);
            }
        }
    } else {
        config->getOption(OPT_LAST_MODULE)->setString("LOCAL");
        if (getLeafName(selectedPath).empty()) {
            config->getOption(OPT_LAST_LOCAL_PATH)->setString(ensureTrailingSlash(selectedPath));
        } else {
            config->getOption(OPT_LAST_LOCAL_PATH)->setString(selectedPath);
        }
    }
    config->save();
}

void Main::setCurrentNetworkIndex(int index) {
    if (index >= 1 && index <= 5) {
        currentNetworkIndex = index;
    }
}

Player *Main::getPlayer() {
    return player;
}

Filer *Main::getFiler() {
    return filer;
}

MenuMain *Main::getMenuMain() {
    return menu_main;
}

MenuVideo *Main::getMenuVideo() {
    return menu_video;
}

PPLAYConfig *Main::getConfig() {
    return config;
}

c2d::Font *Main::getFont() {
    return font;
}

c2d::MessageBox *Main::getMessageBox() {
    return messageBox;
}

StatusBox *Main::getStatus() {
    return statusBox;
}

Vector2f Main::getScaling() {
    return scaling;
}

unsigned int Main::getFontSize(FontSize fontSize) {
    return (unsigned int) ((float) fontSize * scaling.y);
}

StatusBar *Main::getStatusBar() {
    return statusBar;
}

pplay::Scrapper *Main::getScrapper() {
    return scrapper;
}

int main() {

    Vector2f size = {1920, 1080};

#ifdef __SWITCH__
#ifdef NDEBUG
    socketInitializeDefault();
#endif
    appletMainLoop();
    if (appletGetOperationMode() == AppletOperationMode_Console) {
        size = {1920, 1080};
    }
#elif __PS4__
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
#endif

    Main *main = new Main(size);

#ifdef __SWITCH__
    appletLockExit();
    appletHook(&applet_hook_cookie, on_applet_hook, main);
    appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
#endif

    while (main->isRunning()) {
        main->flip();
    }

    delete (main);

#ifdef __SWITCH__
    usbHsFsExit();
    appletUnhook(&applet_hook_cookie);
    appletUnlockExit();
#ifdef NDEBUG
    socketExit();
#endif
#elif __PS4__
    sceSystemServiceLoadExec((char *) "exit", nullptr);
    while (true) {}
#endif

    return 0;
}
