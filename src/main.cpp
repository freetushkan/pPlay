//
// Created by cpasjuste on 02/10/18.
//

#include <algorithm>
#include <cmath>

#include "main.h"
#include "encodings.h"
#include "poster_cache.h"
#include "i18n.h"
#include "input_map.h"
#include "io.h"
#include "net_time.h"
#include "platform.h"
#include "pplay_config.h"
#include "utility.h"

using namespace c2d;
using namespace pplay;

Main::Main(const c2d::Vector2f &size, pplay::Io *io, PPLAYConfig *cfg) : C2DRenderer(size) {

#ifndef NDEBUG
    Renderer::setPrintStats(true);
#endif

    pplayIo = io;
    config = cfg;
    Main::setIo(pplayIo);
    pplayIo->setConfig(config);
    setClearColor(COLOR_BG);

    scaling = {Main::getSize().x / 1280.0f, Main::getSize().y / 720.0f};
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Main: resolution " + std::to_string((int) Main::getSize().x)
                                                                + "x" + std::to_string((int) Main::getSize().y));

    Main::getInput()->setRepeatDelay(INPUT_DELAY);
    pplay::InputMap::setup(Main::getInput());
    timer = new C2DClock();

    createFont();

    statusBox = new StatusBox(this, {0, Main::getSize().y - 16});
    statusBox->setOrigin(Origin::BottomLeft);
    statusBox->setLayer(10);
    Main::add(statusBox);

    // media information, pictures cache: what is not used for a month removed
    pplayIo->create(pplayIo->getDataPath() + "cache");
    pplay::PosterCache::cleanup(30);

    filer = new Filer(this, "/", {0, 0, Main::getSize().x, Main::getSize().y});
    filer->setLayer(1);
    Main::add(filer);

    statusBar = new StatusBar(this);
    statusBar->setLayer(10);
    Main::add(statusBar);
    // Without this trick the status bar is not shown on startup..
    statusBar->setVisibility(Visibility::Hidden, false);
    statusBar->setVisibility(Visibility::Visible, true);

    player = new Player(this);
    player->setLayer(2);
    Main::add(player);

    // under the menus: the rest darkened while one is open
    dim = new RectangleShape(Main::getSize());
    dim->setFillColor(Color::Transparent);
    dim->setLayer(3);
    Main::add(dim);

    createMenus();
    createMessageBox();

    pplay::Utility::log(pplay::Utility::LogLevel::Debug, "Main: ui created");

    showLast();
}

void Main::createFont() {
    font = new Font();
    font->loadFromFile(pplayIo->getDataPath() + "skin/font.ttf");
#ifdef __LINUX__
    // the ui is scaled to the window: smoother glyphs (on whole pixels, see text_snap.cpp)
    font->setFilter(Texture::Filter::Linear);
#else
    font->setFilter(Texture::Filter::Point);
#endif
    font->setOffset({0, -4.0f});
}

void Main::createMenus() {
    std::vector<MenuItem> items;
    items.emplace_back("Local", "home.png", MenuItem::Position::Top, -1);
#ifdef __SWITCH__
    items.emplace_back("Usb", "usb.png", MenuItem::Position::Top, -2);
#endif
    for (int i = 1; i <= PPLAY_MAX_NETWORKS; i++) {
        const bool active = !config->getOption(PPLAYConfig::networkOption(i))->getString().empty();
        // name: menu key, label: network name
        MenuItem item("Network " + std::to_string(i), "network.png", MenuItem::Position::Top, i, active, "", active);
        item.label = config->getOption(PPLAYConfig::networkNameOption(i))->getString();
        items.push_back(item);
    }
    items.emplace_back("Settings", "options.png", MenuItem::Position::Top);
    items.emplace_back("Exit", "exit.png", MenuItem::Position::Bottom);
    menu_main = new MenuMain(this, {-300 * scaling.x, 0, 300 * scaling.x, Main::getSize().y}, items);
    menu_main->setVisibility(Visibility::Hidden, false);
    menu_main->setLayer(3);
    Main::add(menu_main);

    items.clear();
    items.emplace_back("Playback mode", "pl_mode.png", MenuItem::Position::Top);
    items.emplace_back("Playlist", "playlist.png", MenuItem::Position::Top);
    items.emplace_back("Audio", "audio.png", MenuItem::Position::Top);
    items.emplace_back("Video", "video.png", MenuItem::Position::Top);
    items.emplace_back("Subtitles", "subtitles.png", MenuItem::Position::Top);
    items.emplace_back("Volume normalization", "normalize.png", MenuItem::Position::Top);
    items.emplace_back("Visualization", "visualizer.png", MenuItem::Position::Top);
    items.emplace_back("Speed", "speed.png", MenuItem::Position::Top);
    items.emplace_back("Brightness", "brightness.png", MenuItem::Position::Top);
    items.emplace_back("Information", "info.png", MenuItem::Position::Top);
    items.emplace_back("Stop", "stop.png", MenuItem::Position::Bottom);
    float inset = 16 * scaling.y;
    menu_video = new MenuVideo(this, {Main::getSize().x, inset, 420 * scaling.x, Main::getSize().y - 2 * inset},
                               items);
    menu_video->setVisibility(Visibility::Hidden, false);
    menu_video->setLayer(3);
    Main::add(menu_video);
}

