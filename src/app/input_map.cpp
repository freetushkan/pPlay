//
// Input mapping (see input_map.h).
//

#include <algorithm>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <vector>
#ifdef __PS4__
#include <cstddef>
#include <cstdint>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#endif

#include "input_map.h"
#include "utility.h"

using c2d::Input;

namespace {

    struct KeyMap {
        int code;
        unsigned int button;
    };

#if defined(__LINUX__)
    const std::vector<Input::ButtonMapping> g_keyboard = {
            {Input::Button::Up, SDL_SCANCODE_UP},       {Input::Button::Down, SDL_SCANCODE_DOWN},
            {Input::Button::Left, SDL_SCANCODE_LEFT},   {Input::Button::Right, SDL_SCANCODE_RIGHT},
            {Input::Button::A, SDL_SCANCODE_RETURN},    {Input::Button::A, SDL_SCANCODE_KP_ENTER},
            {Input::Button::A, SDL_SCANCODE_SPACE},     {Input::Button::B, SDL_SCANCODE_ESCAPE},
            {Input::Button::B, SDL_SCANCODE_BACKSPACE}, {Input::Button::X, SDL_SCANCODE_TAB},
            {Input::Button::X, SDL_SCANCODE_MENU},      {Input::Button::X, SDL_SCANCODE_APPLICATION},
            {Input::Button::Y, SDL_SCANCODE_DELETE},    {Input::Button::LB, SDL_SCANCODE_PAGEUP},
            {Input::Button::RB, SDL_SCANCODE_PAGEDOWN}, {Input::Button::LT, SDL_SCANCODE_HOME},
            {Input::Button::RT, SDL_SCANCODE_END},      {Input::Button::Start, SDL_SCANCODE_F1},
            {Input::Button::Select, SDL_SCANCODE_F2},
    };
#elif !defined(NO_KEYBOARD)
    const std::vector<Input::ButtonMapping> g_keyboard = {
            {Input::Button::Up, SDL_SCANCODE_UP},
            {Input::Button::Up, SDL_SCANCODE_KP_8},
            {Input::Button::Down, SDL_SCANCODE_DOWN},
            {Input::Button::Down, SDL_SCANCODE_KP_2},
            {Input::Button::Left, SDL_SCANCODE_LEFT},
            {Input::Button::Left, SDL_SCANCODE_KP_4},
            {Input::Button::Right, SDL_SCANCODE_RIGHT},
            {Input::Button::Right, SDL_SCANCODE_KP_6},
            {Input::Button::A, SDL_SCANCODE_RETURN},
            {Input::Button::A, SDL_SCANCODE_KP_ENTER},
            {Input::Button::A, SDL_SCANCODE_SELECT}, // remote "OK" (some keymaps)
            {Input::Button::B, SDL_SCANCODE_ESCAPE},
            {Input::Button::B, SDL_SCANCODE_BACKSPACE},
            {Input::Button::B, SDL_SCANCODE_AC_BACK}, // remote "Back"
            {Input::Button::X, SDL_SCANCODE_X},
            {Input::Button::X, SDL_SCANCODE_MENU}, // remote "Menu" / "Options"
            {Input::Button::X, SDL_SCANCODE_APPLICATION},
            {Input::Button::Y, SDL_SCANCODE_Y},
            {Input::Button::Y, SDL_SCANCODE_DELETE},
            {Input::Button::LB, SDL_SCANCODE_PAGEUP},
            {Input::Button::RB, SDL_SCANCODE_PAGEDOWN},
            {Input::Button::LT, SDL_SCANCODE_LEFTBRACKET},
            {Input::Button::RT, SDL_SCANCODE_RIGHTBRACKET},
            {Input::Button::Start, SDL_SCANCODE_F1},
            {Input::Button::Select, SDL_SCANCODE_F2},
    };
#endif

