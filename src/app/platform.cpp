//
// Platform specific startup and shutdown.
//

#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "main.h"
#include "platform.h"
#include "desktop_window.h"
#include "utility.h"

#ifdef __ANDROID__
#include <atomic>
#include "crashlog.h"
#include <jni.h>
#include <unistd.h>
extern "C" {
#include <libavcodec/jni.h>
}

// the library loaded by SDLActivity: ffmpeg's MediaCodec needs the VM
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
    av_jni_set_java_vm(vm, nullptr);
    return JNI_VERSION_1_6;
}

namespace {
    // the app sent to the background / back (SDL event watch: the UI thread)
    std::atomic<bool> g_background{false};
    std::atomic<bool> g_closeAsked{false};
    std::atomic<mpv_handle *> g_mpv{nullptr};
    bool g_closeInBackground = false;

    int onAppEvent(void *, SDL_Event *event) {
        if (event->type == SDL_APP_WILLENTERBACKGROUND) {
            g_background = true;
            // the main loop blocked there
            pplay::CrashLog::setIdle(true);
            if (g_closeInBackground) {
                g_closeAsked = true;
            } else if (g_mpv.load() != nullptr) {
                // kept: no sound behind another app
                mpv_set_property_string(g_mpv.load(), "pause", "yes");
            }
        } else if (event->type == SDL_APP_DIDENTERFOREGROUND) {
            g_background = false;
            pplay::CrashLog::setIdle(false);
        }
        return 1;
    }
}
#endif

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
#include "i18n.h"

extern "C" {
int sceSystemServiceLoadExec(const char *path, const char *args[]);
int sceSystemServiceReceiveEvent(void *event);
int sceSystemServiceEnableSuspendNotification();
int sceSystemServiceDeclareReadyForSuspend();
int sceSystemServicePowerTick();
int32_t sceAgcSuspendPoint();
int sceKernelSendNotificationRequest(int device, void *request, size_t size, int blocking);
int sceSystemStateMgrEnterStandby();
int sceVideoOutOpen(int userId, int busType, int index, const void *param);
int sceVideoOutClose(int handle);
int sceVideoOutGetResolutionStatus(int handle, void *status);
}

// Rest mode: the system suspends pPlay fine once, but suspending it again after it
// was resumed panics the kernel (A53MM: the app's mapper rings are not suspended).
// So by default pPlay closes when the system announces a suspend (a click on its
// icon after the wake starts it again). "Keep in rest mode" gets ready for the
// suspend instead (unstable).
namespace {
    const int32_t EVENT_ON_RESUME = 0x10000000;
    // with suspend notifications on: the system waits for sceSystemServiceDeclareReadyForSuspend
    const int32_t EVENT_SUSPEND = 0x20001000;

    // a system toast (it stays after pPlay exits)
    void notify(const std::string &text) {
        struct {
            char unused[45];
            char message[3075];
        } request{};
        snprintf(request.message, sizeof(request.message), "%s", text.c_str());
        sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
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
#ifdef PPLAY_PS5_KEEP_IN_REST
        bool keep = main->getConfig()->getOption(OPT_PS5_KEEP_IN_REST)->getInteger() != 0;
#else
        bool keep = false;
#endif
        if (!g_notifications) {
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
            } else if (type == EVENT_SUSPEND && keep) {
                readyForSuspend(main, true);
            } else if (type == EVENT_SUSPEND && !g_quitting) {
                // gone before the suspend (not declared ready: the system waits for the exit)
                pplay::Utility::log(pplay::Utility::LogLevel::Info, "PS5: suspend announced, closing");
                g_quitting = true;
                main->closeForRest();
            }
        }

