//
// System on-screen keyboard (Switch swkbd, PS4/PS5 IME dialog).
//

#include <cstdint>
#include <cstring>
#include <vector>

#include "keyboard.h"

#ifdef __SWITCH__
#include <switch.h>
#elif defined(__PS4__)
#include <orbis/UserService.h>
#include <orbis/ImeDialog.h>
#include <orbis/libkernel.h>
#elif defined(__PS5__)
extern "C" {
typedef struct {
    int userId;
    int type;
    uint64_t supportedLanguages;
    int enterLabel;
    int inputMethod;
    void *filter;
    uint32_t option;
    uint32_t maxTextLength;
    wchar_t *inputTextBuffer;
    float posx;
    float posy;
    int halign;
    int valign;
    const wchar_t *placeholder;
    const wchar_t *title;
    int8_t reserved[16];
} SceImeDialogParam;

typedef struct {
    int outcome;
    int8_t reserved[12];
} SceImeDialogResult;

int sceImeDialogInit(const SceImeDialogParam *, void *);
int sceImeDialogGetResult(SceImeDialogResult *);
int sceImeDialogTerm(void);
int sceImeDialogGetStatus(void);
int sceUserServiceGetForegroundUser(int *);
int sceKernelUsleep(unsigned int usec);
}
#define SCE_IME_DIALOG_STATUS_NONE 0
#define SCE_IME_DIALOG_STATUS_RUNNING 1
#define SCE_IME_DIALOG_STATUS_FINISHED 2
#define SCE_IME_DIALOG_END_STATUS_OK 0
#endif

#define SCE_IME_OPTION_MULTILINE 0x00000001
#define IME_MAX_SINGLE 1024
#define IME_MAX_MULTI 2048

namespace {

    // utf-8 <-> utf-16 (with surrogate pairs)
    std::vector<uint16_t> toUtf16(const std::string &s) {
        std::vector<uint16_t> out;
        out.reserve(s.size() + 1);
        for (size_t i = 0; i < s.size();) {
            auto c = (unsigned char) s[i];
            uint32_t cp;
            size_t len;
            if (c < 0x80) { cp = c; len = 1; }
            else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
            else { i++; continue; }
            if (i + len > s.size()) break;
            for (size_t k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char) s[i + k] & 0x3F);
            i += len;
            if (cp >= 0x10000) {
                cp -= 0x10000;
                out.push_back((uint16_t) (0xD800 | (cp >> 10)));
                out.push_back((uint16_t) (0xDC00 | (cp & 0x3FF)));
            } else {
                out.push_back((uint16_t) cp);
            }
        }
        out.push_back(0);
        return out;
    }

    [[maybe_unused]] std::string toUtf8(const uint16_t *s) {
        std::string out;
        for (size_t i = 0; s[i]; i++) {
            uint32_t cp = s[i];
            if ((cp & 0xFC00) == 0xD800 && (s[i + 1] & 0xFC00) == 0xDC00) {
                cp = 0x10000 + (((cp & 0x3FF) << 10) | (s[i + 1] & 0x3FF));
                i++;
            }
            if (cp < 0x80) {
                out += (char) cp;
            } else if (cp < 0x800) {
                out += (char) (0xC0 | (cp >> 6));
                out += (char) (0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += (char) (0xE0 | (cp >> 12));
                out += (char) (0x80 | ((cp >> 6) & 0x3F));
                out += (char) (0x80 | (cp & 0x3F));
            } else {
                out += (char) (0xF0 | (cp >> 18));
                out += (char) (0x80 | ((cp >> 12) & 0x3F));
                out += (char) (0x80 | ((cp >> 6) & 0x3F));
                out += (char) (0x80 | (cp & 0x3F));
            }
        }
        return out;
    }
}

namespace pplay::Keyboard {

    size_t maxLength(bool multiline) {
        return multiline ? IME_MAX_MULTI : IME_MAX_SINGLE;
    }

    size_t length(const std::string &utf8) {
        return toUtf16(utf8).size() - 1;
    }

    bool edit(const std::string &title, std::string &text, bool multiline) {
        const size_t max = maxLength(multiline);
        if (length(text) > max) {
            return false;
        }

#ifdef __SWITCH__
        std::vector<char> out(max * 4 + 1, 0);
        SwkbdConfig kbd;
        if (R_FAILED(swkbdCreate(&kbd, 0))) {
            return false;
        }
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetHeaderText(&kbd, title.c_str());
        swkbdConfigSetGuideText(&kbd, title.c_str());
        swkbdConfigSetInitialText(&kbd, text.c_str());
        swkbdConfigSetStringLenMax(&kbd, (u32) max);
        if (multiline) {
            swkbdConfigSetTextDrawType(&kbd, SwkbdTextDrawType_Box);
            swkbdConfigSetReturnButtonFlag(&kbd, 1);
        }
        const Result rc = swkbdShow(&kbd, out.data(), out.size());
        swkbdClose(&kbd);
        if (R_FAILED(rc)) {
            return false;
        }
        text = out.data();
        return true;
#elif defined(__PS4__) || defined(__PS5__)
        std::vector<uint16_t> buffer = toUtf16(text);
        buffer.resize(max + 1, 0);
        std::vector<uint16_t> titleBuffer = toUtf16(title.size() > 99 ? title.substr(0, 99) : title);

        int uid = 0;
#ifdef __PS4__
        sceUserServiceGetInitialUser(&uid);
        OrbisImeDialogSetting param;
        memset(&param, 0, sizeof(param));
        param.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
        param.type = ORBIS_TYPE_DEFAULT;
#else
        sceUserServiceGetForegroundUser(&uid);
        SceImeDialogParam param;
        memset(&param, 0, sizeof(param));
        param.type = 0;
#endif
        param.userId = uid;
        param.supportedLanguages = 0;
        param.maxTextLength = (uint32_t) max;
        param.option = multiline ? SCE_IME_OPTION_MULTILINE : 0;
        param.inputTextBuffer = reinterpret_cast<wchar_t *>(buffer.data());
        param.title = reinterpret_cast<wchar_t *>(titleBuffer.data());

        if (sceImeDialogInit(&param, nullptr) < 0) {
            return false;
        }

        bool ok = false;
        while (true) {
#ifdef __PS4__
            int status = sceImeDialogGetStatus();
            if (status == ORBIS_DIALOG_STATUS_STOPPED) {
                OrbisDialogResult result;
                memset(&result, 0, sizeof(result));
                sceImeDialogGetResult(&result);
                ok = result.endstatus == ORBIS_DIALOG_OK;
                break;
            }
            if (status == ORBIS_DIALOG_STATUS_NONE) {
                break;
            }
#else
            int status = sceImeDialogGetStatus();
            if (status == SCE_IME_DIALOG_STATUS_FINISHED) {
                SceImeDialogResult result;
                memset(&result, 0, sizeof(result));
                sceImeDialogGetResult(&result);
                ok = result.outcome == SCE_IME_DIALOG_END_STATUS_OK;
                break;
            }
            if (status == SCE_IME_DIALOG_STATUS_NONE) {
                break;
            }
#endif
            sceKernelUsleep(16000);
        }
        sceImeDialogTerm();

        if (ok) {
            buffer.back() = 0;
            text = toUtf8(buffer.data());
        }
        return ok;
#else
        (void) title;
        (void) multiline;
        return false;
#endif
    }
}
