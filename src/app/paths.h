//
// Path helpers (local paths and urls).
//

#ifndef PPLAY_PATHS_H
#define PPLAY_PATHS_H

#include <string>

namespace pplay::Paths {

    // "smb://host\share\dir" -> "smb://host/share/dir" (the user info is kept as is)
    std::string normalizeSmbPathSeparators(const std::string &path);

    std::string ensureTrailingSlash(const std::string &url);

    // resolve "." / ".." segments (a trailing ".." is kept: "<dir>/..")
    std::string normalizePath(const std::string &path);

    // directory part of a path, with a trailing slash
    std::string extractDirPath(const std::string &path);

    // last path segment, empty for a directory path ("/a/b/")
    std::string getLeafName(const std::string &path);
}

#endif //PPLAY_PATHS_H