        if (resumed) {
            g_renderPaused = false;
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                std::string("PS5: resumed by the system") + (keep ? ", kept running" : ", closing"));
            if (!keep && !g_quitting) {
                // suspended without the notice: suspending it again would panic the kernel
                g_quitting = true;
                notify(pplay::tr("pPlay was closed after rest mode, start it again"));
                main->closeForRest();
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

namespace {
    // the output mode of the system (fullWidth, fullHeight, then the pane: the display area)
    struct VideoOutStatus {
        int32_t fullWidth;
        int32_t fullHeight;
        int32_t paneWidth;
        int32_t paneHeight;
        uint64_t refreshRate;
        float screenSizeInInch;
        uint16_t flags;
        uint8_t reserved[118];
    };

    // asked before SDL opens the video output; false: unknown
    bool screenSize(int *width, int *height) {
        int handle = sceVideoOutOpen(0xFF, 0, 0, nullptr);
        if (handle < 0) {
            char text[16];
            snprintf(text, sizeof(text), "0x%08X", (unsigned) handle);
            pplay::Utility::log(pplay::Utility::LogLevel::Info, std::string("Resolution: video out not opened: ") + text);
            return false;
        }
        VideoOutStatus status{};
        int res = sceVideoOutGetResolutionStatus(handle, &status);
        sceVideoOutClose(handle);
        if (res != 0 || status.fullWidth <= 0 || status.fullHeight <= 0) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "Resolution: video out status " + std::to_string(res));
            return false;
        }
        char text[160];
        snprintf(text, sizeof(text), "Resolution: video out %dx%d, pane %dx%d, refresh %llu, %.1f in, flags 0x%x",
                 status.fullWidth, status.fullHeight, status.paneWidth, status.paneHeight,
                 (unsigned long long) status.refreshRate, status.screenSizeInInch, status.flags);
        pplay::Utility::log(pplay::Utility::LogLevel::Info, text);
        *width = status.fullWidth;
        *height = status.fullHeight;
        return true;
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
static std::string sizeText(int w, int h) {
    return std::to_string(w) + "x" + std::to_string(h);
}

// each size on the way to the screen (Info): the renderer, the window, its GL drawable, the modes
static void logResolution(Main *main) {
    using pplay::Utility;
    SDL_Window *window = ((c2d::SDL2Renderer *) c2d_renderer)->getWindow();
    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GL_GetDrawableSize(window, &dw, &dh);
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLint maxTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    Utility::log(Utility::LogLevel::Info, "Resolution: renderer " + sizeText((int) main->getSize().x, (int) main->getSize().y)
                                                  + ", window " + sizeText(ww, wh) + ", drawable " + sizeText(dw, dh)
                                                  + ", viewport " + sizeText(viewport[2], viewport[3])
                                                  + ", max texture " + std::to_string(maxTexture));
    int display = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(display < 0 ? 0 : display, &mode) == 0) {
        Utility::log(Utility::LogLevel::Info, "Resolution: display current " + sizeText(mode.w, mode.h) + " @"
                                                      + std::to_string(mode.refresh_rate));
    }
    if (SDL_GetDesktopDisplayMode(display < 0 ? 0 : display, &mode) == 0) {
        Utility::log(Utility::LogLevel::Info, "Resolution: display desktop " + sizeText(mode.w, mode.h));
    }
    int modes = SDL_GetNumDisplayModes(display < 0 ? 0 : display);
    for (int i = 0; i < modes; i++) {
        if (SDL_GetDisplayMode(display < 0 ? 0 : display, i, &mode) == 0) {
            Utility::log(Utility::LogLevel::Info, "Resolution: display mode " + std::to_string(i) + " "
                                                          + sizeText(mode.w, mode.h));
        }
    }
    char scale[96];
    snprintf(scale, sizeof(scale), "scaling %.4f x %.4f, object scale %.4f x %.4f, position %.1f, %.1f",
             main->getScaling().x, main->getScaling().y, main->getScale().x, main->getScale().y,
             main->getPosition().x, main->getPosition().y);
    Utility::log(Utility::LogLevel::Info, std::string("Resolution: ") + scale + ", fonts "
                                                  + std::to_string(main->getFontSize(Main::FontSize::Small)) + "/"
                                                  + std::to_string(main->getFontSize(Main::FontSize::Medium)) + "/"
                                                  + std::to_string(main->getFontSize(Main::FontSize::Big)));
}

#if defined(__PS5__) || defined(__ANDROID__)
// the interface drawn on the whole GL drawable (a window other than asked): scaled by a whole
// factor when it can be, centered
static void fitDrawable(Main *main) {
    SDL_Window *window = ((c2d::SDL2Renderer *) c2d_renderer)->getWindow();
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(window, &dw, &dh);
    c2d::Vector2f ui = main->getSize();
    if (dw <= 0 || dh <= 0 || ((float) dw == ui.x && (float) dh == ui.y)) {
        return;
    }
    float scale = std::min((float) dw / ui.x, (float) dh / ui.y);
    float whole = std::floor(scale + 0.001f);
    if (whole >= 1 && std::fabs(scale - whole) < 0.001f) {
        scale = whole;
    }
    main->setScale(scale, scale);
    main->setPosition(std::round(((float) dw - ui.x * scale) / 2), std::round(((float) dh - ui.y * scale) / 2));
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Resolution: the interface scaled by "
                                                                + std::to_string(scale) + " to the drawable");
}
#endif

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
#elif defined(__ANDROID__)
        // the remote "Back" as a key, not the activity closed
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
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
#elif defined(__ANDROID__)
        return "Android (" + c2d::C2DDevice::getName() + ")";
