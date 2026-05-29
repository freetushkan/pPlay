//
// Created by cpasjuste on 02/10/18.
//
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <cstdint>
#include "main.h"
#include "io.h"
#include "filer.h"
#include "menu_main.h"
#include "menu_video.h"
#ifdef PPLAY_ENABLE_SCRAPPING
#include "scrapper.h"
#endif
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



static size_t findFirstPathSeparator(const std::string &str, size_t from) {
    size_t slash = str.find('/', from);
    size_t backslash = str.find('\\', from);
    if (slash == std::string::npos) return backslash;
    if (backslash == std::string::npos) return slash;
    return std::min(slash, backslash);
}

static std::string normalizeSmbPathSeparators(const std::string &path) {
    if (!c2d::Utility::startWith(path, "smb://")) {
        return path;
    }

    size_t schemeEnd = std::string("smb://").size();
    size_t at = path.find_last_of('@');
    size_t hostStart = at != std::string::npos && at >= schemeEnd ? at + 1 : schemeEnd;
    size_t pathStart = findFirstPathSeparator(path, hostStart);
    if (pathStart == std::string::npos) {
        return path;
    }

    std::string normalized = path.substr(0, pathStart);
    std::string rest = path.substr(pathStart);
    std::replace(rest.begin(), rest.end(), '\\', '/');
    return normalized + rest;
}

static int parseNetworkModule(const std::string &module) {
    if (module.rfind("NETWORK", 0) != 0) {
        return -1;
    }
    int index = std::atoi(module.substr(7).c_str());
    return index >= 1 && index <= 5 ? index : 1;
}

static std::string networkModuleName(int index) {
    return "NETWORK" + std::to_string(index);
}

static std::string ensureTrailingSlash(const std::string &url) {
    std::string normalized = normalizeSmbPathSeparators(url);
    if (normalized.empty() || c2d::Utility::endsWith(normalized, "/")) {
        return normalized;
    }
    return normalized + "/";
}

static std::string normalizePath(const std::string &path) {
    if (path.empty()) return path;
    std::string normalizedInput = normalizeSmbPathSeparators(path);
    size_t schemePos = normalizedInput.find("://");
    std::string prefix;
    std::string rest = normalizedInput;
    if (schemePos != std::string::npos) {
        size_t firstSlash = normalizedInput.find('/', schemePos + 3);
        if (firstSlash == std::string::npos) {
            return normalizedInput + "/";
        }
        prefix = normalizedInput.substr(0, firstSlash);
        rest = normalizedInput.substr(firstSlash);
    }
    bool endsWithDotDot = (rest == "..") || 
        (rest.size() >= 3 && rest.compare(rest.size() - 3, 3, "/..") == 0);
    if (endsWithDotDot) {
        if (rest == "..") {
            rest = "";
        } else {
            rest = rest.substr(0, rest.size() - 3); // убираем "/.."
        }
    }
    bool trailingSlash = !rest.empty() && rest.back() == '/';
    std::vector<std::string> segments;
    size_t start = 0, end = 0;
    while ((end = rest.find('/', start)) != std::string::npos) {
        std::string part = rest.substr(start, end - start);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                if (!segments.empty()) segments.pop_back();
            } else {
                segments.push_back(part);
            }
        }
        start = end + 1;
    }
    std::string lastPart = rest.substr(start);
    if (!lastPart.empty() && lastPart != ".") {
        if (lastPart == "..") {
            if (!segments.empty()) segments.pop_back();
        } else {
            segments.push_back(lastPart);
        }
    }
    std::string normalized = prefix + "/";
    for (size_t i = 0; i < segments.size(); ++i) {
        normalized += segments[i];
        if (i < segments.size() - 1) normalized += "/";
    }
    if (trailingSlash && !segments.empty() && normalized.back() != '/') {
        normalized += "/";
    }
    if (endsWithDotDot) {
        if (normalized.back() != '/') {
            normalized += "/";
        }
        normalized += "..";
    }
    return normalized;
}

