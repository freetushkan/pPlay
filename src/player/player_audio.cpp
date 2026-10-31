//
// Player volume and its normalization (lavfi filter).
//

#include <algorithm>

#include "main.h"
#include "player.h"
#include "player_osd.h"

using namespace c2d;

void Player::setAudioNormalize(int mode) {
    auto *option = main->getConfig()->getOption(OPT_AUDIO_NORMALIZE);
    if (option->getInteger() == mode) {
        return;
    }
    const std::string oldValue = option->getString();
    option->setInteger(mode);
    main->getConfig()->save();
    pplay::Utility::log(pplay::Utility::LogLevel::Info, "Options: " + std::string(OPT_AUDIO_NORMALIZE)
                                                                + " changed from " + oldValue + " to "
                                                                + option->getString());
    applyAudioFilter();
}

void Player::applyAudioFilter() {
    if (!mpv->isAvailable()) {
        return;
    }
    int mode = main->getConfig()->getOption(OPT_AUDIO_NORMALIZE)->getInteger();
    std::string chain;
    if (mode > 0) {
        // decoded channels ("5.1(side)", "stereo"... or "fl-fr-fc-lfe-sl-sr")
        std::string layout = mpv->getString("audio-params/channels");
        if (layout.empty()) {
            return; // audio not started yet: on its reconfig event
        }
        static const std::pair<const char *, const char *> layouts[] = {
                {"mono", "fc"},
                {"stereo", "fl-fr"},
                {"2.1", "fl-fr-lfe"},
                {"3.0", "fl-fr-fc"},
                {"3.0(back)", "fl-fr-bc"},
                {"4.0", "fl-fr-fc-bc"},
                {"quad", "fl-fr-bl-br"},
                {"quad(side)", "fl-fr-sl-sr"},
                {"3.1", "fl-fr-fc-lfe"},
                {"5.0", "fl-fr-fc-bl-br"},
                {"5.0(side)", "fl-fr-fc-sl-sr"},
                {"4.1", "fl-fr-fc-lfe-bc"},
                {"5.1", "fl-fr-fc-lfe-bl-br"},
                {"5.1(side)", "fl-fr-fc-lfe-sl-sr"},
                {"6.0", "fl-fr-fc-bc-sl-sr"},
                {"6.1", "fl-fr-fc-lfe-bc-sl-sr"},
                {"7.0", "fl-fr-fc-bl-br-sl-sr"},
                {"7.1", "fl-fr-fc-lfe-bl-br-sl-sr"},
                {"7.1(wide)", "fl-fr-fc-lfe-bl-br-flc-frc"},
                {"7.1(wide-side)", "fl-fr-fc-lfe-flc-frc-sl-sr"},
        };
        std::string speakers = layout;
        for (auto &known: layouts) {
            if (layout == known.first) {
                speakers = known.second;
            }
        }
        auto has = [&speakers](const char *name) {
            std::string s = "-" + speakers + "-";
            return s.find(std::string("-") + name + "-") != std::string::npos;
        };
        int channels = 0;
        for (char c: speakers) {
            channels += c == '-';
        }
        channels++;

        if (mode == 2 && channels > 2 && has("fc") && has("fl") && has("fr")) {
            // night mode downmix: dialogue (center) up, surrounds down, no LFE
            auto side = [&](const char *front, const char *back, const char *sideName, const char *wide) {
                // "<": gains renormalized (no clipping)
                std::string out = std::string(front) + "<1.0*FC+0.6*" + front;
                for (const char *extra: {back, sideName, wide}) {
                    std::string lower = extra;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    if (has(lower.c_str())) {
                        out += std::string("+0.35*") + extra;
                    }
                }
                return out;
            };
            chain = "pan=stereo|" + side("FL", "BL", "SL", "FLC") + "|" + side("FR", "BR", "SR", "FRC") + ",";
        }
        if (mode == 2) {
            // no rumble, loud peaks down softly, then a slow and limited leveling (a fast
            // one with a big gain pumps the noise up and smears the speech)
            chain += "highpass=f=70,"
                     "acompressor=threshold=0.125:ratio=3:attack=15:release=250:knee=4:makeup=1.4,"
                     "dynaudnorm=f=500:g=31:p=0.5:m=3";
        } else {
            chain += "dynaudnorm=f=500:g=31:p=0.7:m=4";
        }
    }

    if (chain == appliedAudioFilter) {
        return;
    }
    if (!appliedAudioFilter.empty()) {
        const char *remove[] = {"af", "remove", "@pplaynorm", nullptr};
        mpv_command(mpv->getHandle(), remove);
    }
    appliedAudioFilter = chain;
    if (!chain.empty()) {
        std::string filter = "@pplaynorm:lavfi=[" + chain + "]";
        const char *add[] = {"af", "add", filter.c_str(), nullptr};
        int res = mpv_command(mpv->getHandle(), add);
        pplay::Utility::log(res < 0 ? pplay::Utility::LogLevel::Error : pplay::Utility::LogLevel::Info,
                            "Player: audio filter " + chain
                                    + (res < 0 ? std::string(": ") + mpv_error_string(res) : ""));
        if (res < 0) {
            appliedAudioFilter.clear();
        }
    }
}

void Player::changeVolume(double delta) {
    std::string message = mpv->changeVolume(delta);
    // the panel shows it (a chip) when there is one
    if (isFullscreen()) {
        osd->expand();
        osd->showVolume();
    } else {
        showMessage(message);
    }
    volumeDirty = true;
    volumeClock.restart();
}

void Player::saveVolume() {
    volumeDirty = false;
    int volume = (int) mpv->getLastVolume();
    auto *option = main->getConfig()->getOption(OPT_VOLUME);
    if (volume >= 0 && option->getInteger() != volume) {
        option->setInteger(volume);
        main->getConfig()->save();
    }
}