#elif defined(_WIN32)
        return "Windows";
#elif defined(__linux__)
        return "Linux";
#else
        return "unknown";
#endif
    }

    c2d::Vector2f resolution(PPLAYConfig *config) {
#ifdef __PS5__
        c2d::Vector2f size = {1920, 1080};
        int w = 0, h = 0;
        bool known = screenSize(&w, &h);
        if (config->getOption(OPT_PS5_UI_4K)->getInteger() != 0) {
            if (!known || (w >= 3840 && h >= 2160)) {
                size = {3840, 2160};
            } else {
                pplay::Utility::log(pplay::Utility::LogLevel::Info, "Resolution: not a 4K output, 1080p used");
            }
        }
#else
#ifdef __ANDROID__
        // before SDL starts: closing needs the loop running in the background
        g_closeInBackground = config->getOption(OPT_ANDROID_BACKGROUND)->getInteger() != 0;
        SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, g_closeInBackground ? "0" : "1");
#endif
        (void) config;
        c2d::Vector2f size = {1920, 1080};
#endif
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Resolution: asked " + std::to_string((int) size.x)
                                                                    + "x" + std::to_string((int) size.y));
        return size;
    }

    void started(Main *main) {
        logRenderer(main);
#if defined(__PS5__) || defined(__ANDROID__)
        fitDrawable(main);
#endif
        logResolution(main);
        pplay::DesktopWindow::setup(main);
#ifdef __SWITCH__
        appletLockExit();
        appletHook(&applet_hook_cookie, on_applet_hook, main);
        appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
#elif defined(__ANDROID__)
        g_mpv = main->getPlayer()->getMpv()->getHandle();
        SDL_AddEventWatch(onAppEvent, nullptr);
#elif __PS4__
        (void) main;
        char report[256];
        ps4_clock_report(report, sizeof(report));
        pplay::Utility::log(pplay::Utility::LogLevel::Info, std::string("PS4 clocks: ") + report);
#else
        (void) main;
#endif
    }

    void keepAwake(bool on, bool playing) {
#ifdef __SWITCH__
        // playing: no dimming either
        static int last = -1;
        int state = on ? (playing ? 2 : 1) : 0;
        if (state != last) {
            appletSetAutoSleepDisabled(on);
            appletSetMediaPlaybackState(on && playing);
            last = state;
        }
#elif defined(__PS4__)
        // the video playback tick: what video apps report to keep the console awake (the power
        // tick alone not enough); the OpenOrbis headers declare them void: results logged
        static c2d::C2DClock tick;
        if (on && tick.getElapsedTime().asSeconds() > 5) {
            static int lastVideo = 1, lastTick = 1, lastSaver = 1;
            int videoResult = ((int (*)()) sceSystemServiceTickVideoPlayback)();
            int tickResult = ((int (*)()) sceSystemServicePowerTick)();
            int saverResult = ((int (*)()) sceSystemServiceTurnOffScreenSaver)();
            if (videoResult != lastVideo || tickResult != lastTick || saverResult != lastSaver) {
                char text[128];
                snprintf(text, sizeof(text), "PS4: video tick 0x%08x, power tick 0x%08x, screen saver off 0x%08x",
                         (unsigned int) videoResult, (unsigned int) tickResult, (unsigned int) saverResult);
                pplay::Utility::log(pplay::Utility::LogLevel::Info, text);
                lastVideo = videoResult;
                lastTick = tickResult;
                lastSaver = saverResult;
            }
            tick.restart();
        }
#elif !defined(__PS5__)
        // Linux, Android: the screen saver / the screen kept on
        static int last = -1;
        if ((int) on != last) {
            on ? SDL_DisableScreenSaver() : SDL_EnableScreenSaver();
            last = on;
        }
#else
        (void) on;
#endif
        (void) playing;
    }

