//
// Path helpers (local paths and urls).
//

#include <algorithm>
#include <vector>

#include "cross2d/c2d.h"
#include "paths.h"

namespace pplay::Paths {

static size_t findFirstPathSeparator(const std::string &str, size_t from) {
    size_t slash = str.find('/', from);
    size_t backslash = str.find('\\', from);
    if (slash == std::string::npos) return backslash;
    if (backslash == std::string::npos) return slash;
    return std::min(slash, backslash);
}

std::string normalizeSmbPathSeparators(const std::string &path) {
    if (!c2d::Utility::startWith(path, "smb://")) {
        return path;
    }

    size_t schemeEnd = std::string("smb://").size();
    size_t at = path.find_last_of('@');
    size_t hostStart = at != std::string::npos && at >= schemeEnd ? at + 1 : schemeEnd;
    size_t pathStart = findFirstPathSeparator(path, hostStart);
    if (pathStart == std::string::npos) {
        return path;
    }

    std::string normalized = path.substr(0, pathStart);
    std::string rest = path.substr(pathStart);
    std::replace(rest.begin(), rest.end(), '\\', '/');
    return normalized + rest;
}

std::string ensureTrailingSlash(const std::string &url) {
    std::string normalized = normalizeSmbPathSeparators(url);
    if (normalized.empty() || c2d::Utility::endsWith(normalized, "/")) {
        return normalized;
    }
    return normalized + "/";
}

std::string normalizePath(const std::string &path) {
    if (path.empty()) return path;
    std::string normalizedInput = normalizeSmbPathSeparators(path);
    size_t schemePos = normalizedInput.find("://");
    std::string prefix;
    std::string rest = normalizedInput;
    if (schemePos != std::string::npos) {
        size_t firstSlash = normalizedInput.find('/', schemePos + 3);
        if (firstSlash == std::string::npos) {
            return normalizedInput + "/";
        }
        prefix = normalizedInput.substr(0, firstSlash);
        rest = normalizedInput.substr(firstSlash);
    }
    bool endsWithDotDot = (rest == "..") ||
        (rest.size() >= 3 && rest.compare(rest.size() - 3, 3, "/..") == 0);
    if (endsWithDotDot) {
        if (rest == "..") {
            rest = "";
        } else {
            rest = rest.substr(0, rest.size() - 3); // strip "/.."
        }
    }
    bool trailingSlash = !rest.empty() && rest.back() == '/';
    std::vector<std::string> segments;
    size_t start = 0, end = 0;
    while ((end = rest.find('/', start)) != std::string::npos) {
        std::string part = rest.substr(start, end - start);
        if (!part.empty() && part != ".") {
            if (part == "..") {
                if (!segments.empty()) segments.pop_back();
            } else {
                segments.push_back(part);
            }
        }
        start = end + 1;
    }
    std::string lastPart = rest.substr(start);
    if (!lastPart.empty() && lastPart != ".") {
        if (lastPart == "..") {
            if (!segments.empty()) segments.pop_back();
        } else {
            segments.push_back(lastPart);
        }
    }
    std::string normalized = prefix + "/";
    for (size_t i = 0; i < segments.size(); ++i) {
        normalized += segments[i];
        if (i < segments.size() - 1) normalized += "/";
    }
    if (trailingSlash && !segments.empty() && normalized.back() != '/') {
        normalized += "/";
    }
    if (endsWithDotDot) {
        if (normalized.back() != '/') {
            normalized += "/";
        }
        normalized += "..";
    }
    return normalized;
}

std::string extractDirPath(const std::string &path) {
    if (path.empty()) return "/";
    if (path.back() == '/') {
        return path;
    }
    std::string p = normalizePath(path);
    size_t schemePos = p.find("://");
    if (schemePos != std::string::npos) {
        size_t afterHost = p.find('/', schemePos + 3);
        if (afterHost == std::string::npos) return ensureTrailingSlash(p);
    }
    size_t pos = p.find_last_of('/');
    if (pos == 0) return "/";
    return p.substr(0, pos + 1);
}

std::string getLeafName(const std::string &path) {
    if (path.empty() || path == "/") return "";
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    if (pos + 1 >= path.size()) return "";
    return path.substr(pos + 1);
}
}