    // media and remote keys, handled here (SDL keycodes)
    const std::vector<KeyMap> g_keycodes = {
#ifndef __LINUX__
            {SDLK_SPACE, pplay::InputMap::PlayPause},
#endif
            {SDLK_AUDIOPLAY, pplay::InputMap::PlayPause},
            {SDLK_AUDIOSTOP, pplay::InputMap::Stop},
            {SDLK_AUDIONEXT, pplay::InputMap::Next},
            {SDLK_AUDIOPREV, pplay::InputMap::Previous},
            {SDLK_AUDIOFASTFORWARD, pplay::InputMap::FastForward},
            {SDLK_AUDIOREWIND, pplay::InputMap::Rewind},
            {SDLK_VOLUMEUP, pplay::InputMap::VolumeUp},
            {SDLK_VOLUMEDOWN, pplay::InputMap::VolumeDown},
            {SDLK_AC_HOME, Input::Button::LB}, // remote "Home": main menu
    };

    std::mutex g_mutex;
    unsigned int g_pending = 0;
    bool g_textInput = false;
    std::atomic<bool> g_fullscreenToggle{false};
    c2d::Input *g_input = nullptr;

    bool isKeyboardMapped(SDL_Scancode scancode) {
#ifndef NO_KEYBOARD
        for (const auto &map: g_keyboard) {
            if (map.value == (int) scancode) {
                return true;
            }
        }
#endif
        return false;
    }

