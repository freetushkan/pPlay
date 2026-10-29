//
// Linux window (see desktop_window.h).
//

#include "desktop_window.h"

#ifdef __LINUX__

#include <algorithm>
#include <cmath>

#include "main.h"
#include "input_map.h"

namespace {
    // the ui size, scaled to the window
    c2d::Vector2f g_ui;
    int g_width = 0, g_height = 0;

    SDL_Window *window() {
        return ((c2d::SDL2Renderer *) c2d_renderer)->getWindow();
    }
}

void pplay::DesktopWindow::setup(Main *main) {
    g_ui = main->getSize();
    SDL_Window *w = window();
    SDL_SetWindowTitle(w, "pPlay");
    SDL_SetWindowResizable(w, SDL_TRUE);
    // smaller, the texts are not readable
    SDL_SetWindowMinimumSize(w, (int) g_ui.x / 3, (int) g_ui.y / 3);

    // fit the desktop
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(w), &mode) == 0) {
        float scale = std::min(1.0f, std::min((float) mode.w * 0.85f / g_ui.x, (float) mode.h * 0.85f / g_ui.y));
        SDL_SetWindowSize(w, (int) (g_ui.x * scale), (int) (g_ui.y * scale));
        SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    // mapped again: some window managers update the decorations (close button) on map only
    SDL_HideWindow(w);
    SDL_ShowWindow(w);
    update(main);
}

void pplay::DesktopWindow::update(Main *main) {
    SDL_Window *w = window();
    if (pplay::InputMap::takeFullscreenToggle()) {
        bool fullscreen = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        SDL_SetWindowFullscreen(w, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_ShowCursor(fullscreen ? SDL_ENABLE : SDL_DISABLE);
    }

    int width, height;
    SDL_GL_GetDrawableSize(w, &width, &height);
    if (width == g_width && height == g_height) {
        return;
    }
    g_width = width;
    g_height = height;
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Window: " + std::to_string(width) + "x" + std::to_string(height));
    // the whole ui in the window, the borders as the background
    float scale = std::min((float) width / g_ui.x, (float) height / g_ui.y);
    main->setScale(scale, scale);
    main->setPosition(std::round(((float) width - g_ui.x * scale) / 2),
                      std::round(((float) height - g_ui.y * scale) / 2));
}

#else

void pplay::DesktopWindow::setup(Main *) {}

void pplay::DesktopWindow::update(Main *) {}

#endif
