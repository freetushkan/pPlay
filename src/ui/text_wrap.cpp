//
// Word wrap measured with the font.
//

#include "text_wrap.h"

// a paragraph wrapped without the hanging indent
#define FLAT '\x02'

namespace {
    uint32_t nextCodepoint(const std::string &s, size_t &i) {
        auto c = (unsigned char) s[i++];
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        uint32_t cp = extra == 3 ? c & 0x07 : extra == 2 ? c & 0x0F : extra == 1 ? c & 0x1F : c;
        for (; extra > 0 && i < s.size(); extra--) {
            cp = (cp << 6) | ((unsigned char) s[i++] & 0x3F);
        }
        return cp;
    }

    bool breakAfter(uint32_t c) {
        return c == '.' || c == '_' || c == '-' || c == '/' || c == ',' || c == ')' || c == ']';
    }

    // bytes of the word head fitting maxWidth (one character at least)
    size_t fitting(c2d::Font *font, unsigned int size, const std::string &word, float maxWidth) {
        float x = 0;
        uint32_t prev = 0;
        size_t fit = 0, lastBreak = 0;
        for (size_t i = 0; i < word.size();) {
            uint32_t c = nextCodepoint(word, i);
            x += font->getKerning(prev, c, size, false) + font->getGlyph(c, size, false).advance;
            if (x > maxWidth) {
                break;
            }
            fit = i;
            if (breakAfter(c)) {
                lastBreak = i;
            }
            prev = c;
        }
        if (fit == 0) {
            size_t i = 0;
            nextCodepoint(word, i);
            return i;
        }
        // at a separator, unless the piece gets too short
        return lastBreak > fit / 2 ? lastBreak : fit;
    }
}

std::string pplay::TextWrap::noBreak(const std::string &text) {
    std::string out;
    for (char c: text) {
        out += c == ' ' ? "\u00A0" : std::string(1, c);
    }
    return out;
}

float pplay::TextWrap::width(c2d::Font *font, unsigned int size, const std::string &text) {
    float x = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < text.size();) {
        uint32_t c = nextCodepoint(text, i);
        x += font->getKerning(prev, c, size, false) + font->getGlyph(c, size, false).advance;
        prev = c;
    }
    return x;
}

std::string pplay::TextWrap::flat(const std::string &line) {
    return FLAT + line;
}

std::string pplay::TextWrap::wrap(c2d::Font *font, unsigned int size, const std::string &text, float maxWidth,
                                  bool hanging) {
    if (font == nullptr || maxWidth <= 0) {
        return text;
    }
    const float space = width(font, size, " ");
    std::string out;
    size_t pos = 0;
    while (true) {
        size_t eol = text.find('\n', pos);
        std::string paragraph = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        bool hang = hanging;
        if (!paragraph.empty() && paragraph[0] == FLAT) {
            paragraph.erase(0, 1);
            hang = false;
        }

        // continuation lines: the paragraph indent and a bit more
        std::string indent;
        for (size_t i = 0; paragraph.compare(i, 2, "\u00A0") == 0 || (i < paragraph.size() && paragraph[i] == ' ');) {
            size_t n = paragraph[i] == ' ' ? 1 : 2;
            indent += paragraph.substr(i, n);
            i += n;
        }
        indent += hang ? "\u00A0\u00A0\u00A0" : "";
        if (!hang) {
            indent.clear();
        }
        const float indentWidth = width(font, size, indent);

        std::string line;
        float lineWidth = 0;
        bool first = true;
        auto newLine = [&]() {
            out += line + "\n";
            line = indent;
            lineWidth = indentWidth;
            first = false;
        };
        size_t start = 0;
        while (start <= paragraph.size()) {
            size_t end = paragraph.find(' ', start);
            if (end == std::string::npos) {
                end = paragraph.size();
            }
            std::string word = paragraph.substr(start, end - start);
            start = end + 1;

            float w = width(font, size, word);
            bool empty = first ? line.empty() : line == indent;
            if (!empty && lineWidth + space + w <= maxWidth) {
                line += " " + word;
                lineWidth += space + w;
                continue;
            }
            if (!empty) {
                newLine();
            }
            while (lineWidth + w > maxWidth && maxWidth - lineWidth > space) {
                size_t n = fitting(font, size, word, maxWidth - lineWidth);
                line += word.substr(0, n);
                newLine();
                word.erase(0, n);
                w = width(font, size, word);
            }
            line += word;
            lineWidth += w;
        }
        out += line;

        if (eol == std::string::npos) {
            break;
        }
        out += "\n";
        pos = eol + 1;
    }
    // drawn as spaces (some fonts have a visible glyph)
    size_t nbsp;
    while ((nbsp = out.find("\u00A0")) != std::string::npos) {
        out.replace(nbsp, 2, " ");
    }
    return out;
}