    bool isJoystickMapped(int button) {
        if (g_input == nullptr) {
            return false;
        }
        for (const auto &map: g_input->getPlayer(0)->mapping) {
            if (map.value == button) {
                return true;
            }
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
            SDL_GameControllerButtonBind bind = SDL_GameControllerGetBindForButton(pad, (SDL_GameControllerButton) b);
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
        if (hat & SDL_HAT_UP) {
            buttons |= Input::Button::Up;
        }
        if (hat & SDL_HAT_DOWN) {
            buttons |= Input::Button::Down;
        }
        if (hat & SDL_HAT_LEFT) {
            buttons |= Input::Button::Left;
        }
        if (hat & SDL_HAT_RIGHT) {
            buttons |= Input::Button::Right;
        }
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
            if (d.pad) {
                SDL_GameControllerClose(d.pad);
            } else if (d.joy) {
                SDL_JoystickClose(d.joy);
            }
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
            if (!d.joy) {
                continue;
            }
            const char *name = SDL_JoystickName(d.joy);
            d.name = name ? name : "?";
            d.buttons.assign((size_t) std::max(0, SDL_JoystickNumButtons(d.joy)), 0);
            d.hats.assign((size_t) std::max(0, SDL_JoystickNumHats(d.joy)), SDL_HAT_CENTERED);
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "Input: device " + std::to_string(i) + " \"" + d.name + "\" ("
                                        + (d.pad ? "game controller" : "joystick") + ", "
                                        + std::to_string(d.buttons.size()) + " buttons, "
                                        + std::to_string(d.hats.size()) + " hats)" + (i == 0 ? ", main" : ""));
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
                    snprintf(buf, sizeof(buf), "hat %d = 0x%x on \"%s\" (handled as directions)", (int) h,
                             (unsigned) value, d.name.c_str());
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

#ifdef __PS4__
    // HDMI-CEC remotes are exposed by Orbis through a separate native pad port.
    // They are not SDL joysticks, so the regular SDL polling path cannot see them.
    constexpr int Ps4RemotePortType = 16;
    constexpr uint32_t Ps4RemoteIntercepted = 0x80000000u;
    constexpr int Ps4RemoteKeyCodeOffset = 3;
    static_assert(offsetof(OrbisPadData, unknown) + Ps4RemoteKeyCodeOffset == 108,
                  "OpenOrbis PS4 remote key-code offset changed");

    int g_ps4RemoteHandle = -1;
    bool g_ps4RemoteHandleOwned = false;
    bool g_ps4RemoteApiInitialized = false;
    bool g_ps4RemoteRetryArmed = false;
    bool g_ps4RemoteConnected = false;
    bool g_ps4RemoteHaveSample = false;
    uint32_t g_ps4RemoteLastRawButtons = 0;
    uint8_t g_ps4RemoteLastKeyCode = 0;
    uint32_t g_ps4RemoteLastOpenError = 0;
    uint32_t g_ps4RemoteLastReadError = 0;
    c2d::C2DClock *g_ps4RemoteRetryClock = nullptr;

    void retryPs4Remote() {
        if (g_ps4RemoteRetryClock == nullptr) {
            g_ps4RemoteRetryClock = new c2d::C2DClock();
        }
        g_ps4RemoteRetryClock->restart();
        g_ps4RemoteRetryArmed = true;
        g_ps4RemoteConnected = false;
        g_ps4RemoteHaveSample = false;
    }

    bool openPs4Remote() {
        if (g_ps4RemoteHandle >= 0) {
            return true;
        }
        if (g_ps4RemoteRetryArmed && g_ps4RemoteRetryClock != nullptr
            && g_ps4RemoteRetryClock->getElapsedTime().asMilliseconds() < 1000) {
            return false;
        }
        g_ps4RemoteRetryArmed = false;

        if (!g_ps4RemoteApiInitialized) {
            OrbisUserServiceInitializeParams userParams {};
            userParams.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
            int userResult = sceUserServiceInitialize(&userParams);
            const bool userReady = userResult == 0
                                   || (uint32_t) userResult == ORBIS_USER_SERVICE_ERROR_ALREADY_INITIALIZED;
            if (!userReady) {
                char message[128];
                snprintf(message, sizeof(message),
                         "Input: PS4 TV remote user-service init failed: 0x%08x",
                         (unsigned) userResult);
                pplay::Utility::log(pplay::Utility::LogLevel::Info, message);
            }

            int padResult = scePadInit();
            const bool padReady = padResult == 0;
            if (!padReady) {
                char message[128];
                snprintf(message, sizeof(message), "Input: PS4 TV remote pad init failed: 0x%08x",
                         (unsigned) padResult);
                pplay::Utility::log(pplay::Utility::LogLevel::Info, message);
            }
            g_ps4RemoteApiInitialized = userReady && padReady;
            if (!g_ps4RemoteApiInitialized) {
                retryPs4Remote();
                return false;
            }
        }

        constexpr int systemUser = ORBIS_USER_SERVICE_USER_ID_SYSTEM;
        int handle = scePadOpen(systemUser, Ps4RemotePortType, 0, nullptr);
        if ((uint32_t) handle == ORBIS_PAD_ERROR_ALREADY_OPENED) {
            // Another native reader already owns this port. Borrow, do not close it.
            handle = scePadGetHandle(systemUser, Ps4RemotePortType, 0);
            g_ps4RemoteHandleOwned = false;
        } else {
            g_ps4RemoteHandleOwned = handle >= 0;
        }

        if (handle < 0) {
            if (g_ps4RemoteLastOpenError != (uint32_t) handle) {
                char message[160];
                snprintf(message, sizeof(message),
                         "Input: PS4 TV remote open failed (system user, port 16): 0x%08x",
                         (unsigned) handle);
                pplay::Utility::log(pplay::Utility::LogLevel::Info, message);
                g_ps4RemoteLastOpenError = (uint32_t) handle;
            }
            retryPs4Remote();
            return false;
        }

        g_ps4RemoteHandle = handle;
        g_ps4RemoteLastOpenError = 0;
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
                            std::string("Input: PS4 TV remote opened (system user, port 16, ")
                                    + (g_ps4RemoteHandleOwned ? "owned)" : "shared)"));
        return true;
    }

