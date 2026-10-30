//
// Pictures cache (posters, covers): data/cache/posters/<md5 of the picture>, an index of
// the urls (a picture shared by many urls is stored once).
//

#ifndef PPLAY_POSTER_CACHE_H
#define PPLAY_POSTER_CACHE_H

#include <string>

namespace pplay::PosterCache {

    // the picture file of an url, "" if none
    std::string find(const std::string &url);

    bool store(const std::string &url, const std::string &data, std::string *error);

    // a picture that can't be decoded
    void forget(const std::string &url);

    // the pictures and media information not used for some days (start)
    void cleanup(int days);

    // all of it, the files count
    int clear();
}

#endif //PPLAY_POSTER_CACHE_H
