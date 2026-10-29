//
// Items of the main menu settings: languages, colors, about...
//

#include "main.h"
#include "menu_main_items.h"
#include "mpv.h"
#include "player.h"

namespace pplay::MenuMainItems {

    std::vector<MenuItem> makeAdjustItems() {
        std::vector<MenuItem> items;
        items.emplace_back("Value", "", MenuItem::Position::Top, 0, false);
        items.emplace_back("-", "", MenuItem::Position::Top, -1);
        items.emplace_back("+", "", MenuItem::Position::Top, 1);
        return items;
    }

    std::string networkKey(int index) {
        return "Network " + std::to_string(index);
    }

    std::vector<MenuItem> makeTrackLanguageItems(const std::string &none) {
        static const std::pair<const char *, const char *> languages[] = {
                {"Russian", "rus,ru,russian"},
                {"English", "eng,en,english"},
                {"Ukrainian", "ukr,uk,ukrainian"},
                {"Belarusian", "bel,be,belarusian"},
                {"Kazakh", "kaz,kk,kazakh"},
                {"Uzbek", "uzb,uz,uzbek"},
                {"Georgian", "geo,kat,ka,georgian"},
                {"Armenian", "arm,hye,hy,armenian"},
                {"Azerbaijani", "aze,az,azerbaijani"},
                {"Polish", "pol,pl,polish"},
                {"Czech", "cze,ces,cs,czech"},
                {"Slovak", "slo,slk,sk,slovak"},
                {"Bulgarian", "bul,bg,bulgarian"},
                {"Serbian", "srp,sr,serbian"},
                {"Croatian", "hrv,hr,croatian"},
                {"Slovenian", "slv,sl,slovenian"},
                {"Romanian", "rum,ron,ro,romanian"},
                {"Hungarian", "hun,hu,hungarian"},
                {"Greek", "gre,ell,el,greek"},
                {"Lithuanian", "lit,lt,lithuanian"},
                {"Latvian", "lav,lv,latvian"},
                {"Estonian", "est,et,estonian"},
                {"Finnish", "fin,fi,finnish"},
                {"Swedish", "swe,sv,swedish"},
                {"Norwegian", "nor,nob,nno,no,nb,nn,norwegian"},
                {"Danish", "dan,da,danish"},
                {"Icelandic", "ice,isl,is,icelandic"},
                {"Dutch", "dut,nld,nl,dutch"},
                {"German", "ger,deu,de,german"},
                {"French", "fre,fra,fr,french"},
                {"Spanish", "spa,es,spanish"},
                {"Catalan", "cat,ca,catalan"},
                {"Portuguese", "por,pt,portuguese"},
                {"Italian", "ita,it,italian"},
                {"Turkish", "tur,tr,turkish"},
                {"Hebrew", "heb,he,hebrew"},
                {"Arabic", "ara,ar,arabic"},
                {"Persian", "per,fas,fa,persian"},
                {"Hindi", "hin,hi,hindi"},
                {"Bengali", "ben,bn,bengali"},
                {"Tamil", "tam,ta,tamil"},
                {"Thai", "tha,th,thai"},
                {"Vietnamese", "vie,vi,vietnamese"},
                {"Indonesian", "ind,id,indonesian"},
                {"Malay", "may,msa,ms,malay"},
                {"Filipino", "fil,tgl,tl,filipino"},
                {"Japanese", "jpn,ja,japanese"},
                {"Korean", "kor,ko,korean"},
                {"Chinese", "chi,zho,zh,cmn,yue,chinese"},
        };
        std::vector<MenuItem> items;
        items.emplace_back(none, "", MenuItem::Position::Top);
        for (const auto &language: languages) {
            items.emplace_back(language.first, "", MenuItem::Position::Top, 0, true, language.second);
        }
        return items;
    }

    static const char *platformName() {
#if defined(__SWITCH__)
        return "Nintendo Switch";
#elif defined(__PS4__)
        return "PlayStation 4";
#elif defined(__PS5__)
        return "PlayStation 5";
#else
        return "Linux";
#endif
    }

