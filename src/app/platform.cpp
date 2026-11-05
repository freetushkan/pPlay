//
// Platform specific startup and shutdown.
//

#include <cstdio>
#include <string>

#include "main.h"
#include "platform.h"
#include "desktop_window.h"
#include "utility.h"

#ifdef __SWITCH__

#include <switch.h>
#include "usbfs.h"

static AppletHookCookie applet_hook_cookie;
static bool paused_on_focus_loss = false;

static void on_applet_hook(AppletHookType hook, void *arg) {
    auto *main = (Main *) arg;

    switch (hook) {
        case AppletHookType_OnExitRequest:
            main->quit();
            break;
        case AppletHookType_OnFocusState:
            if (appletGetFocusState() == AppletFocusState_InFocus) {
                if (main->getPlayer()->getMpv()->isPaused() && paused_on_focus_loss) {
                    main->getPlayer()->resume();
                    paused_on_focus_loss = false;
                }
            } else {
                if (!main->getPlayer()->getMpv()->isPaused()) {
                    main->getPlayer()->pause();
                    paused_on_focus_loss = true;
                }
            }
            break;
        default:
            break;
    }
}

#elif __PS4__

#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/Net.h>
#include <orbis/Pad.h>
#include <cross2d/platforms/ps4/ps4_clock.h>
#include "ps4/ps4_compat.h"
#ifdef PPLAY_PS4_JAILBREAK
#include "ps4/orbis_jbc.h"
#endif

extern "C" int sceSystemServiceLoadExec(const char *path, const char *args[]);

c2d::Time c2d::PS4Clock::getCurrentTime() const {
    return c2d::microseconds((long) sceKernelGetProcessTime());
}

c2d::PS4Clock::PS4Clock() : Clock() {
    m_startTime = PS4Clock::getCurrentTime();
}

#elif __PS5__

#include <csignal>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <atomic>
#include <cstdint>
#include <ctime>
#include <thread>
#include "player.h"
#include "pplay_config.h"

extern "C" {
int sceSystemServiceLoadExec(const char *path, const char *args[]);
int sceSystemServiceReceiveEvent(void *event);
int sceSystemServiceEnableSuspendNotification();
int sceSystemServiceDeclareReadyForSuspend();
int32_t sceAgcSuspendPoint();
}

// Rest mode: the system suspends pPlay fine once, but suspending it again after it
// was resumed panics the kernel (A53MM: the app's mapper rings are not suspended).
// So by default pPlay closes as soon as it is resumed. "Keep in rest mode" asks for
// suspend notifications instead and gets ready for each suspend (unstable).
namespace {
    // the PS4 event types (ON_RESUME and the known others); anything else may be
    // the suspend notification, whose type is not documented
    const int32_t EVENT_ON_RESUME = 0x10000000;

    bool knownEvent(int32_t type) {
        return (type >= 0x10000001 && type <= 0x10000014) || type == 0x30000000;
    }

    // a frozen process: all its threads stop, this one ticking each second sees the
    // wall clock jump once resumed (a main thread blocked on the network does not)
    std::atomic<bool> g_frozen{false};

    void watchFreeze() {
        std::thread([] {
            time_t last = time(nullptr);
            while (true) {
                sleep(1);
                time_t now = time(nullptr);
                if (now - last > 10) {
                    g_frozen = true;
                }
                last = now;
            }
        }).detach();
    }

    bool g_notifications = false;
    bool g_quitting = false;
    // ready for a suspend: nothing drawn (no GPU work) until resumed
    bool g_renderPaused = false;
    time_t g_pausedAt = 0;

    std::string hexEvent(const uint8_t *data) {
        char text[3 * 16 + 1] = {};
        for (int i = 0; i < 16; i++) {
            snprintf(text + i * 3, 4, "%02x ", data[i]);
        }
        return text;
    }

    void readyForSuspend(Main *main, bool keep) {
        if (keep) {
            if (!main->getPlayer()->getMpv()->isStopped() && !main->getPlayer()->getMpv()->isPaused()) {
                main->getPlayer()->pause();
            }
            g_renderPaused = true;
            g_pausedAt = time(nullptr);
            int32_t point = sceAgcSuspendPoint();
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "PS5: ready for the suspend, drawing paused (suspend point: " + std::to_string(point) + ")");
        }
        int ready = sceSystemServiceDeclareReadyForSuspend();
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "PS5: declared ready for the suspend: " + std::to_string(ready));
    }

    void ps5RestMode(Main *main) {
        bool keep = main->getConfig()->getOption(OPT_PS5_KEEP_IN_REST)->getInteger() != 0;
        if (keep && !g_notifications) {
            g_notifications = true;
            int res = sceSystemServiceEnableSuspendNotification();
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "PS5: suspend notifications: " + std::to_string(res));
        }

        static bool watching = false;
        if (!watching) {
            watching = true;
            watchFreeze();
        }
        time_t now = time(nullptr);
        bool resumed = g_frozen.exchange(false);

        alignas(16) static uint8_t event[0x4000];
        for (int i = 0; i < 8; i++) {
            if (sceSystemServiceReceiveEvent(event) != 0) {
                break;
            }
            int32_t type = *(int32_t *) event;
            char name[16];
            snprintf(name, sizeof(name), "0x%08X", (unsigned) type);
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                std::string("PS5: system event ") + name + ": " + hexEvent(event));
            if (type == EVENT_ON_RESUME) {
                resumed = true;
            } else if (!knownEvent(type) && g_notifications) {
                readyForSuspend(main, keep);
            }
        }

        if (resumed) {
            g_renderPaused = false;
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                std::string("PS5: resumed by the system") + (keep ? ", kept running" : ", closing"));
            if (!keep && !g_quitting) {
                // suspending it again would panic the kernel
                g_quitting = true;
                main->quit();
            }
        } else if (g_renderPaused && now - g_pausedAt > 15) {
            // no suspend came (cancelled)
            g_renderPaused = false;
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "PS5: no suspend after 15 s, drawing again");
        }
        if (g_renderPaused) {
            usleep(16000);
        }
    }
}

