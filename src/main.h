//
// Created by cpasjuste on 02/10/18.
//

#ifndef PPLAY_MAIN_H
#define PPLAY_MAIN_H

#include "cross2d/c2d.h"
#include "filer.h"
#include "player.h"
#include "pplay_config.h"
#include "menu.h"
#include "menu_main.h"
#include "menu_video.h"
#include "status_box.h"
#include "status_bar.h"
#include "scrapper.h"
#include "io.h"
#include "usbfs.h"

//#define FULL_TEXTURE_TEST 1

#define INPUT_DELAY 500
#define ICON_SIZE 24
#define BUTTON_HEIGHT 64

#define COLOR_BG            Color(20, 22, 24, 255)
#define COLOR_BG_ALPHA      Color(20, 22, 24, 128)
#define COLOR_FONT          Color(230, 230, 230, 255)
#define COLOR_HIGHLIGHT     Color(255, 255, 255, 60)
#define COLOR_ACCENT        Color(16, 120, 200, 255)

#define COLOR_BLACK         Color(0x000000FF)
#define COLOR_WHITE         Color(0xFFFFFFFF)
#define COLOR_CLOUD         Color(0xDFE6E9FF)
#define COLOR_GRAY          Color(0x95A5A6FF)
#define COLOR_GRAY_LIGHT    Color(0xD2D7D9FF)
#define COLOR_GRAY_DARK     Color(0x34495EFF)
#define COLOR_BLUE          Color(0x54A0FFFF)
#define COLOR_BLUE_LIGHT    Color(0x81ECECFF)
#define COLOR_PURPLE        Color(0xA29BFEFF)
#define COLOR_PURPLE_LIGHT  Color(0xD6D1FFFF)
#define COLOR_GREEN         Color(0x26DE81FF)
#define COLOR_GREEN_LIGHT   Color(0x7BED9FFF)
#define COLOR_ORANGE        Color(0xFA8231FF)
#define COLOR_ORANGE_LIGHT  Color(0xFFB142FF)
#define COLOR_RED           Color(0xEB4D4BFF)
#define COLOR_VIEWED        Color(0x5B6464FF)


class Main : public c2d::C2DRenderer {

public:

    enum class MenuType {
        Local,
        Network,
        Current,
#ifdef __SWITCH__
        Usb
#endif
    };

    enum class FontSize {
        XS = 14,
        Small = 16,
        Medium = 18,
        Large = 20,
        Big = 22,
        Bigger = 24,
        XL = 36
    };

    explicit Main(const c2d::Vector2f &size);

    ~Main() override;

    void show(MenuType type);

    void setCurrentNetworkIndex(int index);

    bool isExiting();

    bool isRunning();

    void setRunningStop();
    void syncLastLocation();

    MenuMain *getMenuMain();

    MenuVideo *getMenuVideo();

    Player *getPlayer();

    Filer *getFiler();

    PPLAYConfig *getConfig();

    c2d::Font *getFont() override;

    c2d::MessageBox *getMessageBox();

    StatusBox *getStatus();

    StatusBar *getStatusBar();

    pplay::Scrapper *getScrapper();

    c2d::Vector2f getScaling();

    c2d::Vector2f getScaled(const c2d::Vector2f &v) {
        return {v.x * scaling.x, v.y * scaling.y};
    }

    c2d::Vector2f getScaled(float x, float y) {
        return {x * scaling.x, y * scaling.y};
    }

    c2d::FloatRect getScaled(const c2d::FloatRect &rect) {
        return {rect.left * scaling.x, rect.top * scaling.y,
                rect.width * scaling.x, rect.height * scaling.y};
    }

    unsigned int getFontSize(FontSize fontSize);

    void quit();

private:

    bool onInput(c2d::Input::Player *players) override;

    void onUpdate() override;

    pplay::Io *pplayIo;
    c2d::Font *font;
    c2d::Clock *timer;
    c2d::MessageBox *messageBox;
    StatusBox *statusBox;
    PPLAYConfig *config;
    Filer *filer;
    StatusBar *statusBar;
    Player *player;
    MenuMain *menu_main;
    MenuVideo *menu_video;
    pplay::Scrapper *scrapper;
    unsigned int oldKeys = 0;
    c2d::Vector2f scaling = {1, 1};

    bool exit = false;
    bool running = true;
    MenuType currentMenuType = MenuType::Local;
    int currentNetworkIndex = 1;
};

#endif //PPLAY_MAIN_H
