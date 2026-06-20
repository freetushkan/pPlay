#ifdef __SWITCH__

#include <chrono>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <switch.h>
#include "usbfs.h"
#include "utility.h"

static std::mutex g_usbMutex;
static std::vector<UsbHsFsDevice> g_usbDevices;

static UEvent *g_statusChangeEvent = nullptr;
static UEvent g_exitEvent = {};
static Thread g_usbThread = {};

static bool g_usbInitialized = false;
static bool g_usbThreadCreated = false;
static u32 g_usbDeviceCount = 0;
static u32 g_usbListedDeviceCount = 0;

static void usbPrintDevicesLocked(const char *prefix) {
    printf("%s mounted devices=%u, listed=%u\n", prefix, g_usbDeviceCount, g_usbListedDeviceCount);
    pplay::Utility::log(pplay::Utility::LogLevel::Info,
        std::string(prefix) + " mounted devices=" + std::to_string(g_usbDeviceCount) +
        ", listed=" + std::to_string(g_usbListedDeviceCount));

    for (u32 i = 0; i < g_usbListedDeviceCount; i++) {
        printf("%s device[%u]=%s\n", prefix, i, g_usbDevices[i].name);
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
            std::string(prefix) + " device[" + std::to_string(i) + "]=" + std::string(g_usbDevices[i].name));
    }
}

static void usbRefreshMountedDevices() {
    std::vector<UsbHsFsDevice> devices;
    u32 count = 0;
    u32 listed = 0;

    count = usbHsFsGetMountedDeviceCount();
    if (count) {
        devices.resize(count);
        listed = usbHsFsListMountedDevices(devices.data(), count);
        devices.resize(listed);
    }

    {
        std::lock_guard<std::mutex> lock(g_usbMutex);
        g_usbDevices.swap(devices);
        g_usbDeviceCount = count;
        g_usbListedDeviceCount = listed;

        usbPrintDevicesLocked("usbRefreshMountedDevices:");
    }
}

static void usbMscThreadFunc(void *arg) {
    (void)arg;

    Waiter status_change_event_waiter = waiterForUEvent(g_statusChangeEvent);
    Waiter exit_event_waiter = waiterForUEvent(&g_exitEvent);

    while (true) {
        int idx = 0;
        Result rc = waitMulti(&idx, -1, status_change_event_waiter, exit_event_waiter);
        if (R_FAILED(rc)) {
            continue;
        }

        if (idx == 1) {
            printf("usbMscThreadFunc: exit event triggered\n");
            pplay::Utility::log(pplay::Utility::LogLevel::Info, "usbMscThreadFunc: exit event triggered");
            break;
        }

        printf("usbMscThreadFunc: USB Mass Storage status change event triggered\n");
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
            "usbMscThreadFunc: USB Mass Storage status change event triggered");

        usbRefreshMountedDevices();
    }
}

void usbInit() {
    {
        std::lock_guard<std::mutex> lock(g_usbMutex);
        if (g_usbInitialized) {
            return;
        }
    }

    printf("usbInit\n");
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "usbInit()");

    Result rc = usbHsFsInitialize(0);
    printf("usbHsFsInitialize: 0x%X\n", rc);

    {
        std::stringstream ss;
        ss << "usbHsFsInitialize: 0x" << std::hex << rc;
        pplay::Utility::log(pplay::Utility::LogLevel::Info, ss.str());
    }

    if (R_FAILED(rc)) {
        return;
    }

    g_statusChangeEvent = usbHsFsGetStatusChangeUserEvent();
    if (!g_statusChangeEvent) {
        printf("usbInit: failed to get status change event\n");
        pplay::Utility::log(pplay::Utility::LogLevel::Error, "usbInit: failed to get status change event");
        usbHsFsExit();
        return;
    }

    ueventCreate(&g_exitEvent, true);

    rc = threadCreate(&g_usbThread, usbMscThreadFunc, nullptr, nullptr, 0x10000, 0x2C, -2);
    if (R_FAILED(rc)) {
        printf("usbInit: threadCreate failed: 0x%X\n", rc);
        {
            std::stringstream ss;
            ss << "usbInit: threadCreate failed: 0x" << std::hex << rc;
            pplay::Utility::log(pplay::Utility::LogLevel::Error, ss.str());
        }
        usbHsFsExit();
        return;
    }

    rc = threadStart(&g_usbThread);
    if (R_FAILED(rc)) {
        printf("usbInit: threadStart failed: 0x%X\n", rc);
        {
            std::stringstream ss;
            ss << "usbInit: threadStart failed: 0x" << std::hex << rc;
            pplay::Utility::log(pplay::Utility::LogLevel::Error, ss.str());
        }
        threadClose(&g_usbThread);
        usbHsFsExit();
        return;
    }

    g_usbThreadCreated = true;

    usbRefreshMountedDevices();

    {
        std::lock_guard<std::mutex> lock(g_usbMutex);
        g_usbInitialized = true;
    }

    printf("usbInit: done\n");
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "usbInit(): done");
}

void usbExit() {
    {
        std::lock_guard<std::mutex> lock(g_usbMutex);
        if (!g_usbInitialized) {
            return;
        }
        g_usbInitialized = false;
    }

    if (g_usbThreadCreated) {
        ueventSignal(&g_exitEvent);
        threadWaitForExit(&g_usbThread);
        threadClose(&g_usbThread);
        g_usbThreadCreated = false;
    }

    {
        std::lock_guard<std::mutex> lock(g_usbMutex);
        g_usbDevices.clear();
        g_usbDeviceCount = 0;
        g_usbListedDeviceCount = 0;
    }

    usbHsFsExit();

    printf("usbExit: done\n");
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "usbExit(): done");
}

bool usbWaitForDevice(int timeoutSeconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);

    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(g_usbMutex);
            if (!g_usbDevices.empty()) {
                return true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return false;
}

std::string usbGetFirstMountName() {
    std::lock_guard<std::mutex> lock(g_usbMutex);
    return g_usbDevices.empty() ? std::string() : std::string(g_usbDevices.front().name) + "/";
}

std::vector<std::string> usbGetMountNames() {
    std::vector<std::string> names;

    std::lock_guard<std::mutex> lock(g_usbMutex);
    names.reserve(g_usbDevices.size());

    for (const auto &device: g_usbDevices) {
        names.emplace_back(std::string(device.name) + "/");
    }

    return names;
}

#endif
