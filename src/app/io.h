//
// Local files and network sources (protocols in src/net).
//

#ifndef PPLAY_IO_H
#define PPLAY_IO_H

#include <atomic>
#include <mutex>
#include <unordered_map>
#include "cross2d/c2d.h"

#ifdef __SMB2__
#include "smb.h"
#endif

class PPLAYConfig;

namespace pplay {

    class Io : public c2d::C2DIo {

    public:

        using c2d::C2DIo::getDirList;

        Io();

        ~Io() override;

        // the application io
        static Io *get();

        // the network open in the filer (main menu), 0 if none: the one taken among the same
        // address and user (the filer requests)
        void setCurrentNetwork(int index);
        int lastP2PlayNetworkIndex();

        // the networks settings changed: the paths found again
        void forgetNetworks();

        enum class DeviceType {
            Local,
            Http,
            Ftp,
            Smb,
            TorrServe,
            P2Play,
            WebDav,
            Dlna
        };

        // NETWORKx_TYPE
        enum class NetworkType {
            Auto,
            TorrServe,
            P2Play,
            Smb,
            Ftp,
            Http,
            WebDav,
            Dlna,
            // the new TorrServer (its silo branch): its own api, a token for the login
            TorrServeSilo
        };

        static NetworkType parseNetworkType(const std::string &name);

        static const std::vector<std::string> &networkTypeNames();

        // as shown and saved ("" for Auto)
        static std::string networkTypeName(NetworkType type);

        void setConfig(PPLAYConfig *cfg) { config = cfg; }

        std::vector<Io::File> getDirList(const DeviceType &type, const std::vector<std::string> &extensions,
                                         const std::string &path, int timeout = 5, bool sort = false,
                                         bool showHidden = false);

        // listing without the Io state (lastError): usable from another thread
        std::vector<Io::File> list(const DeviceType &type, const std::string &path, int timeout, std::string *error,
                                   bool sort = false, bool showHidden = false);

        // type of the network the path belongs to, or from the url scheme
        DeviceType getDeviceType(const std::string &path);

        // type of the network (its setting) the path belongs to, Auto if none
        NetworkType getNetworkType(const std::string &path);

        // root url of the network the path belongs to, or ""
        std::string getNetworkRoot(const std::string &path);

        // Resolve the playback URL of a virtual source (including P2Play's token URL).
        std::string streamUrl(const std::string &path);

        // the user and password of the network of an url (its options, over the url ones), false if none
        bool credentialsOf(const std::string &url, std::string *user, std::string *password);

        // TS Silo: the token kept for the network of an url ("" if none)
        std::string siloToken(const std::string &url);

        // the user and password set for the network of an url, whatever its type
        bool loginOf(const std::string &url, std::string *user, std::string *password);

        // the settings of the network of a path or url (its defaults if none)
        int networkTimeout(const std::string &path);

        // TLS certificates checked (https): the setting of its network, else of the network of
        // the context (a path there: a poster of a torrent elsewhere), else checked
        bool tlsVerify(const std::string &url, const std::string &context = "");

        // TorrServer: its lists asked again after (s)
        int networkSync(const std::string &root);

        // Samba: the read ahead (MB)
        int smbBuffer(const std::string &url);

        // http(s) / ftp: the url with them (encoded), TS Silo: with its token, else as is
        std::string withCredentials(const std::string &url);

        const std::string &getLastError() const { return lastError; }

        // romfs -> data directory (new or changed files)
        void syncRomFs();

        bool readText(const std::string &path, std::string &out);

        bool writeText(const std::string &path, const std::string &text);

#ifdef __PS4__
        std::string getDataPath() override { return "/data/pplay/"; }
#endif

#ifdef __PS5__
        std::string getDataPath() override { return "/data/homebrew/pplay/"; }
#endif

#ifdef __SWITCH__
        std::string getDataPath() override { return "/config/pplay/"; }
#endif

#ifdef __LINUX__
        std::string getDataPath() override;

        std::string getRomFsPath() override;
#endif

    private:

        std::atomic<int> currentNetwork {0};
        std::atomic<int> lastP2PlayNetwork {0};

        // path -> network index (the filer items ask for each of them, often): the typed ones, all
        std::mutex networkCacheMutex;
        std::unordered_map<std::string, int> networkCache[2];

        // network index of a path, -1 if none
        int findNetwork(const std::string &path, bool typedOnly);

        // (not cached)
        int searchNetwork(const std::string &path, bool typedOnly);

        // false if a file failed
        bool syncRomFsDir(const std::string &relativePath, bool full);

        PPLAYConfig *config = nullptr;
        std::string lastError;
    };
}

#endif //PPLAY_IO_H
