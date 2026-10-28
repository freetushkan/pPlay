//
// Input mapping (see input_map.h).
//

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

#include "input_map.h"
#include "utility.h"

using c2d::Input;

namespace {

    struct KeyMap {
        int code;
        unsigned int button;
    };

#ifndef NO_KEYBOARD
    // keyboard, read by libcross2d (SDL scancodes)
    const std::vector<Input::ButtonMapping> g_keyboard = {
            {Input::Button::Up,     SDL_SCANCODE_UP},
            {Input::Button::Up,     SDL_SCANCODE_KP_8},
            {Input::Button::Down,   SDL_SCANCODE_DOWN},
            {Input::Button::Down,   SDL_SCANCODE_KP_2},
            {Input::Button::Left,   SDL_SCANCODE_LEFT},
            {Input::Button::Left,   SDL_SCANCODE_KP_4},
            {Input::Button::Right,  SDL_SCANCODE_RIGHT},
            {Input::Button::Right,  SDL_SCANCODE_KP_6},
            {Input::Button::A,      SDL_SCANCODE_RETURN},
            {Input::Button::A,      SDL_SCANCODE_KP_ENTER},
            {Input::Button::A,      SDL_SCANCODE_SELECT},       // remote "OK" (some keymaps)
            {Input::Button::B,      SDL_SCANCODE_ESCAPE},
            {Input::Button::B,      SDL_SCANCODE_BACKSPACE},
            {Input::Button::B,      SDL_SCANCODE_AC_BACK},      // remote "Back"
            {Input::Button::X,      SDL_SCANCODE_X},
            {Input::Button::X,      SDL_SCANCODE_MENU},         // remote "Menu" / "Options"
            {Input::Button::X,      SDL_SCANCODE_APPLICATION},
            {Input::Button::Y,      SDL_SCANCODE_Y},
            {Input::Button::Y,      SDL_SCANCODE_DELETE},
            {Input::Button::LB,     SDL_SCANCODE_PAGEUP},
            {Input::Button::RB,     SDL_SCANCODE_PAGEDOWN},
            {Input::Button::LT,     SDL_SCANCODE_LEFTBRACKET},
            {Input::Button::RT,     SDL_SCANCODE_RIGHTBRACKET},
            {Input::Button::Start,  SDL_SCANCODE_F1},
            {Input::Button::Select, SDL_SCANCODE_F2},
    };
#endif

    // media and remote keys, handled here (SDL keycodes)
    const std::vector<KeyMap> g_keycodes = {
            {SDLK_SPACE,            pplay::InputMap::PlayPause},
            {SDLK_AUDIOPLAY,        pplay::InputMap::PlayPause},
            {SDLK_AUDIOSTOP,        pplay::InputMap::Stop},
            {SDLK_AUDIONEXT,        pplay::InputMap::Next},
            {SDLK_AUDIOPREV,        pplay::InputMap::Previous},
            {SDLK_AUDIOFASTFORWARD, pplay::InputMap::FastForward},
            {SDLK_AUDIOREWIND,      pplay::InputMap::Rewind},
            {SDLK_VOLUMEUP,         pplay::InputMap::VolumeUp},
            {SDLK_VOLUMEDOWN,       pplay::InputMap::VolumeDown},
            {SDLK_AC_HOME,          Input::Button::LB},           // remote "Home": main menu
    };

    std::mutex g_mutex;
    unsigned int g_pending = 0;
    c2d::Input *g_input = nullptr;

    bool isKeyboardMapped(SDL_Scancode scancode) {
#ifndef NO_KEYBOARD
        for (const auto &map: g_keyboard) {
            if (map.value == (int) scancode) return true;
        }
#endif
        return false;
    }

    bool isJoystickMapped(int button) {
        if (g_input == nullptr) return false;
        for (const auto &map: g_input->getPlayer(0)->mapping) {
            if (map.value == button) return true;
        }
        return false;
    }

    void logUnknown(const std::string &message) {
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Input: unknown " + message);
    }