// single instance (the port is released on exit/crash)
static bool ps5_single_instance() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return true;
    }
    struct sockaddr_in addr {};
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9095);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        close(sock);
        return false;
    }
    return true;
}

#endif

// renderer information
static void logRenderer(Main *main) {
    auto str = [](GLenum name) {
        const char *s = (const char *) glGetString(name);
        return std::string(s ? s : "?");
    };
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
                        "Renderer: " + std::to_string((int) main->getSize().x) + "x"
                                + std::to_string((int) main->getSize().y) + ", " + str(GL_RENDERER) + ", "
                                + str(GL_VERSION)
#ifdef __PS5__
                                + ", cpus: " + std::to_string(sysconf(_SC_NPROCESSORS_ONLN))
#endif
    );
}

namespace pplay::Platform {

    size_t threadStackSize() {
        // FFmpeg probing, embedded covers and external-track workers need a generous stack.
        return 4 * 1024 * 1024;
    }

    bool init() {
        const std::string stackSize = std::to_string(threadStackSize());
        SDL_SetHint(SDL_HINT_THREAD_STACK_SIZE, stackSize.c_str());
#ifdef __SWITCH__
#ifdef NDEBUG
        socketInitializeDefault();
#endif
        // mbedTLS entropy (ffmpeg TLS)
        csrngInitialize();
        appletMainLoop();
#elif __PS5__
        signal(SIGPIPE, SIG_IGN);
        if (!ps5_single_instance()) {
            printf("pPlay is already running\n");
            return false;
        }
#elif __PS4__
        setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef PPLAY_PS4_JAILBREAK
        if (!initialize_jbc()) {
            sceKernelDebugOutText(0, "[pPlay] jailbreak failed, continuing sandboxed\n");
        }
#endif
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE);
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
        sceSysmoduleLoadModuleInternal((enum OrbisSysModuleInternal) ORBIS_SYSMODULE_IME_DIALOG);
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD);
        ps4_compat_init();
        sceKernelDebugOutText(0, "[pPlay] started\n");
#endif
        return true;
    }

    std::string name() {
#ifdef __SWITCH__
        std::string out = "Switch";
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysInitialize())) {
            if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) {
                out += std::string(" ") + fw.display_version;
            }
            setsysExit();
        }
        out += appletGetAppletType() == AppletType_Application ? " (title)" : " (applet)";
        return out;
#elif __PS5__
        return "PS5";
#elif __PS4__
#ifdef PPLAY_PS4_JAILBREAK
        return "PS4 (jailbreak)";
#else
        return "PS4";
#endif
#elif defined(_WIN32)
        return "Windows";
#elif defined(__linux__)
        return "Linux";
#else
        return "unknown";
#endif
    }

    c2d::Vector2f resolution() {
        return {1920, 1080};
    }

    void started(Main *main) {
        logRenderer(main);
        pplay::DesktopWindow::setup(main);
#ifdef __SWITCH__
        appletLockExit();
        appletHook(&applet_hook_cookie, on_applet_hook, main);
        appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
#elif __PS4__
        (void) main;
        char report[256];
        ps4_clock_report(report, sizeof(report));
        pplay::Utility::log(pplay::Utility::LogLevel::Info, std::string("PS4 clocks: ") + report);
#else
        (void) main;
#endif
    }

    bool renderPaused() {
#ifdef __PS5__
        return g_renderPaused;
#else
        return false;
#endif
    }

    void frameDone(Main *main) {
        pplay::DesktopWindow::update(main);
#ifdef __PS5__
        ps5RestMode(main);
#endif
        static c2d::C2DClock statsClock;
        if (statsClock.getElapsedTime().asSeconds() >= 10) {
            statsClock.restart();
            pplay::Utility::log(pplay::Utility::LogLevel::Debug, "fps: " + std::to_string((int) main->getFps()));
        }
    }

    void exit() {
#ifdef __SWITCH__
        usbExit();
        appletUnhook(&applet_hook_cookie);
        appletUnlockExit();
#ifdef NDEBUG
        socketExit();
#endif
        csrngExit();
#elif __PS5__
        // hand control back to the system; without this the BigApp stays up
        // after the loop ends and the screen goes black
        sceSystemServiceLoadExec("exit", nullptr);
        while (true) {
        }
#elif __PS4__
#ifdef PPLAY_PS4_JAILBREAK
        terminate_jbc();
#endif
        sceSystemServiceLoadExec((char *) "exit", nullptr);
        while (true) {
        }
#endif
    }
}