static std::string extractDirPath(const std::string &path) {
    if (path.empty()) return "/";
    if (path.back() == '/') {
        return path;
    }
    std::string p = normalizePath(path);
    size_t schemePos = p.find("://");
    size_t minPos = 0;
    if (schemePos != std::string::npos) {
        size_t afterHost = p.find('/', schemePos + 3);
        if (afterHost == std::string::npos) return ensureTrailingSlash(p);
        minPos = afterHost + 1;
    }
    size_t pos = p.find_last_of('/');
    if (pos == 0) return "/";
    return p.substr(0, pos + 1);
}

static std::string getLeafName(const std::string &path) {
    if (path.empty() || path == "/") return "";
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    if (pos + 1 >= path.size()) return "";
    return path.substr(pos + 1);
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

    statusBox = new StatusBox(this, {0, Main::getSize().y - 16});
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

    // status bar
    statusBar = new StatusBar(this);
    statusBar->setLayer(10);
    Main::add(statusBar);
    // Without this trick the status bar is not shown on startup..
    statusBar->setVisibility(Visibility::Hidden, false);
    statusBar->setVisibility(Visibility::Visible, true);

    // ffmpeg player
    player = new Player(this);
    player->setLayer(2);
    Main::add(player);


    if (config->getOption(OPT_DLNA_RENDERER)->getInteger() != 0) {
        uint16_t dlnaPort = (uint16_t) config->getOption(OPT_DLNA_HTTP_PORT)->getInteger();
        dlnaRenderer = new pplay::DlnaRenderer("pPlay", dlnaPort);
        if (!dlnaRenderer->start()) {
            pplay::Utility::log(pplay::Utility::LogLevel::Error, "Main::Main failed to start DLNA renderer");
            delete dlnaRenderer;
            dlnaRenderer = nullptr;
        }
    }

    // main menu
    setCurrentModuleIndex(
        parseNetworkModule(config->getOption(OPT_LAST_MODULE)->getString()));
    std::vector<MenuItem> items;
    items.emplace_back("Local", "home.png", MenuItem::Position::Top, -1);
#ifdef __SWITCH__
    items.emplace_back("Usb", "usb.png", MenuItem::Position::Top, -2);
#endif
    for (int i = 1; i <= 5; i++) {
        if (!config->getOption(PPLAYConfig::networkOption(i))->getString().empty()) {
            items.emplace_back(config->getOption(PPLAYConfig::networkNameOption(i))->getString(),
                "network.png", MenuItem::Position::Top, i);
        }
    }
    items.emplace_back("Settings", "options.png", MenuItem::Position::Top);
    items.emplace_back("Exit", "exit.png", MenuItem::Position::Bottom);
    menu_main = new MenuMain(this, {-250 * scaling.x, 0, 250 * scaling.x, Main::getSize().y}, items);
    menu_main->setVisibility(Visibility::Hidden, false);
    menu_main->setLayer(3);
    Main::add(menu_main);

    // video menu
    items.clear();
    items.emplace_back("Playlist", "playlist.png", MenuItem::Position::Top);
    items.emplace_back("Audio", "audio.png", MenuItem::Position::Top);
    items.emplace_back("Video", "video.png", MenuItem::Position::Top);
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

#ifdef PPLAY_ENABLE_SCRAPPING
    scrapper = new Scrapper(this);
#endif

    // open last
    show(currentModuleIndex > 0 ? MenuType::Network : MenuType::Local);
}

