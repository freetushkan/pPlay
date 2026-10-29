//
// Shared skin images.
//

#include <map>

#include "skin.h"
#include "utility.h"

c2d::Texture *pplay::Skin::texture(const std::string &name) {
    static std::map<std::string, c2d::Texture *> textures;
    auto it = textures.find(name);
    if (it != textures.end()) {
        return it->second;
    }
    std::string path = Utility::getDataPath() + "skin/" + name;
    c2d::Texture *texture = Utility::fileExists(path) ? new c2d::C2DTexture(path) : nullptr;
    textures[name] = texture;
    return texture;
}
