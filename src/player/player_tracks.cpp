//
// Player tracks: audio / subtitles menus, preferred languages, external files, the
// choices carried to the next files.
//

#include <algorithm>

#include "main.h"
#include "player.h"
#include "filer.h"
#include "input_map.h"
#include "paths.h"
#include "i18n.h"

using namespace c2d;

void Player::applyLanguages() {
    if (!mpv->isAvailable()) {
        return;
    }
    auto preferred = [this](const char *option, const std::string &fallback) {
        std::string value = main->getConfig()->getOption(option)->getString();
        return value.empty() || value == "Auto" || value == "Off" ? fallback : value;
    };
    // only on change: mpv may select the tracks again
    std::string alang = preferred(OPT_AUDIO_LANGUAGE, confAudioLang);
    std::string slang = preferred(OPT_SUB_LANGUAGE, confSubLang);
    if (alang != appliedAudioLang) {
        appliedAudioLang = alang;
        mpv_set_property_string(mpv->getHandle(), "alang", alang.c_str());
    }
    if (slang != appliedSubLang) {
        appliedSubLang = slang;
        mpv_set_property_string(mpv->getHandle(), "slang", slang.c_str());
    }
}

void Player::startExternalTracks() {
    // the user bindings first (remembered selection), then the files found
    externalBindings = pplay::ExternalTracks::load(playingKey);
    for (auto &track: externalBindings) {
        if (track.selected) {
            externalSelect[track.audio ? 0 : 1] = pplay::ExternalTracks::toMpvUrl(track.path);
        }
        addExternalTrack(track);
    }

    if (main->getConfig()->getOption(OPT_EXTERNAL_TRACKS)->getInteger() == 0 || isPlaylistFile()
        || file.type != c2d::Io::Type::File) {
        return;
    }
    // one search at a time: a running one (previous file) restarts when done
    if (externalThread != nullptr) {
        externalRestart = true;
        return;
    }
    externalJobGeneration = externalGeneration;
    externalJobPath = file.path;
    externalJobName = file.name;
    externalFound.clear();
    externalDone = false;
    externalThread = new c2d::C2DThread(externalFindThread, this);
}

int Player::externalFindThread(void *data) {
    auto *self = (Player *) data;
    int timeout = self->main->getConfig()->getOption(OPT_NETWORK_TIMEOUT)->getInteger();
    self->externalFound =
            pplay::ExternalTracks::find(pplay::Io::get(), self->externalJobPath, self->externalJobName, timeout);
    self->externalDone = true;
    return 0;
}

void Player::updateExternalTracks() {
    if (externalThread == nullptr || !externalDone) {
        return;
    }
    externalThread->join();
    delete (externalThread);
    externalThread = nullptr;

    if (externalJobGeneration == externalGeneration && !mpv->isStopped()) {
        for (auto &track: externalFound) {
            addExternalTrack(track);
        }
    }
    if (externalRestart) {
        externalRestart = false;
        if (externalJobGeneration != externalGeneration && !mpv->isStopped()) {
            startExternalTracks();
        }
    }
}

void Player::addExternalTrack(const pplay::ExternalTracks::Track &track) {
    std::string url = pplay::ExternalTracks::toMpvUrl(track.path);
    if (std::find(externalAdded.begin(), externalAdded.end(), url) != externalAdded.end()) {
        return;
    }
    externalAdded.push_back(url);
    externalAll.push_back(track);

    // async: the file is opened by mpv, without blocking the ui
    const char *args[] = {track.audio ? "audio-add" : "sub-add",
                          url.c_str(),
                          "auto",
                          track.title.c_str(),
                          track.lang.empty() ? nullptr : track.lang.c_str(),
                          nullptr};
    int res = mpv_command_async(mpv->getHandle(), ExternalReply + (uint64_t) externalGeneration, args);
    pplay::Utility::log(res < 0 ? pplay::Utility::LogLevel::Error : pplay::Utility::LogLevel::Info,
                        std::string("Player: external ") + (track.audio ? "audio" : "subtitles") + " \"" + track.title
                                + "\" " + (track.lang.empty() ? "" : "(" + track.lang + ") ") + url
                                + (res < 0 ? std::string(": ") + mpv_error_string(res) : ""));
    if (res >= 0) {
        externalPending++;
    }
}