Main::~Main() {
    delete (dlnaRenderer);
#ifdef PPLAY_ENABLE_SCRAPPING
    delete (scrapper);
#endif
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
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "Main::onInput keys=" + pplay::Utility::getKeysString(keys));

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
    if (dlnaRenderer != nullptr) {
        pplay::DlnaRenderer::Command command;
        while (dlnaRenderer->popCommand(command)) {
            switch (command.type) {
                case pplay::DlnaRenderer::CommandType::Play:
                    player->resume();
                    break;
                case pplay::DlnaRenderer::CommandType::Pause:
                    player->pause();
                    break;
                case pplay::DlnaRenderer::CommandType::Stop:
                    player->stop();
                    break;
                case pplay::DlnaRenderer::CommandType::SeekRelative:
                    player->getMpv()->seek(command.value);
                    break;
                case pplay::DlnaRenderer::CommandType::SeekAbsolute:
                    player->getMpv()->seekAbsolute(command.value);
                    break;
                case pplay::DlnaRenderer::CommandType::SetVolume:
                    player->getMpv()->setVolume(command.value);
                    break;
                case pplay::DlnaRenderer::CommandType::LoadUri: {
                    MediaFile media;
                    media.path = command.text;
                    size_t slash = command.text.find_last_of('/');
                    media.name = slash == std::string::npos ? command.text : command.text.substr(slash + 1);
                    if (media.name.empty()) {
                        media.name = "DLNA stream";
                    }
                    player->load(media, true, "pause=no");
                    player->setFullscreen(true);
                    break;
                }
            }
        }

        pplay::DlnaRenderer::State state;
        state.stopped = player->getMpv()->isStopped();
        state.paused = player->getMpv()->isPaused();
        state.position = player->getMpv()->getPosition();
        state.duration = player->getMpv()->getDuration();
        state.volume = player->getMpv()->getVolume();
        state.title = player->getTitle();
        state.uri = player->getMpv()->getCurrentPath();
        dlnaRenderer->updateState(state);
    }

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
    messageBox->setOutlineColor(COLOR_ACCENT);
    Renderer::onUpdate();
}

void Main::show(MenuType type) {
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
        std::string dirPath = extractDirPath(path);
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
        std::string network = config->getOption(
            PPLAYConfig::networkOption(currentModuleIndex))->getString();
        if (network.empty()) {
            show(MenuType::Local);
            return;
        }
        std::string root = ensureTrailingSlash(network);
        std::string path = config->getOption(
            PPLAYConfig::networkLastOption(currentModuleIndex))->getString();
        if (pplayIo->getDeviceType(path) == pplay::Io::DeviceType::Local || path.empty()) {
            path = root;
        }
        std::string dirPath = extractDirPath(path);
        if (!filer->getDir(dirPath)) {
            if (!filer->getDir(root)) {
                messageBox->show("OOPS", filer->getError(), "OK");
                show(MenuType::Local);
            }
        } else {
            filer->selectByPath(path);
        }
        filer->clearHistory();
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
    std::string selectedPath = selected.path.empty() ? filer->getPath() + "/.." : selected.path;
    selectedPath = normalizePath(selectedPath);
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
        "Main::syncLastLocation module="
        + std::string(currentModuleIndex > 0 ? networkModuleName(currentModuleIndex) : "LOCAL")
        + " index=" + std::to_string(currentModuleIndex)
        + " selected=" + selectedPath);
    if (currentModuleIndex > 0) {
        config->getOption(OPT_LAST_MODULE)->setString(networkModuleName(currentModuleIndex));
        if (pplayIo->getDeviceType(selectedPath) != pplay::Io::DeviceType::Local) {
            const char *lastOption = PPLAYConfig::networkLastOption(currentModuleIndex);
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

void Main::setCurrentModuleIndex(int index) {
    currentModuleIndex = index;
}

int Main::getcurrentModuleIndex() {
    return currentModuleIndex;
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

#ifdef PPLAY_ENABLE_SCRAPPING
pplay::Scrapper *Main::getScrapper() {
    return scrapper;
}
#endif

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

#ifdef __PS4__
#include <chrono>
#include <cross2d/platforms/ps4/ps4_clock.h>

c2d::Time c2d::PS4Clock::getCurrentTime() const {
    static const auto start_app = std::chrono::system_clock::now();
    auto now = std::chrono::system_clock::now();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now - start_app).count();
    return c2d::microseconds(static_cast<long>(micros));
}
#endif
