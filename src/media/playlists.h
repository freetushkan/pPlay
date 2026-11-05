//
// Playlists: <home>/playlists/<name>.m3u8, the filer paths of the files (network ones
// too), opened in the filer as one level directories.
//

#ifndef PPLAY_PLAYLISTS_H
#define PPLAY_PLAYLISTS_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"

class PPLAYConfig;

namespace pplay::Playlists {

    void start(PPLAYConfig *config);

    // the additions in progress stopped
    void stop();

    // a message for the status box, once
    bool takeMessage(std::string *type, std::string *message);

    // changes when a playlist is written
    unsigned int generation();

    // <home>/playlists (no trailing slash)
    std::string root();

    bool isRoot(const std::string &path);

    // a playlist file of the root
    bool isPlaylist(const std::string &path);

    // its name (no extension)
    std::string nameOf(const std::string &path);

    // a file, or the media files of a directory (in a thread, sorted as the filer lists them)
    void add(const std::string &name, const c2d::Io::File &file);

    // the root listing: the playlists as directories
    void filter(std::vector<c2d::Io::File> &files);

    std::vector<c2d::Io::File> entries(const std::string &path);

    // the n-th entry (the same file may be there more than once)
    bool removeEntry(const std::string &path, int index);

    // false: the name is used, invalid, or can't be written
    bool rename(const std::string &path, const std::string &name);

    bool remove(const std::string &path);
}

#endif //PPLAY_PLAYLISTS_H