    void addPending(unsigned int buttons) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pending |= buttons;
    }

    // joystick button used by the game controller mapping?
    bool isBound(SDL_GameController *pad, int joyButton) {
        if (pad == nullptr) {
            return false;
        }
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            SDL_GameControllerButtonBind bind =
                    SDL_GameControllerGetBindForButton(pad, (SDL_GameControllerButton) b);
            if (bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && bind.value.button == joyButton) {
                return true;
            }
        }
        return false;
    }

    bool hasDpad(SDL_GameController *pad) {
        if (pad == nullptr) {
            return false;
        }
        SDL_GameControllerButtonBind bind = SDL_GameControllerGetBindForButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
        return bind.bindType != SDL_CONTROLLER_BINDTYPE_NONE;
    }

    unsigned int hatButtons(Uint8 hat) {
        unsigned int buttons = 0;
        if (hat & SDL_HAT_UP) buttons |= Input::Button::Up;
        if (hat & SDL_HAT_DOWN) buttons |= Input::Button::Down;
        if (hat & SDL_HAT_LEFT) buttons |= Input::Button::Left;
        if (hat & SDL_HAT_RIGHT) buttons |= Input::Button::Right;
        return buttons;
    }

    // Joysticks are polled (the PS5 SDL port sends no joystick events): unmapped
    // buttons are logged, hats of devices without d-pad mapping are directions.
    struct Device {
        SDL_Joystick *joy = nullptr;
        SDL_GameController *pad = nullptr;
        std::string name;
        std::vector<Uint8> buttons;
        std::vector<Uint8> hats;
    };
    std::vector<Device> g_devices;
    int g_deviceCount = -1;

    void scanDevices() {
        for (auto &d: g_devices) {
            if (d.pad) SDL_GameControllerClose(d.pad);
            else if (d.joy) SDL_JoystickClose(d.joy);
        }
        g_devices.clear();
        g_deviceCount = SDL_NumJoysticks();
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Input: " + std::to_string(g_deviceCount) + " device(s)");
        for (int i = 0; i < g_deviceCount; i++) {
            Device d;
            if (SDL_IsGameController(i)) {
                d.pad = SDL_GameControllerOpen(i);
                d.joy = d.pad ? SDL_GameControllerGetJoystick(d.pad) : nullptr;
            } else {
                d.joy = SDL_JoystickOpen(i);
            }
            if (!d.joy) continue;
            const char *name = SDL_JoystickName(d.joy);
            d.name = name ? name : "?";
            d.buttons.assign((size_t) std::max(0, SDL_JoystickNumButtons(d.joy)), 0);
            d.hats.assign((size_t) std::max(0, SDL_JoystickNumHats(d.joy)), SDL_HAT_CENTERED);
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "Input: device " + std::to_string(i) + " \"" + d.name + "\" ("
                                + (d.pad ? "game controller" : "joystick") + ", "
                                + std::to_string(d.buttons.size()) + " buttons, "
                                + std::to_string(d.hats.size()) + " hats)"
                                + (i == 0 ? ", main" : ""));
            g_devices.push_back(d);
        }
    }

    unsigned int pollDevices() {
        if (SDL_NumJoysticks() != g_deviceCount) {
            scanDevices();
        }
        unsigned int held = 0;
        char buf[256];
        for (auto &d: g_devices) {
            for (size_t b = 0; b < d.buttons.size(); b++) {
                Uint8 state = SDL_JoystickGetButton(d.joy, (int) b);
                if (state && !d.buttons[b] && !isBound(d.pad, (int) b)) {
                    snprintf(buf, sizeof(buf), "joystick button %d on \"%s\"%s", (int) b, d.name.c_str(),
                             d.pad ? " (not in its game controller mapping)" : "");
                    logUnknown(buf);
                }
                d.buttons[b] = state;
            }
            for (size_t h = 0; h < d.hats.size(); h++) {
                Uint8 value = SDL_JoystickGetHat(d.joy, (int) h);
                bool unused = !hasDpad(d.pad);
                if (value != d.hats[h] && value != SDL_HAT_CENTERED && unused) {
                    snprintf(buf, sizeof(buf), "hat %d = 0x%x on \"%s\" (handled as directions)",
                             (int) h, (unsigned) value, d.name.c_str());
                    pplay::Utility::log(pplay::Utility::LogLevel::Debug, std::string("Input: ") + buf);
                }
                d.hats[h] = value;
                if (unused) {
                    held |= hatButtons(value);
                }
            }
        }
        return held;
    }

    // other devices auto repeat, with a debounce (CEC remotes may report a held key
    // as quick release/press)