void Main::createMessageBox() {
    float w = Main::getSize().x / 3;
    float h = Main::getSize().y / 3;
    messageBox = new MessageBox({Main::getSize().x / 2, Main::getSize().y / 2, w, h}, Main::getInput(), Main::getFont(),
                                (int) getFontSize(Main::FontSize::Medium));
    messageBox->setOrigin(Origin::Center);
    messageBox->setFillColor(COLOR_BG);
    messageBox->setAlpha(240);
    messageBox->setOutlineColor(COLOR_ACCENT);
    messageBox->setOutlineThickness(2);
    messageBox->getTitleText()->setOutlineThickness(0);
    messageBox->getMessageText()->setOutlineThickness(0);
    Main::add(messageBox);
}

void Main::retranslate() {
    for (auto *menu: Menu::all()) {
        menu->retranslate();
    }
    if (menu_main != nullptr) {
        menu_main->reloadModules();
    }
    filer->refresh();
}

void Main::setLanguage(const std::string &code) {
    pplay::I18n::load(code);
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Main: language " + pplay::I18n::current());
    retranslate();
}

void Main::applyConfig() {
    pplay::Utility::setLogLevel((pplay::Utility::LogLevel) config->getOption(OPT_LOG_LEVEL)->getInteger());
    int codePage = config->getOption(OPT_TEXT_ENCODING)->getInteger();
    if (codePage != encoding::g_preferred) {
        encoding::setPreferred(codePage);
        filer->refresh();
    }
    pplay::Utility::setAccentColor(config->getOption(OPT_ACCENT_COLOR)->getString());
    player->applyConfig();
    if (pplay::I18n::current() != c2d::Utility::toLower(config->getOption(OPT_LANGUAGE)->getString())) {
        setLanguage(config->getOption(OPT_LANGUAGE)->getString());
    } else {
        retranslate();
    }
}

Main::~Main() {
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
    if (keys == 0 || keys == Input::Button::Delay) {
        return true;
    }
    pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                        "Main::onInput keys=" + pplay::Utility::getKeysString(keys) + pplay::InputMap::toString(keys));

    // media keys: the player, whatever is shown
    if (keys & pplay::InputMap::Media) {
        player->onMediaKeys(keys);
        return true;
    }

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
    if (keys != Input::Delay && keys != oldKeys) {
        oldKeys = keys;
        timer->restart();
    }
    int heldMs = oldKeys ? (int) timer->getElapsedTime().asMilliseconds() : 0;
    getInput()->setRepeatDelay(pplay::InputMap::repeatDelay(heldMs));

    messageBox->setOutlineColor(COLOR_ACCENT);

    bool menu = false;
    for (auto *m: Menu::all()) {
        menu |= m->isVisible();
    }
    float dt = std::min(0.1f, dimClock.restart().asSeconds());
    dimAlpha += ((menu ? 110.0f : 0.0f) - dimAlpha) * std::min(1.0f, dt * 12);
    dim->setFillColor(Color(0, 0, 0, (uint8_t) std::lround(dimAlpha)));
    dim->setVisibility(dimAlpha >= 1 ? Visibility::Visible : Visibility::Hidden);

    std::string clockWarning;
    if (!messageBox->isVisible() && pplay::NetTime::takeWarning(&clockWarning)) {
        showMessage(pplay::tr("Clock"), clockWarning, pplay::tr("OK"));
    }

    // media keys and other devices
    unsigned int pending = pplay::InputMap::takePending() | pplay::InputMap::update();
    if (pending != 0) {
        Input::Player *players = getInput()->getPlayers();
        unsigned int saved = players[0].buttons;
        players[0].buttons = pending;
        onInput(players);
        players[0].buttons = saved;
    }

    Renderer::onUpdate();
}