    unsigned int pollPs4Remote() {
        using B = Input::Button;
        if (!openPs4Remote()) {
            return 0;
        }

        OrbisPadData sample {};
        int result = scePadReadState(g_ps4RemoteHandle, &sample);
        if (result != 0) {
            if (g_ps4RemoteLastReadError != (uint32_t) result) {
                char message[128];
                snprintf(message, sizeof(message), "Input: PS4 TV remote read failed: 0x%08x",
                         (unsigned) result);
                pplay::Utility::log(pplay::Utility::LogLevel::Info, message);
                g_ps4RemoteLastReadError = (uint32_t) result;
            }
            if (g_ps4RemoteHandleOwned) {
                scePadClose(g_ps4RemoteHandle);
            }
            g_ps4RemoteHandle = -1;
            g_ps4RemoteHandleOwned = false;
            retryPs4Remote();
            return 0;
        }
        g_ps4RemoteLastReadError = 0;

        if (!sample.connected) {
            if (g_ps4RemoteConnected) {
                pplay::Utility::log(pplay::Utility::LogLevel::Info, "Input: PS4 TV remote disconnected");
            }
            g_ps4RemoteConnected = false;
            g_ps4RemoteHaveSample = false;
            return 0;
        }

        const uint32_t raw = sample.buttons;
        const uint8_t keyCode = sample.unknown[Ps4RemoteKeyCodeOffset];
        if (!g_ps4RemoteHaveSample || raw != g_ps4RemoteLastRawButtons || keyCode != g_ps4RemoteLastKeyCode) {
            if (raw != 0 || keyCode != 0) {
                char message[160];
                snprintf(message, sizeof(message),
                         "Input: PS4 TV remote sample buttons=0x%08x keyCode=%u",
                         (unsigned) raw, (unsigned) keyCode);
                pplay::Utility::log(pplay::Utility::LogLevel::Debug, message);
            }
            g_ps4RemoteLastRawButtons = raw;
            g_ps4RemoteLastKeyCode = keyCode;
            g_ps4RemoteHaveSample = true;
        }
        if (!g_ps4RemoteConnected) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "Input: PS4 TV remote connected");
        }
        g_ps4RemoteConnected = true;

        unsigned int held = 0;
        if (raw & ORBIS_PAD_BUTTON_UP) held |= B::Up;
        if (raw & ORBIS_PAD_BUTTON_DOWN) held |= B::Down;
        if (raw & ORBIS_PAD_BUTTON_LEFT) held |= B::Left;
        if (raw & ORBIS_PAD_BUTTON_RIGHT) held |= B::Right;
        if (raw & ORBIS_PAD_BUTTON_CROSS) held |= B::A;
        if (raw & ORBIS_PAD_BUTTON_CIRCLE) held |= B::B;
        if (raw & ORBIS_PAD_BUTTON_OPTIONS) held |= B::X;

        // The system-intercepted flag guards the additional remote key-code
        // translation only. Keep ordinary D-pad/button bits intact.
        if ((raw & Ps4RemoteIntercepted) == 0) {
            // Some remote keys arrive in byte 108 rather than OrbisPadData::buttons.
            // 13/15/18 follow Sony's PS5 remote table (Enter/Back/Menu); PS4 firmware
            // may expose different values, so the raw sample above remains diagnostic.
            switch (keyCode) {
                case 13: held |= B::A; break; // Enter / OK
                case 15: held |= B::B; break; // Back
                case 18: held |= B::X; break; // Menu / Options
                default:
                    if (keyCode != 0) {
                        char message[160];
                        snprintf(message, sizeof(message),
                                 "Input: unknown PS4 TV remote keyCode=%u buttons=0x%08x",
                                 (unsigned) keyCode, (unsigned) raw);
                        pplay::Utility::log(pplay::Utility::LogLevel::Debug, message);
                    }
                    break;
            }
        }
        return held;
    }