void Player::onExternalTracksAdded() {
    if (mpv->isStopped()) {
        return;
    }
    file.mediaInfo = mpv->getMediaInfo(file);
    buildTrackMenus();

    std::vector<Mpv::TrackEntry> tracks = mpv->getTrackList();
    for (int t = 0; t < 2; t++) {
        bool audio = t == 0;
        const std::string type = audio ? "audio" : "sub";
        int current = audio ? mpv->getAid() : mpv->getSid();
        int target = -1;
        if (!externalSelect[t].empty()) {
            // remembered or just picked
            for (auto &track: tracks) {
                if (track.type == type && track.external && track.filename == externalSelect[t]) {
                    target = track.id;
                    externalSelect[t].clear();
                    break;
                }
            }
        } else if (!hadWatchLater && !trackChoiceApplied[t]) {
            // preferred language, if the current track is not
            std::string preferred =
                    main->getConfig()->getOption(audio ? OPT_AUDIO_LANGUAGE : OPT_SUB_LANGUAGE)->getString();
            if (!preferred.empty() && preferred != "Auto" && preferred != "Off") {
                bool currentOk = false;
                for (auto &track: tracks) {
                    if (track.type == type && track.id == current
                        && pplay::ExternalTracks::matchLanguage(preferred, track.lang)) {
                        currentOk = true;
                    }
                }
                for (auto &track: tracks) {
                    if (!currentOk && track.type == type
                        && pplay::ExternalTracks::matchLanguage(preferred, track.lang)) {
                        target = track.id;
                        break;
                    }
                }
            }
        }
        if (target >= 0 && target != current) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "Player: select " + type + " track " + std::to_string(target));
            audio ? mpv->setAid(target) : mpv->setSid(target);
        }
    }
    selectAudioIfNone();
}

std::string Player::trackLayout(const std::string &type) {
    std::string layout;
    for (auto &track: mpv->getTrackList()) {
        if (track.type == type && !track.external) {
            layout += std::to_string(track.id) + ":" + track.lang + ";";
        }
    }
    return layout;
}

void Player::rememberTrackChoice(bool audio, int trackId) {
    const std::string type = audio ? "audio" : "sub";
    TrackChoice &choice = trackChoice[audio ? 0 : 1];
    bool external = false;
    for (auto &track: mpv->getTrackList()) {
        external |= track.type == type && track.id == trackId && track.external;
    }
    // external ones: remembered per file (ExternalTracks bindings)
    choice.set = !external;
    choice.id = trackId;
    choice.layout = trackLayout(type);
}

void Player::applyTrackChoices() {
    for (int t = 0; t < 2; t++) {
        bool audio = t == 0;
        const std::string type = audio ? "audio" : "sub";
        TrackChoice &choice = trackChoice[t];
        trackChoiceApplied[t] = false;
        // the file own choice first (watch later), then the same tracks only
        if (!choice.set || hadWatchLater || trackLayout(type) != choice.layout) {
            continue;
        }
        trackChoiceApplied[t] = true;
        int current = audio ? mpv->getAid() : mpv->getSid();
        if (current != choice.id) {
            pplay::Utility::log(pplay::Utility::LogLevel::Info,
                                "Player: previous file " + type + " track " + std::to_string(choice.id));
            audio ? mpv->setAid(choice.id) : mpv->setSid(choice.id);
        }
    }
}

