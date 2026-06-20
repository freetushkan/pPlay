#ifndef PPLAY_ENCODINGS_H
#define PPLAY_ENCODINGS_H

#pragma once

#include <string>
#include <cstdint>

namespace encoding {
    namespace detail {

        inline bool isValidUtf8(const std::string &text) {
            int remaining = 0;
            for (unsigned char c : text) {
                if (remaining == 0) {
                    if (c < 0x80) continue;
                    else if ((c >> 5) == 0x6) remaining = 1;
                    else if ((c >> 4) == 0xE) remaining = 2;
                    else if ((c >> 3) == 0x1E) remaining = 3;
                    else return false;
                } else {
                    if ((c >> 6) != 0x2) return false;
                    remaining--;
                }
            }
            return remaining == 0;
        }

        inline int detectWindowsCodePage(const std::string &text) {
            size_t cyrillic = 0;
            size_t latin2 = 0;
            size_t latin1 = 0;
            size_t greek = 0;
            size_t turkish = 0;
            size_t hebrew = 0;
            size_t arabic = 0;
            size_t baltic = 0;

            for (unsigned char c : text) {
                if (c < 0x80) continue;
                if (c >= 0xC0) {
                    cyrillic++;
                }
                if ((c >= 0xC0 && c <= 0xDF) || (c >= 0xE0 && c <= 0xFF)) {
                    if (c == 0xB9 || c == 0xBE || c == 0xFD) latin2++;
                    if (c == 0xDF || c == 0xA7) latin1++;
                    if (c >= 0xC1 && c <= 0xD9) greek++;
                    if (c == 0xFD || c == 0xDD || c == 0xDE) turkish++;
                    if (c >= 0xE0 && c <= 0xFA) hebrew++;
                    if (c >= 0xC1 && c <= 0xEF) arabic++;
                    if (c == 0xCA || c == 0xCC || c == 0xDF) baltic++;
                }
            }
            size_t max_score = cyrillic;
            int best_cp = 1251;
            if (latin1 > max_score) { max_score = latin1; best_cp = 1252; }
            if (latin2 > max_score) { max_score = latin2; best_cp = 1250; }
            if (greek > max_score) { max_score = greek; best_cp = 1253; }
            if (turkish > max_score) { max_score = turkish; best_cp = 1254; }
            if (hebrew > max_score) { max_score = hebrew; best_cp = 1255; }
            if (arabic > max_score) { max_score = arabic; best_cp = 1256; }
            if (baltic > max_score) { max_score = baltic; best_cp = 1257; }
            return best_cp;
        }

        inline uint32_t winCpToUnicode(unsigned char c, int cp) {
            if (c < 0x80) return c;
            if (c >= 0xC0) {
                switch (cp) {
                    case 1250: {
                        static const uint16_t upper[] = {0x0154,0x00C1,0x00C2,0x0102,0x00C4,0x0139,0x0106,0x00C7,0x010C,0x00C9,0x0118,0x00CB,0x011A,0x00CD,0x00CE,0x010E,0x0110,0x0143,0x0147,0x00D3,0x00D4,0x0150,0x00D6,0x00D7,0x0158,0x016E,0x00DA,0x0170,0x00DC,0x00DD,0x0162,0x00DF};
                        static const uint16_t lower[] = {0x0155,0x00E1,0x00E2,0x0103,0x00E4,0x013A,0x0107,0x00E7,0x010D,0x00E9,0x0119,0x00EB,0x011B,0x00ED,0x00EE,0x010F,0x0111,0x0144,0x0148,0x00F3,0x00F4,0x0151,0x00F6,0x00F7,0x0159,0x016F,0x00FA,0x0171,0x00FC,0x00FD,0x0163,0x02D9};
                        return (c < 0xE0) ? upper[c - 0xC0] : lower[c - 0xE0];
                    }
                    case 1251: return 0x0410 + (c - 0xC0);
                    case 1252: return c;
                    case 1253: return (c == 0xDF) ? 0x03CE : 0x0391 + (c - 0xC1);
                    case 1254: {
                        if (c == 0xD0) {return 0x011E;} if (c == 0xDD) {return 0x0130;} if (c == 0xDE) {return 0x015E;}
                        if (c == 0xF0) {return 0x011F;} if (c == 0xFD) {return 0x0131;} if (c == 0xFE) {return 0x015F;}
                        return c;
                    }
                    case 1255: return (c >= 0xE0 && c <= 0xFA) ? 0x05D0 + (c - 0xE0) : c;
                    case 1256: {
                        static const uint16_t arab[] = {0x0640,0x0621,0x0622,0x0623,0x0624,0x0625,0x0626,0x0627,0x0628,0x0629,0x062A,0x062B,0x062C,0x062D,0x062E,0x062F,0x0630,0x0631,0x0632,0x0633,0x0634,0x0635,0x0636,0x00D7,0x0637,0x0638,0x0639,0x063A,0x0641,0x0642,0x0643,0x0644,0x0645,0x0646,0x0647,0x0648,0x00FA,0x064A,0x00FC,0x200E,0x200F,0x064A};
                        return arab[c - 0xC0];
                    }
                    case 0x1257: {
                        static const uint16_t balt[] = {0x0104,0x012E,0x0100,0x0106,0x00C4,0x00C5,0x0118,0x0112,0x010C,0x00C9,0x0179,0x0116,0x0122,0x0130,0x0160,0x0143,0x0145,0x00D3,0x014C,0x00D5,0x00D6,0x00D7,0x017B,0x016A,0x0172,0x00DA,0x00DC,0x00DD,0x017D,0x00DF,0x0105,0x012F,0x0101,0x0107,0x00E4,0x00E5,0x0119,0x0113,0x010D,0x00E9,0x017A,0x0117,0x0123,0x0124,0x0161,0x0144,0x0146,0x00F3,0x014D,0x00F5,0x00F6,0x00F7,0x017C,0x016B,0x0173,0x00FA,0x00FC,0x00FD,0x017E,0x02D9};
                        return balt[c - 0xC0];
                    }
                }
            }

            if (c == 0x80) return 0x20AC;
            return '?'; 
        }
    }

    inline std::string fix(const std::string &text) {
        if (text.empty() || detail::isValidUtf8(text)) {
            return text;
        }
        int detected_cp = detail::detectWindowsCodePage(text);
        std::string result;
        result.reserve(text.size() * 2);
        for (unsigned char c : text) {
            uint32_t cp = detail::winCpToUnicode(c, detected_cp);
            if (cp < 0x80) {
                result.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                result.push_back(static_cast<char>((cp >> 6) | 0xC0));
                result.push_back(static_cast<char>((cp & 0x3F) | 0x80));
            } else {
                result.push_back(static_cast<char>((cp >> 12) | 0xE0));
                result.push_back(static_cast<char>(((cp >> 6) & 0x3F) | 0x80));
                result.push_back(static_cast<char>((cp & 0x3F) | 0x80));
            }
        }
        return result;
    }

}

#endif
