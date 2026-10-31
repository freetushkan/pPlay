//
// TorrServe virtual filesystem helpers.
//

#ifndef PPLAY_TORRSERVE_H
#define PPLAY_TORRSERVE_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"

namespace pplay::TorrServe {

    struct Info {
        std::string title;
        std::string hash;
        std::string poster; // poster url (may be empty)
        std::string category;
        int64_t torrentSize = 0;
        int64_t filesSize = 0;
        size_t fileCount = 0;
        int fileIndex = -1; // selected file, if any
        std::string fileName;
        int64_t fileSize = 0;
    };

    // TorrServer virtual file system: <server>/<torrent title>/<torrent dirs...>/<file>?link=..&index=..
    std::vector<c2d::Io::File> getDirList(const std::string &path, int timeout, std::string *error = nullptr);
    // torrent information of a path, from the last listing (no network access)
    bool getInfo(const std::string &path, Info &info);
    // url to give to mpv for a filer path
    std::string toStreamUrl(const std::string &path);
    bool isFileViewed(const std::string &path);
    // the position the server keeps for a file (its "track timecode" setting), 0 if none
    double fileProgress(const std::string &path);
    // sent in a thread
    void setFileProgress(const std::string &path, double seconds);
    bool remFileViewed(const std::string &path);
    // the kept position: mode 0 local only, 1 local first, 2 TorrServer first
    double keptPosition(const std::string &path, int mode);
}

#endif // PPLAY_TORRSERVE_H