void Player::selectAudioIfNone() {
    // an old watch later may hold an external track id (not added yet / another one):
    // mpv then plays without audio, the first track instead (internal ones first)
    if (trackChoiceApplied[0] && trackChoice[0].id < 0) {
        return; // audio off, chosen
    }
    std::vector<Mpv::TrackEntry> tracks = mpv->getTrackList();
    int target = -1;
    bool targetExternal = true;
    for (auto &track: tracks) {
        if (track.type != "audio") {
            continue;
        }
        if (track.selected) {
            return;
        }
        if (target < 0 || (targetExternal && !track.external)) {
            target = track.id;
            targetExternal = track.external;
        }
    }
    if (target >= 0) {
        pplay::Utility::log(pplay::Utility::LogLevel::Info,
                            "Player: no audio selected, track " + std::to_string(target));
        mpv->setAid(target);
    }
}

void Player::rememberSelection(bool audio, int trackId) {
    if (externalAll.empty()) {
        return;
    }
    std::string url;
    for (auto &track: mpv->getTrackList()) {
        if (track.id == trackId && track.type == (audio ? "audio" : "sub") && track.external) {
            url = track.filename;
        }
    }
    bool changed = false, found = false;
    for (auto &binding: externalBindings) {
        if (binding.audio != audio) {
            continue;
        }
        bool selected = !url.empty() && pplay::ExternalTracks::toMpvUrl(binding.path) == url;
        changed |= binding.selected != selected;
        binding.selected = selected;
        found |= selected;
    }
    if (!url.empty() && !found) {
        for (auto &track: externalAll) {
            if (track.audio == audio && pplay::ExternalTracks::toMpvUrl(track.path) == url) {
                pplay::ExternalTracks::Track binding = track;
                binding.selected = true;
                externalBindings.push_back(binding);
                changed = true;
                break;
            }
        }
    }
    if (changed) {
        pplay::ExternalTracks::save(playingKey, externalBindings);
    }
}

void Player::pickExternalTrack(bool audio) {
    if (mpv->isStopped()) {
        return;
    }
    setFullscreen(false);
    std::string hint = pplay::tr(audio ? "Choose an audio file" : "Choose a subtitles file") + "\n"
                       + pplay::InputMap::buttonName(c2d::Input::Button::X) + " " + pplay::tr("cancel");
    main->getFiler()->startPick(
            audio ? pplay::ExternalTracks::audioExtensions() : pplay::ExternalTracks::subtitleExtensions(),
            pplay::Paths::extractDirPath(file.path), hint, [this, audio](const MediaFile &picked) {
                setFullscreen(true);
                if (mpv->isStopped()) {
                    return;
                }
                pplay::ExternalTracks::Track track = pplay::ExternalTracks::fromPath(audio, picked.path, file.path);
                track.selected = true;
                // remembered, the selected one of its type
                bool exists = false;
                for (auto &binding: externalBindings) {
                    if (binding.audio == audio) {
                        binding.selected = binding.path == track.path;
                        exists |= binding.selected;
                    }
                }
                if (!exists) {
                    externalBindings.push_back(track);
                }
                pplay::ExternalTracks::save(playingKey, externalBindings);

                std::string url = pplay::ExternalTracks::toMpvUrl(track.path);
                externalSelect[audio ? 0 : 1] = url;
                if (std::find(externalAdded.begin(), externalAdded.end(), url) != externalAdded.end()) {
                    if (externalPending == 0) {
                        onExternalTracksAdded();
                    }
                } else {
                    addExternalTrack(track);
                }
            });
}

void Player::setVideoStream(int streamId) {
    mpv->setVid(streamId);
}

void Player::setAudioStream(int streamId) {
    mpv->setAid(streamId);
    rememberSelection(true, streamId);
    rememberTrackChoice(true, streamId);
}

void Player::setSubtitleStream(int streamId) {
    mpv->setSid(streamId);
    rememberSelection(false, streamId);
    rememberTrackChoice(false, streamId);
}

int Player::getVideoStream() {
    return mpv->getVid();
}

int Player::getAudioStream() {
    return mpv->getAid();
}

int Player::getSubtitleStream() {
    return mpv->getSid();
}
