//
// TorrServe virtual filesystem helpers.
//

#ifndef PPLAY_TORRSERVE_H
#define PPLAY_TORRSERVE_H

#include <string>
#include <vector>
#include "cross2d/c2d.h"

class Player; 
class Browser; 

namespace pplay::TorrServe {

std::vector<c2d::Io::File> getDirList(Browser *browser, const std::string &path, int timeout);
std::string toStreamUrl(const std::string &path);
bool isFileViewed(const std::string &path);
bool remFileViewed(const std::string &path);

inline bool forceViewedRefresh = false;
}

#endif // PPLAY_TORRSERVE_H
