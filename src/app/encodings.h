//
// Single byte texts (old tags, file names) to UTF-8: Windows code pages, guessed.
//

#ifndef PPLAY_ENCODINGS_H
#define PPLAY_ENCODINGS_H

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace encoding {
    namespace detail {

        inline bool isValidUtf8(const std::string &text) {
            int remaining = 0;
            for (unsigned char c: text) {
                if (remaining == 0) {
                    if (c < 0x80) {
                        continue;
                    } else if ((c >> 5) == 0x6) {
                        remaining = 1;
                    } else if ((c >> 4) == 0xE) {
                        remaining = 2;
                    } else if ((c >> 3) == 0x1E) {
                        remaining = 3;
                    } else {
                        return false;
                    }
                } else {
                    if ((c >> 6) != 0x2) {
                        return false;
                    }
                    remaining--;
                }
            }
            return remaining == 0;
        }

        inline uint32_t winCpToUnicode(unsigned char c, int cp) {
            if (c < 0x80) {
                return c;
            }
            if (c < 0xC0) {
                if (cp == 1251) {
                    static const uint16_t symbols[] = {
                            0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409,
                            0x2039, 0x040A, 0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                            0x2013, 0x2014, 0x003F, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0,
                            0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB,
                            0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6,
                            0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457};
                    return symbols[c - 0x80];
                }
                if (cp == 1250) {
                    static const uint16_t central[] = {
                            0x20AC, 0x003F, 0x201A, 0x003F, 0x201E, 0x2026, 0x2020, 0x2021, 0x003F, 0x2030, 0x0160,
                            0x2039, 0x015A, 0x0164, 0x017D, 0x0179, 0x003F, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                            0x2013, 0x2014, 0x003F, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A, 0x00A0,
                            0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB,
                            0x00AC, 0x00AD, 0x00AE, 0x017B, 0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6,
                            0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C};
                    return central[c - 0x80];
                }
                // the others: cp1252 ones (the Latin-1 symbols from 0xA0)
                static const uint16_t western[] = {0x20AC, 0x003F, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                                   0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x003F, 0x017D, 0x003F,
                                                   0x003F, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                                   0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x003F, 0x017E, 0x0178};
                return c >= 0xA0 ? c : western[c - 0x80];
            }
            switch (cp) {
                case 1250: {
                    static const uint16_t upper[] = {0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
                                                     0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
                                                     0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
                                                     0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF};
                    static const uint16_t lower[] = {0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
                                                     0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
                                                     0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
                                                     0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9};
                    return (c < 0xE0) ? upper[c - 0xC0] : lower[c - 0xE0];
                }
                case 1251:
                    return 0x0410 + (c - 0xC0);
                case 1253:
                    // 0xD2 and 0xFF: unused
                    return c == 0xD2 || c == 0xFF ? '?' : c + 0x2D0;
                case 1254: {
                    switch (c) {
                        case 0xD0:
                            return 0x011E;
                        case 0xDD:
                            return 0x0130;
                        case 0xDE:
                            return 0x015E;
                        case 0xF0:
                            return 0x011F;
                        case 0xFD:
                            return 0x0131;
                        case 0xFE:
                            return 0x015F;
                        default:
                            return c;
                    }
                }
                case 1255:
                    return (c >= 0xE0 && c <= 0xFA) ? 0x05D0 + (c - 0xE0) : '?';
                case 1256: {
                    static const uint16_t arabic[] = {
                            0x06C1, 0x0621, 0x0622, 0x0623, 0x0624, 0x0625, 0x0626, 0x0627, 0x0628, 0x0629, 0x062A,
                            0x062B, 0x062C, 0x062D, 0x062E, 0x062F, 0x0630, 0x0631, 0x0632, 0x0633, 0x0634, 0x0635,
                            0x0636, 0x00D7, 0x0637, 0x0638, 0x0639, 0x063A, 0x0640, 0x0641, 0x0642, 0x0643, 0x00E0,
                            0x0644, 0x00E2, 0x0645, 0x0646, 0x0647, 0x0648, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB,
                            0x0649, 0x064A, 0x00EE, 0x00EF, 0x064B, 0x064C, 0x064D, 0x064E, 0x00F4, 0x064F, 0x0650,
                            0x00F7, 0x0651, 0x00F9, 0x0652, 0x00FB, 0x00FC, 0x200E, 0x200F, 0x06D2};
                    return arabic[c - 0xC0];
                }
                case 1257: {
                    static const uint16_t baltic[] = {
                            0x0104, 0x012E, 0x0100, 0x0106, 0x00C4, 0x00C5, 0x0118, 0x0112, 0x010C, 0x00C9, 0x0179,
                            0x0116, 0x0122, 0x0136, 0x012A, 0x013B, 0x0160, 0x0143, 0x0145, 0x00D3, 0x014C, 0x00D5,
                            0x00D6, 0x00D7, 0x0172, 0x0141, 0x015A, 0x016A, 0x00DC, 0x017B, 0x017D, 0x00DF, 0x0105,
                            0x012F, 0x0101, 0x0107, 0x00E4, 0x00E5, 0x0119, 0x0113, 0x010D, 0x00E9, 0x017A, 0x0117,
                            0x0123, 0x0137, 0x012B, 0x013C, 0x0161, 0x0144, 0x0146, 0x00F3, 0x014D, 0x00F5, 0x00F6,
                            0x00F7, 0x0173, 0x0142, 0x015B, 0x016B, 0x00FC, 0x017C, 0x017E, 0x02D9};
                    return baltic[c - 0xC0];
                }
                default: // 1252
                    return c;
            }
        }

        inline void appendUtf8(std::string &out, uint32_t cp) {
            if (cp < 0x80) {
                out.push_back((char) cp);
            } else if (cp < 0x800) {
                out.push_back((char) ((cp >> 6) | 0xC0));
                out.push_back((char) ((cp & 0x3F) | 0x80));
            } else {
                out.push_back((char) ((cp >> 12) | 0xE0));
                out.push_back((char) (((cp >> 6) & 0x3F) | 0x80));
                out.push_back((char) ((cp & 0x3F) | 0x80));
            }
        }

        // the longest run of bytes 0xC0+: words of a non Latin script
        inline size_t highRun(const std::string &text) {
            size_t run = 0, longest = 0;
            for (unsigned char c: text) {
                run = c >= 0xC0 ? run + 1 : 0;
                longest = std::max(longest, run);
            }
            return longest;
        }

        // words of a non Latin script: runs of bytes 0xC0+, more of them than Latin letters
        inline bool nonLatin(const std::string &text) {
            size_t ascii = 0, high = 0;
            for (unsigned char c: text) {
                ascii += ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') ? 1 : 0;
                high += c >= 0xC0 ? 1 : 0;
            }
            return highRun(text) >= 3 && high * 2 >= ascii;
        }

        inline int detectWindowsCodePage(const std::string &text) {
            if (nonLatin(text)) {
                // Cyrillic, Greek, Hebrew, Arabic share the bytes: the one with the most
                // frequent letters of its languages
                struct Script {
                    int cp;
                    uint16_t letters[16];
                };
                static const Script scripts[] = {{1251,
                                                  {0x043E, 0x0435, 0x0430, 0x0438, 0x043D, 0x0442, 0x0441, 0x0440,
                                                   0x0432, 0x043B, 0x043A, 0x043C, 0x0434, 0x043F, 0x0443, 0x044F}},
                                                 {1253,
                                                  {0x03B1, 0x03BF, 0x03B9, 0x03B5, 0x03C4, 0x03C3, 0x03BD, 0x03B7,
                                                   0x03C5, 0x03C1, 0x03C0, 0x03BA, 0x03BC, 0x03BB, 0x03C2, 0x03AC}},
                                                 {1255,
                                                  {0x05D9, 0x05D5, 0x05D4, 0x05DC, 0x05D0, 0x05E8, 0x05EA, 0x05D1,
                                                   0x05DE, 0x05E9, 0x05DD, 0x05E0, 0x05E2, 0x05DB, 0x05D3, 0x05DF}},
                                                 {1256,
                                                  {0x0627, 0x0644, 0x064A, 0x0645, 0x0648, 0x0646, 0x0647, 0x0631,
                                                   0x062A, 0x0628, 0x0639, 0x062F, 0x0633, 0x0641, 0x0643, 0x0629}}};
                int best = 1251;
                size_t bestScore = 0;
                for (auto &script: scripts) {
                    size_t score = 0;
                    for (unsigned char c: text) {
                        uint32_t u = winCpToUnicode(c, script.cp);
                        score += std::count(script.letters, script.letters + 16, u);
                    }
                    if (score > bestScore) {
                        best = script.cp;
                        bestScore = score;
                    }
                }
                return best;
            }
            // Latin scripts: a few letters only, their own bytes
            size_t central = 0, turkish = 0, baltic = 0;
            for (unsigned char c: text) {
                central += c == 0x8A || c == 0x8E || c == 0x9A || c == 0x9E || c == 0xB9 || c == 0xBE || c == 0xC8
                                           || c == 0xE8 || c == 0xF8 || c == 0xD8
                                   ? 1
                                   : 0;
                turkish += c == 0xD0 || c == 0xDD || c == 0xDE || c == 0xF0 || c == 0xFD || c == 0xFE ? 1 : 0;
                baltic += c == 0xC0 || c == 0xE0 || c == 0xD0 || c == 0xF0 || c == 0xDB || c == 0xFB ? 1 : 0;
            }
            size_t most = std::max(central, std::max(turkish, baltic));
            if (most == 0) {
                return 1252;
            }
            return most == central ? 1250 : most == turkish ? 1254 : 1257;
        }

        // a single byte text decoded as Latin-1 (old tags read by ffmpeg / mpv): the bytes
        // back, "" if it does not look like one (real accented Latin text: a letter here and
        // there, these ones: whole words)
        inline std::string latin1Bytes(const std::string &text) {
            std::string bytes;
            for (size_t i = 0; i < text.size(); i++) {
                auto c = (unsigned char) text[i];
                if (c < 0x80) {
                    bytes.push_back((char) c);
                } else if ((c == 0xC2 || c == 0xC3) && i + 1 < text.size()) {
                    bytes.push_back((char) (((c & 0x1F) << 6) | ((unsigned char) text[++i] & 0x3F)));
                } else {
                    return ""; // above U+00FF: real Unicode
                }
            }
            return nonLatin(bytes) ? bytes : "";
        }
    }

    inline std::string fix(const std::string &input) {
        if (input.empty()) {
            return input;
        }
        std::string text = input;
        if (detail::isValidUtf8(text)) {
            text = detail::latin1Bytes(text);
            if (text.empty()) {
                return input;
            }
        }
        int cp = detail::detectWindowsCodePage(text);
        std::string result;
        result.reserve(text.size() * 2);
        for (unsigned char c: text) {
            detail::appendUtf8(result, detail::winCpToUnicode(c, cp));
        }
        return result;
    }
}

#endif