#define DEBOUNCE_MS 120
#define STICK_DEADZONE 16000
#define MIRROR_MS 400
    c2d::C2DClock *g_mirrorClock = nullptr;
    int g_mainLastMs[32] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    unsigned int g_extraHeld = 0;
    c2d::C2DClock *g_extraClock = nullptr;
    c2d::C2DClock *g_holdClock = nullptr;
    c2d::C2DClock *g_releaseClock = nullptr;

    unsigned int extraFire(unsigned int held) {
        if (g_extraClock == nullptr) {
            g_extraClock = new c2d::C2DClock();
            g_holdClock = new c2d::C2DClock();
            g_releaseClock = new c2d::C2DClock();
        }
        unsigned int fire = 0;
        unsigned int pressed = held & ~g_extraHeld;
        bool bounce = pressed && g_extraHeld == 0
                      && g_releaseClock->getElapsedTime().asMilliseconds() < DEBOUNCE_MS;
        if (pressed && !bounce) {
            fire = held;
            g_extraClock->restart();
            g_holdClock->restart();
        } else if (held) {
            int heldMs = g_holdClock->getElapsedTime().asMilliseconds();
            int sinceMs = g_extraClock->getElapsedTime().asMilliseconds();
            if (sinceMs >= pplay::InputMap::repeatDelay(heldMs)) {
                fire = held;
                g_extraClock->restart();
            }
        }
        if (held == 0 && g_extraHeld != 0) {
            g_releaseClock->restart();
        }
        g_extraHeld = held;
        return fire;
    }

    int onEvent(void *, SDL_Event *event) {
        char buf[256];
        if (event->type == SDL_KEYDOWN) {
            const SDL_Keycode sym = event->key.keysym.sym;
            const SDL_Scancode scancode = event->key.keysym.scancode;
            for (const auto &map: g_keycodes) {
                if (map.code == sym) {
                    if (!event->key.repeat || (map.button & pplay::InputMap::Media) == 0) {
                        addPending(map.button);
                    }
                    return 1;
                }
            }
            if (!event->key.repeat && !isKeyboardMapped(scancode)) {
                snprintf(buf, sizeof(buf),
                         "key \"%s\": scancode {Input::Button::?, (SDL_Scancode) %d}, "
                         "keycode {(SDL_Keycode) 0x%x, Button::?}",
                         SDL_GetKeyName(sym), (int) scancode, (unsigned int) sym);
                logUnknown(buf);
            }
        } else if (event->type == SDL_CONTROLLERBUTTONDOWN && event->cbutton.which >= 0) {
            if (!isJoystickMapped(event->cbutton.button)) {
                SDL_GameController *pad = SDL_GameControllerFromInstanceID(event->cbutton.which);
                snprintf(buf, sizeof(buf),
                         "controller button \"%s\" on \"%s\": {Input::Button::?, (SDL_GameControllerButton) %d}",
                         SDL_GameControllerGetStringForButton((SDL_GameControllerButton) event->cbutton.button),
                         pad ? SDL_GameControllerName(pad) : "?", (int) event->cbutton.button);
                logUnknown(buf);
            }
        }
        return 1;
    }
}

namespace pplay::InputMap {

