//
// Watched marks of a whole directory: its media files listed in a thread (subdirectories
// too), counted, then marked watched or reset at once.
//

#ifndef PPLAY_FOLDER_MARKS_H
#define PPLAY_FOLDER_MARKS_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"

class PPLAYConfig;

namespace pplay::FolderMarks {

    struct State {
        bool ready = false;
        int files = 0;
        // files with a position or a mark (local or the server one)
        int marked = 0;
        // files not watched (marked, or by progress if set)
        int unwatched = 0;
    };

    void start(PPLAYConfig *config);

    void stop();

    // the thread started if needed, woken
    void wake();

    // listed again (force) or if another directory
    void request(const c2d::Io::File &dir, bool force = false);

    // of the requested directory
    State stateOf(const std::string &path);

    // changes when a listing is done
    unsigned int generation();

    // a row directory: as counted last (counted again in the background if not lately)
    State cached(const c2d::Io::File &dir);

    // all counted again when shown (marks changed)
    void forget();

    // changes when a row count changes
    unsigned int rowsGeneration();

    // the media files of the listed directory
    std::vector<c2d::Io::File> files(const std::string &path);
}

#endif //PPLAY_FOLDER_MARKS_H
