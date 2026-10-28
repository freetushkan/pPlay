//
// Local files and network sources (protocols in src/net).
//

#ifndef PPLAY_IO_H
#define PPLAY_IO_H

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

        enum class DeviceType {
            Local, Http, Ftp, Smb, TorrServe, WebDav
        };

        // NETWORKx_TYPE
        enum class NetworkType {
            Auto, TorrServe, Smb, Ftp, Http, WebDav
        };

        static NetworkType parseNetworkType(const std::string &name);

        static const std::vector<std::string> &networkTypeNames();

        void setConfig(PPLAYConfig *cfg) { config = cfg; }

        std::vector<Io::File> getDirList(const DeviceType &type, const std::vector<std::string> &extensions,
                                         const std::string &path, int timeout = 5, bool sort = false,
                                         bool showHidden = false);

        // listing without the Io state (lastError): usable from another thread
        std::vector<Io::File> list(const DeviceType &type, const std::string &path, int timeout,
                                   std::string *error, bool sort = false, bool showHidden = false);

        // type of the network the path belongs to, or from the url scheme
        DeviceType getDeviceType(const std::string &path);

        // root url of the network the path belongs to, or ""
        std::string getNetworkRoot(const std::string &path);

        const std::string &getLastError() const { return lastError; }

        // romfs -> data directory (new or changed files)
        void syncRomFs(const std::string &relativePath = "");

        bool readText(const std::string &path, std::string &out);

        bool writeText(const std::string &path, const std::string &text);

#ifdef __PS4__
        std::string getDataPath() override {
            return "/data/pplay/";
        }
#endif

#ifdef __PS5__
        std::string getDataPath() override {
            return "/data/homebrew/pplay/";
        }
#endif

#ifdef __SWITCH__
        std::string getDataPath() override {
            return "/config/pplay/";
        }
#endif

    private:

        // network index of a path, -1 if none
        int findNetwork(const std::string &path, bool typedOnly);

        PPLAYConfig *config = nullptr;
        std::string lastError;
    };
}

#endif //PPLAY_IO_H
