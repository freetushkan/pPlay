//
// System on-screen keyboard
// Switch: swkbd
// PS4/PS5: IME dialog
// Linux: a text box typed with the keyboard
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
#elif defined(__LINUX__)
#include <algorithm>
#include "main.h"
#include "i18n.h"
#include "input_map.h"
#endif

#define SCE_IME_OPTION_MULTILINE 0x00000001
#ifdef __LINUX__
#define IME_MAX_SINGLE 4096
#define IME_MAX_MULTI 65536
#else
#define IME_MAX_SINGLE 1024
#define IME_MAX_MULTI 2048
#endif

namespace {

    // utf-8 <-> utf-16 (with surrogate pairs)
    std::vector<uint16_t> toUtf16(const std::string &s) {
        std::vector<uint16_t> out;
        out.reserve(s.size() + 1);
        for (size_t i = 0; i < s.size();) {
            auto c = (unsigned char) s[i];
            uint32_t cp;
            size_t len;
            if (c < 0x80) {
                cp = c;
                len = 1;
            } else if ((c & 0xE0) == 0xC0) {
                cp = c & 0x1F;
                len = 2;
            } else if ((c & 0xF0) == 0xE0) {
                cp = c & 0x0F;
                len = 3;
            } else if ((c & 0xF8) == 0xF0) {
                cp = c & 0x07;
                len = 4;
            } else {
                i++;
                continue;
            }
            if (i + len > s.size()) {
                break;
            }
            for (size_t k = 1; k < len; k++) {
                cp = (cp << 6) | ((unsigned char) s[i + k] & 0x3F);
            }
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

#ifdef __LINUX__
namespace {
    // utf-8 code point boundaries
    size_t prevChar(const std::string &s, size_t i) {
        if (i == 0) {
            return 0;
        }
        i--;
        while (i > 0 && ((unsigned char) s[i] & 0xC0) == 0x80) {
            i--;
        }
        return i;
    }

    size_t nextChar(const std::string &s, size_t i) {
        if (i >= s.size()) {
            return s.size();
        }
        i++;
        while (i < s.size() && ((unsigned char) s[i] & 0xC0) == 0x80) {
            i++;
        }
        return i;
    }

    // the text with the cursor, the lines around it when they don't fit
    std::string visibleText(const std::string &text, size_t cursor, int maxLines) {
        std::string shown = text.substr(0, cursor) + "\u2502" + text.substr(cursor);
        std::vector<std::string> lines;
        size_t start = 0, cursorLine = 0, cursorPos = cursor;
        while (true) {
            size_t eol = shown.find('\n', start);
            std::string line = shown.substr(start, eol == std::string::npos ? std::string::npos : eol - start);
            if (cursorPos >= start && (eol == std::string::npos || cursorPos <= eol)) {
                cursorLine = lines.size();
            }
            lines.push_back(line.empty() ? " " : line);
            if (eol == std::string::npos) {
                break;
            }
            start = eol + 1;
        }
        size_t first = 0;
        if ((int) lines.size() > maxLines) {
            first = cursorLine >= (size_t) maxLines / 2 ? cursorLine - maxLines / 2 : 0;
            first = std::min(first, lines.size() - (size_t) maxLines);
        }
        std::string out;
        for (size_t i = first; i < lines.size() && i < first + (size_t) maxLines; i++) {
            out += (i > first ? "\n" : "") + lines[i];
        }
        return out;
    }

    // modal box, the keyboard typed text: no shortcut is handled meanwhile
    bool editText(const std::string &title, std::string &text, bool multiline, size_t max) {
        using c2d::Color; // COLOR_* macros
        auto *main = (Main *) c2d_renderer;
        const c2d::Vector2f size = main->getSize();
        const float scale = main->getScaling().y;
        const float margin = 24 * scale;

        auto *dim = new c2d::RectangleShape({0, 0, size.x, size.y});
        dim->setFillColor(c2d::Color(0, 0, 0, 170));
        const float boxHeight = size.y * (multiline ? 0.76f : 0.34f);
        auto *box = new c2d::RectangleShape({size.x * 0.1f, (size.y - boxHeight) / 2, size.x * 0.8f, boxHeight});
        box->setFillColor(COLOR_BG);
        box->setOutlineColor(COLOR_ACCENT);
        box->setOutlineThickness(2 * scale);
        dim->add(box);

        auto *titleText = new c2d::Text(title, main->getFontSize(Main::FontSize::Big), main->getFont());
        titleText->setFillColor(COLOR_FONT);
        titleText->setPosition(margin, margin);
        titleText->setSizeMax(box->getSize().x - margin * 2, 0);
        box->add(titleText);

        auto *hint = new c2d::Text(pplay::tr(multiline ? "Ctrl+Enter: OK, Esc: cancel" : "Enter: OK, Esc: cancel"),
                                   main->getFontSize(Main::FontSize::Small), main->getFont());
        hint->setFillColor(COLOR_GRAY_LIGHT);
        hint->setOrigin(c2d::Origin::BottomLeft);
        hint->setPosition(margin, box->getSize().y - margin);
        box->add(hint);

        const unsigned int fontSize = main->getFontSize(Main::FontSize::Medium);
        const float bodyTop = margin * 2 + (float) main->getFontSize(Main::FontSize::Big);
        const float bodyHeight = box->getSize().y - bodyTop - margin * 2 - (float) fontSize;
        auto *body = new c2d::Text("", fontSize, main->getFont());
        body->setFillColor(COLOR_FONT);
        body->setOverflow(c2d::Text::NewLine);
        body->setPosition(margin, bodyTop);
        body->setSizeMax(box->getSize().x - margin * 2, bodyHeight);
        box->add(body);
        const int maxLines = std::max(1, (int) (bodyHeight / ((float) fontSize * 1.25f)));

        main->add(dim);
        dim->setLayer(20000);

        pplay::InputMap::setTextInput(true);
        SDL_StartTextInput();
        std::string value = text;
        size_t cursor = value.size();
        int result = 0; // 1 ok, -1 cancel
        while (result == 0) {
            SDL_Event event;
            while (result == 0 && SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    result = -1;
                } else if (event.type == SDL_TEXTINPUT) {
                    std::string in = event.text.text;
                    if (pplay::Keyboard::length(value + in) <= max) {
                        value.insert(cursor, in);
                        cursor += in.size();
                    }
                } else if (event.type == SDL_KEYDOWN) {
                    const SDL_Keycode key = event.key.keysym.sym;
                    const bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0;
                    if (key == SDLK_ESCAPE) {
                        result = -1;
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        if (multiline && !ctrl) {
                            value.insert(cursor, "\n");
                            cursor++;
                        } else {
                            result = 1;
                        }
                    } else if (key == SDLK_BACKSPACE && cursor > 0) {
                        size_t from = prevChar(value, cursor);
                        value.erase(from, cursor - from);
                        cursor = from;
                    } else if (key == SDLK_DELETE && cursor < value.size()) {
                        value.erase(cursor, nextChar(value, cursor) - cursor);
                    } else if (key == SDLK_LEFT) {
                        cursor = prevChar(value, cursor);
                    } else if (key == SDLK_RIGHT) {
                        cursor = nextChar(value, cursor);
                    } else if (key == SDLK_HOME) {
                        size_t bol = cursor > 0 ? value.rfind('\n', cursor - 1) : std::string::npos;
                        cursor = ctrl || bol == std::string::npos ? 0 : bol + 1;
                    } else if (key == SDLK_END) {
                        size_t eol = value.find('\n', cursor);
                        cursor = ctrl || eol == std::string::npos ? value.size() : eol;
                    } else if (key == SDLK_UP || key == SDLK_DOWN) {
                        // same column, previous / next line
                        size_t bol = cursor > 0 ? value.rfind('\n', cursor - 1) : std::string::npos;
                        bol = bol == std::string::npos ? 0 : bol + 1;
                        size_t column = cursor - bol;
                        if (key == SDLK_UP && bol > 0) {
                            size_t prev = bol > 1 ? value.rfind('\n', bol - 2) : std::string::npos;
                            prev = prev == std::string::npos ? 0 : prev + 1;
                            cursor = std::min(prev + column, bol - 1);
                        } else if (key == SDLK_DOWN) {
                            size_t eol = value.find('\n', cursor);
                            if (eol != std::string::npos) {
                                size_t next = value.find('\n', eol + 1);
                                cursor = std::min(eol + 1 + column, next == std::string::npos ? value.size() : next);
                            }
                        }
                        // utf-8: back to a code point start
                        while (cursor > 0 && cursor < value.size() && ((unsigned char) value[cursor] & 0xC0) == 0x80) {
                            cursor--;
                        }
                    } else if (ctrl && key == SDLK_v && SDL_HasClipboardText()) {
                        char *clip = SDL_GetClipboardText();
                        std::string in = clip ? clip : "";
                        SDL_free(clip);
                        if (!multiline) {
                            std::replace(in.begin(), in.end(), '\n', ' ');
                        }
                        if (pplay::Keyboard::length(value + in) <= max) {
                            value.insert(cursor, in);
                            cursor += in.size();
                        }
                    }
                }
            }
            body->setString(visibleText(value, cursor, maxLines));
            c2d_renderer->flip(true, false);
        }
        SDL_StopTextInput();
        pplay::InputMap::setTextInput(false);
        delete dim;
        main->getInput()->clear();

        if (result > 0) {
            text = value;
            return true;
        }
        return false;
    }
}
#endif

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
#elif defined(__LINUX__)
        return editText(title, text, multiline, max);
#else
        (void) title;
        (void) multiline;
        return false;
#endif
    }
}