#endif

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
    bool g_extraRepeat = false;
    bool g_repeat = false;

    unsigned int extraFire(unsigned int held) {
        if (g_extraClock == nullptr) {
            g_extraClock = new c2d::C2DClock();
            g_holdClock = new c2d::C2DClock();
            g_releaseClock = new c2d::C2DClock();
        }
        unsigned int fire = 0;
        unsigned int pressed = held & ~g_extraHeld;
        bool bounce = pressed && g_extraHeld == 0 && g_releaseClock->getElapsedTime().asMilliseconds() < DEBOUNCE_MS;
        g_extraRepeat = false;
        if (pressed && !bounce) {
            fire = held;
            g_extraClock->restart();
            g_holdClock->restart();
        } else if (held) {
            int heldMs = g_holdClock->getElapsedTime().asMilliseconds();
            int sinceMs = g_extraClock->getElapsedTime().asMilliseconds();
            if (sinceMs >= pplay::InputMap::repeatDelay(heldMs)) {
                fire = held;
                g_extraRepeat = true;
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
        if (g_textInput) {
            return 1;
        }
        if (event->type == SDL_KEYDOWN) {
            const SDL_Keycode sym = event->key.keysym.sym;
            const SDL_Scancode scancode = event->key.keysym.scancode;
#ifdef __LINUX__
            if (sym == SDLK_F11) {
                if (!event->key.repeat) {
                    g_fullscreenToggle = true;
                }
                return 1;
            }
#endif
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
#if defined(__PS4__) || defined(__PS5__)
            // Options and the touchpad: Start on both consoles (the ports map them differently)
            auto &mapping = input->getPlayer(i)->mapping;
            mapping.push_back({Input::Button::Start, SDL_CONTROLLER_BUTTON_BACK});
#if SDL_VERSION_ATLEAST(2, 0, 14)
            mapping.push_back({Input::Button::Start, SDL_CONTROLLER_BUTTON_TOUCHPAD});
#endif
#endif
        }
        SDL_AddEventWatch(onEvent, nullptr);
        scanDevices();
#ifdef __PS4__
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
                            "Input: probing native PS4 HDMI-CEC remote on system pad port 16");
#endif
    }

    unsigned int update() {
        unsigned int held = pollDevices();
#ifdef __PS4__
        const unsigned int remoteHeld = pollPs4Remote();
#endif
        // buttons of the other game controllers, as mapped for player 1 (no sticks and
        // triggers: remotes may report them off-center)
        auto mapped = [](SDL_GameController *pad) {
            unsigned int buttons = 0;
            if (pad == nullptr || g_input == nullptr) {
                return buttons;
            }
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
            if (g_mirrorClock == nullptr) {
                g_mirrorClock = new c2d::C2DClock();
            }
            const int now = (int) g_mirrorClock->getElapsedTime().asMilliseconds();
            const unsigned int mainHeld = mapped(mainPad);
            for (int bit = 0; bit < 32; bit++) {
                if (mainHeld & (1u << bit)) {
                    g_mainLastMs[bit] = now;
                }
                if ((held & (1u << bit)) && g_mainLastMs[bit] >= 0 && now - g_mainLastMs[bit] < MIRROR_MS) {
                    held &= ~(1u << bit);
                }
            }
        }
#ifdef __PS4__
        // Keep the remote independent from the DS4 mirror suppression above.
        held |= remoteHeld;
#endif
        unsigned int fire = extraFire(held);
        if (fire) {
            pplay::Utility::log(pplay::Utility::LogLevel::Debug,
                                "Input: other devices " + pplay::Utility::getKeysString(fire));
        }
        return fire;
    }

    int repeatDelay(int heldMs) {
        if (heldMs < REPEAT_DELAY) {
            return REPEAT_DELAY;
        }
        if (heldMs < REPEAT_LEVEL2_AFTER) {
            return REPEAT_LEVEL1;
        }
        if (heldMs < REPEAT_LEVEL3_AFTER) {
            return REPEAT_LEVEL2;
        }
        return REPEAT_LEVEL3;
    }

    void setRepeat(bool value) {
        g_repeat = value;
    }

    bool isRepeat() {
        return g_repeat;
    }

    bool updateRepeated() {
        return g_extraRepeat;
    }

    unsigned int takePending() {
        std::lock_guard<std::mutex> lock(g_mutex);
        unsigned int pending = g_pending;
        g_pending = 0;
        return pending;
    }

    bool takeFullscreenToggle() {
        return g_fullscreenToggle.exchange(false);
    }

    std::string buttonIcon(unsigned int buttons) {
        using B = Input::Button;
        static const std::pair<unsigned int, const char *> icons[] = {
#if defined(__LINUX__)
                {B::Up | B::Down, "kb_updown"},
                {B::Up, "kb_up"},
                {B::Down, "kb_down"},
                {B::Left, "kb_left"},
                {B::Right, "kb_right"},
                {B::A, "kb_enter"},
                {B::B, "kb_esc"},
                {B::X, "kb_tab"},
                {B::Y, "kb_del"},
                {B::LB, "kb_pgup"},
                {B::RB, "kb_pgdn"},
                {B::LT, "kb_home"},
                {B::RT, "kb_end"},
                {B::Start, "kb_f1"},
                {B::Select, "kb_f2"},
#else
                {B::Up | B::Down, "dpad_updown"},
                {B::Left | B::Right, "dpad_leftright"},
                {B::Up, "dpad_up"},
                {B::Down, "dpad_down"},
                {B::Left, "dpad_left"},
                {B::Right, "dpad_right"},
#if defined(__PS4__) || defined(__PS5__)
                {B::A, "ps_cross"},
                {B::B, "ps_circle"},
                {B::X, "ps_square"},
                {B::Y, "ps_triangle"},
                {B::LB, "ps_l1"},
                {B::RB, "ps_r1"},
                {B::LT, "ps_l2"},
                {B::RT, "ps_r2"},
                {B::Start, "ps_options"},
#else
                {B::A, "sw_a"},
                {B::B, "sw_b"},
                {B::X, "sw_x"},
                {B::Y, "sw_y"},
                {B::LB, "sw_l"},
                {B::RB, "sw_r"},
                {B::LT, "sw_zl"},
                {B::RT, "sw_zr"},
                {B::Start, "sw_plus"},
                {B::Select, "sw_minus"},
#endif
#endif
        };
        for (const auto &icon: icons) {
            if (icon.first == buttons) {
                return std::string("key_") + icon.second + ".png";
            }
        }
        return "";
    }

    std::string buttonName(unsigned int button) {
        using B = Input::Button;
        // first match: the platform names, then the generic ones
        static const std::pair<unsigned int, const char *> names[] = {
                {B::Up, "\u2191"},     {B::Down, "\u2193"},   {B::Left, "\u2190"}, {B::Right, "\u2192"},
#if defined(__PS4__) || defined(__PS5__)
                {B::A, "\u2715"},      {B::B, "\u25CB"},      {B::X, "\u25A1"},    {B::Y, "\u25B3"},
                {B::LB, "L1"},         {B::RB, "R1"},         {B::LT, "L2"},       {B::RT, "R2"},
                {B::Start, "Options"},
#elif defined(__SWITCH__)
                {B::LT, "ZL"},       {B::RT, "ZR"},         {B::Start, "+"},     {B::Select, "-"},
#elif defined(__LINUX__)
                // the keyboard (see g_keyboard)
                {B::A, "Enter"}, {B::B, "Esc"}, {B::X, "Tab"}, {B::Y, "Del"}, {B::LB, "PgUp"}, {B::RB, "PgDn"},
                {B::LT, "Home"}, {B::RT, "End"}, {B::Start, "F1"}, {B::Select, "F2"},
#endif
                {B::A, "A"},           {B::B, "B"},           {B::X, "X"},         {B::Y, "Y"},
                {B::LB, "L"},          {B::RB, "R"},          {B::LT, "LT"},       {B::RT, "RT"},
                {B::Start, "Start"},   {B::Select, "Select"},
        };
        for (const auto &name: names) {
            if (name.first == button) {
                return name.second;
            }
        }
        return "?";
    }

    void setTextInput(bool active) {
        g_textInput = active;
    }

    std::string toString(unsigned int buttons) {
        static const std::pair<unsigned int, const char *> names[] = {
                {PlayPause, "PlayPause"},     {Stop, "Stop"},     {Next, "Next"},         {Previous, "Previous"},
                {FastForward, "FastForward"}, {Rewind, "Rewind"}, {VolumeUp, "VolumeUp"}, {VolumeDown, "VolumeDown"}};
        std::string out;
        for (const auto &name: names) {
            if (buttons & name.first) {
                out += std::string(name.second) + " ";
            }
        }
        return out;
    }
}
