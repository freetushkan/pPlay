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
    struct CatalogDetails {
        std::string title, description, posterBytes, posterCacheKey;
        std::string descriptionError, posterError;
        int64_t size = 0, date = 0;
        int files = 0;
        std::vector<std::string> tags;
    };
    struct CatalogList {
        std::string pubkey, name, description;
        bool meta = false;
        std::vector<std::string> includes;
    };

    struct ServerUser {
        std::string id, username;
        int rank = 0;
        bool banned = false;
    };
    struct PlaybackProgress { double position = 0; double duration = 0; bool viewed = false; };
    struct ProgressEntry {
        std::string hash, title, fileName, updatedAt;
        int fileIndex = -1;
        double position = 0, duration = 0;
        bool viewed = false;
    };

    bool isSearchPromptPath(const std::string &path);
    bool isSearchResultsPath(const std::string &path);
    bool isContinueWatchingPath(const std::string &path);
    std::string progressLabel(const std::string &path);
    bool isSearchResultPath(const std::string &path);
    std::string searchResultsPath(const std::string &contextPath, const std::string &query,
                                  const std::vector<std::string> &selectedLists = {});
    bool getCatalogLists(const std::string &contextPath, int timeout,
                         std::vector<CatalogList> *lists, std::string *error);

    // Silo-compatible user management. Tokens are deliberately not exposed.
    bool getUsers(const std::string &contextPath, int timeout, std::vector<ServerUser> *users,
                  int *actorRank, std::string *error);
    bool createUser(const std::string &contextPath, const std::string &username, const std::string &password,
                    int rank, int timeout, std::string *error);
    bool updateUserPassword(const std::string &contextPath, const std::string &userId,
                            const std::string &password, int timeout, std::string *error);
    bool setUserRank(const std::string &contextPath, const std::string &userId,
                     int rank, int timeout, std::string *error);
    bool deleteUser(const std::string &contextPath, const std::string &userId,
                    int timeout, std::string *error);
    struct LibraryTorrent { std::string hash, title; int64_t size = 0; int64_t addedAt = 0; bool viewed = false; };
    bool getLibrary(const std::string &contextPath, int timeout, std::vector<LibraryTorrent> *torrents, std::string *error);
    bool deleteTorrent(const std::string &contextPath, const std::string &hash, int timeout, std::string *error);
    bool setFileViewed(const std::string &filePath, bool viewed, int timeout, std::string *error);
    bool getFileProgress(const std::string &filePath, int timeout, PlaybackProgress *progress, std::string *error);
    bool setFileProgress(const std::string &filePath, double position, double duration, int timeout, std::string *error);
    bool getProgress(const std::string &contextPath, int timeout, std::vector<ProgressEntry> *progress,
                    std::string *error);
    // Fetch signed description/poster refs only when a result is selected, never during search.
    bool getCatalogDetails(const std::string &contextPath, const std::string &recordHash, int timeout,
                           CatalogDetails *details, std::string *error);
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
