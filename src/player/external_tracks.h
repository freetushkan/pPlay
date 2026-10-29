//
// External audio / subtitles files: found next to the video (same directory or
// sub directories, by name) or bound by the user (remembered per video).
//

#ifndef PPLAY_EXTERNAL_TRACKS_H
#define PPLAY_EXTERNAL_TRACKS_H

#include <string>
#include <vector>

namespace pplay {

    class Io;

    namespace ExternalTracks {

        struct Track {
            bool audio = true;
            std::string path;      // as listed (TorrServe virtual path, smb://...)
            std::string title;     // shown in the tracks menu
            std::string lang;      // mpv language code, or ""
            bool selected = false; // chosen by the user
        };

        const std::vector<std::string> &audioExtensions();

        const std::vector<std::string> &subtitleExtensions();

        bool isAudio(const std::string &name);

        bool isSubtitle(const std::string &name);

        // the video directory and its sub directories (blocking: network listings)
        std::vector<Track> find(Io *io, const std::string &videoPath, const std::string &videoName, int timeout);

        // user bindings of a video (key: the playing key)
        std::vector<Track> load(const std::string &videoKey);

        void save(const std::string &videoKey, const std::vector<Track> &tracks);

        // title / language of a file picked by the user
        Track fromPath(bool audio, const std::string &path, const std::string &videoPath);

        // path -> url mpv opens (TorrServe stream, smb2://)
        std::string toMpvUrl(const std::string &path);

        // "rus,ru" style list contains the language?
        bool matchLanguage(const std::string &list, const std::string &lang);
    }
}

#endif //PPLAY_EXTERNAL_TRACKS_H