bool Main::isExiting() {
    return exit;
}

bool Main::isRunning() {
    return running;
}

void Main::setRunningStop() {
    running = false;
}

void Main::quit() {
#ifdef __SWITCH__
    usbExit();
#endif
    syncLastLocation();
    config->save();
    exit = true;
    if (player->getMpv()->isStopped()) {
        running = false;
    } else {
        player->stop();
    }
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

void Main::reloadMainMenuModules() {
    if (menu_main != nullptr) {
        menu_main->reloadModules();
    }
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

int Main::showMessage(const std::string &title, const std::string &message, const std::string &buttonLeft,
                      const std::string &buttonRight) {
    float width = Main::getSize().x * 0.4f;
    float margin = 16 * scaling.y;
    Text *text = messageBox->getMessageText();
    float fontSize = (float) text->getCharacterSize();
    float textTop = text->getPosition().y;
    bool buttons = !buttonLeft.empty() || !buttonRight.empty();
    float buttonHeight = buttons ? messageBox->getButton(0)->getSize().y + 16 : 0;

    // measure the message (no height limit)
    messageBox->setSize(width, Main::getSize().y / 3);
    text->setSizeMax(width - fontSize * 2, 0);
    text->setString(message);
    float textHeight = message.empty() ? 0 : text->getLocalBounds().top + text->getLocalBounds().height;

    float height = textTop + textHeight + margin + buttonHeight + margin;
    height = std::max(height, Main::getSize().y / 4);
    height = std::min(height, Main::getSize().y * 0.9f);
    messageBox->setSize(width, height);
    text->setSizeMax(width - fontSize * 2, height - textTop - buttonHeight - margin * 2);
    messageBox->setOrigin(Origin::Center);
    messageBox->setPosition(Main::getSize().x / 2, Main::getSize().y / 2);

    messageBox->setOutlineColor(COLOR_ACCENT);
    messageBox->setSelectedColor(COLOR_ACCENT, COLOR_FONT);
    messageBox->setNotSelectedColor(COLOR_BG, Color(150, 150, 150));
    for (int i = 0; i < 2; i++) {
        messageBox->getButton(i)->setOutlineThickness(2);
        messageBox->getButton(i)->getText()->setOutlineThickness(0);
    }

    return messageBox->show(title, message, buttonLeft, buttonRight);
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

// platform, data directory, configuration and language before the ui
int main() {
    SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, "1048576");
    if (!pplay::Platform::init()) {
        return 0;
    }

    auto *io = new pplay::Io();
    io->create(io->getDataPath());
    pplay::Utility::setDataPath(io->getDataPath());
#if defined(__PS4__) || defined(__SWITCH__) || defined(__LINUX__)
    // romfs (skin, mpv, lang...) -> data directory
    io->syncRomFs();
#endif

    auto *config = new PPLAYConfig(io);
    io->setConfig(config);
    pplay::Utility::setLogLevel((pplay::Utility::LogLevel) config->getOption(OPT_LOG_LEVEL)->getInteger());
    encoding::setPreferred(config->getOption(OPT_TEXT_ENCODING)->getInteger());
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "pPlay " APP_VERSION_FULL " starting");

    // bundled languages, then the user ones
    pplay::I18n::setSearchPaths({io->getRomFsPath() + "lang", io->getDataPath() + "lang"});
    pplay::I18n::load(config->getOption(OPT_LANGUAGE)->getString());

    c2d::Vector2f size = pplay::Platform::resolution();

    auto *main = new Main(size, io, config);
    pplay::Platform::started(main);
    pplay::NetTime::start();

    while (main->isRunning()) {
        main->flip();
        pplay::Platform::frameDone(main);
    }

    pplay::NetTime::stop();
    delete (main);

    pplay::Platform::exit();

    return 0;
}
