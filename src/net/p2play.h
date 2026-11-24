//
// P2Play MediaMesh network client. This module is intentionally separate from torrserve.*.
//
#ifndef PPLAY_P2PLAY_H
#define PPLAY_P2PLAY_H

#include <cstdint>
#include <string>
#include <vector>

#include "cross2d/c2d.h"

namespace pplay::P2Play {

    struct TorrentFile { int index = -1; std::string path; int64_t size = 0; };
    struct TorrentInfo { std::string hash, title; int64_t size = 0; std::vector<TorrentFile> files; };
    struct CatalogRecord { std::string hash, title; int64_t size = 0, date = 0; int files = 0; std::vector<std::string> tags; };

    bool isSearchPromptPath(const std::string &path);
    bool isSearchResultsPath(const std::string &path);
    bool isSearchResultPath(const std::string &path);
    std::string searchResultsPath(const std::string &contextPath, const std::string &query);
    std::string searchResultHash(const std::string &path);
    std::string directTorrentPath(const std::string &contextPath, const std::string &torrentHash);

    bool previewCatalogRecord(const std::string &contextPath, const std::string &recordHash, int timeout,
                              CatalogRecord *record, TorrentInfo *torrent, std::string *error);
    bool addCatalogRecord(const std::string &contextPath, const std::string &recordHash, int timeout,
                          TorrentInfo *torrent, std::string *error);
    bool previewTorrentLink(const std::string &contextPath, const std::string &link, const std::string &title,
                            int timeout, TorrentInfo *torrent, std::string *error);
    bool addTorrentLink(const std::string &contextPath, const std::string &link, const std::string &title,
                        int timeout, TorrentInfo *torrent, std::string *error);

    // Browse discovered/subscribed MediaMesh lists and records as a virtual filesystem.
    std::vector<c2d::Io::File> getDirList(const std::string &path, int timeout, std::string *error = nullptr);

    // Convert a P2Play virtual file path into its authenticated native stream URL.
    std::string toStreamUrl(const std::string &path);

    // Called when a network's credentials or TLS policy change.
    void forgetFailures();
}

#endif // PPLAY_P2PLAY_H