    void setup(c2d::Input *input) {
        g_input = input;
#ifndef NO_KEYBOARD
        input->setKeyboardMapping(g_keyboard);
#endif
        for (int i = 0; i < PLAYER_MAX; i++) {
            input->getPlayer(i)->dz = STICK_DEADZONE;
        }
        SDL_AddEventWatch(onEvent, nullptr);
        scanDevices();
    }

    unsigned int update() {
        unsigned int held = pollDevices();
        // buttons of the other game controllers, as mapped for player 1 (no sticks and
        // triggers: remotes may report them off-center)
        auto mapped = [](SDL_GameController *pad) {
            unsigned int buttons = 0;
            if (pad == nullptr || g_input == nullptr) return buttons;
            for (const auto &map: g_input->getPlayer(0)->mapping) {
                if (map.value >= 0 && map.value < SDL_CONTROLLER_BUTTON_MAX
                    && SDL_GameControllerGetButton(pad, (SDL_GameControllerButton) map.value)) {
                    buttons |= map.button;
                }
            }
            return buttons;
        };
        auto *mainPad = g_input != nullptr ? (SDL_GameController *) g_input->getPlayer(0)->data : nullptr;
        SDL_Joystick *mainJoy = mainPad ? SDL_GameControllerGetJoystick(mainPad) : nullptr;
        for (auto &d: g_devices) {
            if (d.joy != mainJoy) {
                held |= mapped(d.pad);
            }
        }
        // some devices mirror the main controller, late (PS5 "User1" hat): ignore what
        // it pressed in the last MIRROR_MS
        if (mainPad != nullptr) {
            if (g_mirrorClock == nullptr) g_mirrorClock = new c2d::C2DClock();
            const int now = (int) g_mirrorClock->getElapsedTime().asMilliseconds();
            const unsigned int mainHeld = mapped(mainPad);
            for (int bit = 0; bit < 32; bit++) {
                if (mainHeld & (1u << bit)) g_mainLastMs[bit] = now;
                if ((held & (1u << bit)) && g_mainLastMs[bit] >= 0 && now - g_mainLastMs[bit] < MIRROR_MS) {
                    held &= ~(1u << bit);
                }
            }
        }
        unsigned int fire = extraFire(held);
        if (fire) {
            pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                                "Input: other devices " + pplay::Utility::getKeysString(fire));
        }
        return fire;
    }

    int repeatDelay(int heldMs) {
        if (heldMs < REPEAT_DELAY) return REPEAT_DELAY;
        if (heldMs < REPEAT_LEVEL2_AFTER) return REPEAT_LEVEL1;
        if (heldMs < REPEAT_LEVEL3_AFTER) return REPEAT_LEVEL2;
        return REPEAT_LEVEL3;
    }

    unsigned int takePending() {
        std::lock_guard<std::mutex> lock(g_mutex);
        unsigned int pending = g_pending;
        g_pending = 0;
        return pending;
    }

    std::string buttonName(unsigned int button) {
#if defined(__PS4__) || defined(__PS5__)
        switch (button) {
            case Input::Button::A: return "\u2715";      // cross
            case Input::Button::B: return "\u25CB";      // circle
            case Input::Button::X: return "\u25A1";      // square
            case Input::Button::Y: return "\u25B3";      // triangle
            default: break;
        }
#endif
        switch (button) {
            case Input::Button::A: return "A";
            case Input::Button::B: return "B";
            case Input::Button::X: return "X";
            case Input::Button::Y: return "Y";
            default: return "?";
        }
    }

    std::string toString(unsigned int buttons) {
        static const std::pair<unsigned int, const char *> names[] = {
                {PlayPause, "PlayPause"}, {Stop, "Stop"}, {Next, "Next"}, {Previous, "Previous"},
                {FastForward, "FastForward"}, {Rewind, "Rewind"}, {VolumeUp, "VolumeUp"},
                {VolumeDown, "VolumeDown"}
        };
        std::string out;
        for (const auto &name: names) {
            if (buttons & name.first) out += std::string(name.second) + " ";
        }
        return out;
    }
}
