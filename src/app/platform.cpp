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

    bool init() {
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

    void frameDone(Main *main) {
        pplay::DesktopWindow::update(main);
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