#ifdef __ANDROID__
    // a static method of PPlayActivity; its String result if it has one
    static std::string callActivity(const char *method, const char *signature, ...) {
        auto *env = (JNIEnv *) SDL_AndroidGetJNIEnv();
        auto activity = (jobject) SDL_AndroidGetActivity();
        if (env == nullptr || activity == nullptr) {
            return "";
        }
        std::string value;
        jclass cls = env->GetObjectClass(activity);
        jmethodID mid = env->GetStaticMethodID(cls, method, signature);
        if (mid == nullptr) {
            env->ExceptionClear();
        } else {
            va_list args;
            va_start(args, signature);
            bool string = signature[strlen(signature) - 1] == ';';
            jobject result = string ? env->CallStaticObjectMethodV(cls, mid, args) : nullptr;
            if (!string) {
                env->CallStaticVoidMethodV(cls, mid, args);
            }
            va_end(args);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            } else if (result != nullptr) {
                const char *utf = env->GetStringUTFChars((jstring) result, nullptr);
                if (utf != nullptr) {
                    value = utf;
                    env->ReleaseStringUTFChars((jstring) result, utf);
                }
            }
            if (result != nullptr) {
                env->DeleteLocalRef(result);
            }
        }
        env->DeleteLocalRef(cls);
        env->DeleteLocalRef(activity);
        return value;
    }
#endif

    std::string takeViewed(std::string *name) {
#ifdef __ANDROID__
        std::string value = callActivity("takeViewed", "()Ljava/lang/String;");
        size_t nl = value.find('\n');
        *name = nl == std::string::npos ? value : value.substr(nl + 1);
        return value.substr(0, nl);
#else
        (void) name;
        return "";
#endif
    }

    void mediaState(const std::string &title, int state, long positionMs, long durationMs) {
#ifdef __ANDROID__
        auto *env = (JNIEnv *) SDL_AndroidGetJNIEnv();
        if (env == nullptr) {
            return;
        }
        jstring jTitle = env->NewStringUTF(title.c_str());
        callActivity("mediaState", "(Ljava/lang/String;IJJ)V", jTitle, (jint) state, (jlong) positionMs,
                     (jlong) durationMs);
        env->DeleteLocalRef(jTitle);
#else
        (void) title, (void) state, (void) positionMs, (void) durationMs;
#endif
    }

    std::string takeMediaCommand() {
#ifdef __ANDROID__
        return callActivity("takeMediaCommand", "()Ljava/lang/String;");
#else
        return "";
#endif
    }

    bool hasStandby() {
#if defined(__PS5__) || defined(__SWITCH__)
        return true;
#else
        return false;
#endif
    }

    bool sleepNow() {
#ifdef __PS5__
        int res = sceSystemStateMgrEnterStandby();
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "PS5: rest mode asked: " + std::to_string(res));
        return res == 0;
#elif defined(__SWITCH__)
        Result res = appletRequestToSleep();
        pplay::Utility::log(pplay::Utility::LogLevel::Info, "Switch: sleep asked: " + std::to_string(res));
        return R_SUCCEEDED(res);
#else
        return false;
#endif
    }

    float backlight() {
#ifdef __SWITCH__
        static bool ready = R_SUCCEEDED(lblInitialize());
        float value = 0;
        if (!ready || appletGetOperationMode() != AppletOperationMode_Handheld
            || R_FAILED(lblGetCurrentBrightnessSetting(&value))) {
            return -1;
        }
        return value;
#else
        return -1;
#endif
    }

    void setBacklight(float value) {
#ifdef __SWITCH__
        if (backlight() >= 0) {
            lblSetCurrentBrightnessSetting(std::max(0.0f, std::min(1.0f, value)));
        }
#else
        (void) value;
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
#elif defined(__ANDROID__)
        pplay::CrashLog::heartbeat();
        // closed in the background: the playing file reopened at the next start
        if (g_closeAsked.exchange(false)) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "Android: closing in the background");
            main->closeForRest();
        }
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
#elif defined(__ANDROID__)
        // the process ended (a next start in it would find the statics of this one); no
        // destructors: the threads of mpv and Android still run (the settings saved already)
        g_mpv = nullptr;
        fflush(nullptr);
        _exit(0);
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
