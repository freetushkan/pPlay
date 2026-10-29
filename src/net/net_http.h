//
// libcurl based HTTP helpers.
//

#ifndef PPLAY_NET_HTTP_H
#define PPLAY_NET_HTTP_H

#include <ctime>
#include <string>
#include <vector>
#include "cross2d/c2d.h"

namespace pplay::Http {

    struct Response {
        long status = 0; // HTTP status code, 0 on transport error
        std::string body;
        std::string error;        // transport error message
        std::string effectiveUrl; // after redirects
        time_t date = 0;          // "Date" header (server clock), 0 if none

        bool ok() const { return error.empty() && status >= 200 && status < 300; }
    };

    struct Request {
        std::string url;
        std::string method = "GET";
        std::vector<std::string> headers;
        std::string body;
        int timeout = 15;
        size_t maxSize = 16 * 1024 * 1024;
    };

    // call once (not thread safe)
    void init();

    Response perform(const Request &request);

    Response get(const std::string &url, int timeout);

    Response postJson(const std::string &url, const std::string &json, int timeout);

    // escape the characters not allowed in an url (spaces, utf-8...), keeps %XX sequences
    std::string normalize(const std::string &url);

    // percent-encode a path segment / decode %XX sequences
    std::string escape(const std::string &value);

    std::string unescape(const std::string &value);

    // "scheme://[user[:pass]@]host[:port]" part of an url (no trailing slash)
    std::string origin(const std::string &url);

    // resolve a link found in a page (absolute, root relative or relative)
    std::string resolve(const std::string &base, const std::string &href);

    // apache/nginx/lighttpd... autoindex pages
    std::vector<c2d::Io::File> listIndex(const std::string &url, int timeout, std::string *error = nullptr);

    // WebDAV (PROPFIND, Depth: 1)
    std::vector<c2d::Io::File> listWebDav(const std::string &url, int timeout, std::string *error = nullptr);
}

#endif //PPLAY_NET_HTTP_H
