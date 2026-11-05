//
// Saved copies of network files: <home>/.pplay_save/<network>/<its path>, the names made
// safe for any file system, the original names, paths and torrents kept in an index
// (.index). Downloaded in threads, played and shown without the network.
//

#ifndef PPLAY_SAVED_H
#define PPLAY_SAVED_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"
#include "torrserve.h"

class PPLAYConfig;

namespace pplay::Saved {

    struct State {
        // files saved, being saved (a directory: all under it)
        int saved = 0;
        int pending = 0;
        // of the pending bytes, 0..1
        float progress = 0;
        bool failed = false;
    };

    // the index read, the unfinished copies resumed
    void start(PPLAYConfig *config);

    // the downloads stopped (resumed at the next start), the index written
    void stop();

    // each frame: threads joined, the index written, cleanup
    void update();

    // the settings changed (threads, cleanup)
    void configChanged();

    // a message for the status box, once
    bool takeMessage(std::string *type, std::string *message);

    // changes with the states (marks to show again)
    unsigned int generation();

    // <home>/.pplay_save/
    std::string root();

    // a local path in the saved copies (the root too)
    bool isInside(const std::string &path);

    bool isRoot(const std::string &path);

    // a network file / directory
    bool canSave(const c2d::Io::File &file);

    // queued (a directory: its files, listed in a thread)
    void save(const c2d::Io::File &file);

    // of a network or a saved (local) path
    State stateOf(const std::string &path);

    // the copies under a network or a local path, their downloads stopped; the count
    int remove(const std::string &path);

    // a saved directory listing: the original names, the saved files only
    void filter(const std::string &dir, std::vector<c2d::Io::File> &files);

    // the shown names of a saved path, the root excluded
    std::vector<std::string> displayParts(const std::string &path);

    // the torrent of a saved path (as listed then), the poster file ("dir:" url) if any
    bool getInfo(const std::string &path, TorrServe::Info &info, std::string *poster);

    // the last access (cleanup: the oldest first)
    void touch(const std::string &path);

    // the network file of a saved copy, else the path
    std::string originalOf(const std::string &path);

    // the saved copy of a network (or saved) path, "" if none (or unfinished)
    std::string localCopy(const std::string &path);

    // what to open for a path: its saved copy, else its stream url
    std::string openUrl(const std::string &path);
}

#endif //PPLAY_SAVED_H