    // the people and projects pPlay is built with
    static std::vector<std::string> credits() {
        std::vector<std::string> list = {
                "Cpasjuste — pPlay, libcross2d", "freetushkan — pPlay fork",     "mpv contributors — mpv",
                "FFmpeg developers — FFmpeg",    "libass contributors — libass",
        };
#if defined(__SWITCH__) || defined(__PS5__)
        list.emplace_back("Niklas Haas — libplacebo");
#endif
#ifdef __SWITCH__
        list.emplace_back("VideoLAN — dav1d");
#endif
        list.emplace_back("Daniel Stenberg — curl");
#ifdef __SMB2__
        list.emplace_back("Ronnie Sahlberg, cy33hc — libsmb2");
#endif
        list.emplace_back("Thomas Pfau — ftplib");
        list.emplace_back("D. J. Bernstein — ftpparse");
        list.emplace_back("Niels Lohmann — JSON for Modern C++");
        list.emplace_back("YouROK — TorrServer");
        list.emplace_back("Google — Material Design Icons");
        list.emplace_back("Anthropic — Claude");
        list.emplace_back("OpenAI — Codex");
#ifdef __PS5__
        list.emplace_back("OpenSSL Project — OpenSSL");
        list.emplace_back("Mesa developers — OSMesa");
#else
        list.emplace_back("Trusted Firmware — Mbed TLS");
#endif
        list.emplace_back("FreeType, FriBidi, libpng, libjpeg-turbo, libarchive, zstd, bzip2");
#if defined(__SWITCH__)
        list.emplace_back("devkitPro — devkitA64");
        list.emplace_back("Switchbrew — libnx");
        list.emplace_back("DarkMatterCore — libusbhsfs");
        list.emplace_back("ntfs-3g, lwext4");
#elif defined(__PS4__)
        list.emplace_back("OpenOrbis — PS4 toolchain");
#ifdef PPLAY_PS4_JAILBREAK
        list.emplace_back("sleirsgoevy, bucanero, cy33hc — libjbc");
#endif
#elif defined(__PS5__)
        list.emplace_back("John Törnblom — ps5-payload-sdk");
        list.emplace_back("OpenOrbis — PS4 toolchain (launcher)");
#endif
        return list;
    }

    std::vector<MenuItem> makeAboutItems(Main *main) {
        using Pos = MenuItem::Position;
        std::vector<MenuItem> items;
        // dynamic texts are labels, the names stay menu keys
        MenuItem version("About version", "", Pos::Top);
        version.label = "pPlay v" APP_VERSION_FULL;
        items.push_back(version);
        MenuItem build("About build", "", Pos::Top);
        build.label = std::string(platformName()) + " · " + __DATE__;
        items.push_back(build);
        std::string mpvVersion, ffmpegVersion;
        if (main->getPlayer() != nullptr && main->getPlayer()->getMpv() != nullptr) {
            mpvVersion = main->getPlayer()->getMpv()->getString("mpv-version");
            ffmpegVersion = main->getPlayer()->getMpv()->getString("ffmpeg-version");
        }
        if (!mpvVersion.empty()) {
            MenuItem libs("About libs", "", Pos::Top);
            libs.label = mpvVersion + (ffmpegVersion.empty() ? "" : " · FFmpeg " + ffmpegVersion);
            items.push_back(libs);
        }
        // section title, not selectable
        items.emplace_back("Thanks", "", Pos::Top, 0, false, "#95A5A6");
        for (const auto &credit: credits()) {
            items.emplace_back(credit, "", Pos::Top);
        }
        return items;
    }

    std::vector<MenuItem> makeIntItems(const std::vector<std::pair<std::string, int>> &values) {
        std::vector<MenuItem> items;
        for (auto &value: values) {
            items.emplace_back(value.first, "", MenuItem::Position::Top, value.second);
        }
        return items;
    }

    std::vector<MenuItem> makeAccentColorItems() {
        static const std::pair<const char *, const char *> colors[] = {
                {"Base Blue", "#1078C8"},      {"Muted Cyan", "#008EA0"},        {"Muted Crimson", "#D64545"},
                {"Soft Crimson", "#EB4D4B"},   {"Terracotta Orange", "#DB6B30"}, {"Burnt Orange", "#FA8231"},
                {"Ochre Yellow", "#C29346"},   {"Olive Gold", "#9EA133"},        {"Olive Lime", "#88B04B"},
                {"Sage Green", "#5D9E54"},     {"Soft Emerald", "#3A9668"},      {"Bright Emerald", "#20BF6B"},
                {"Deep Turquoise", "#2E8B96"}, {"Slate Blue", "#4378A6"},        {"Dark Indigo", "#535EA8"},
                {"Soft Purple", "#A55EEA"},    {"Amethyst Purple", "#7A5299"},   {"Plum Violet", "#9C498D"},
                {"Dusty Rose", "#B5526C"},     {"Deep Red", "#D63031"},          {"Classic Red", "#FF0000"},
        };
        std::vector<MenuItem> items;
        for (const auto &color: colors) {
            items.emplace_back(color.first, "", MenuItem::Position::Top, 0, true, color.second);
        }
        return items;
    }
}
