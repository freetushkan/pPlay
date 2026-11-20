//
// A skin icon made at the screen pixels: sharp at any interface scale.
//

#ifndef PPLAY_ICON_SPRITE_H
#define PPLAY_ICON_SPRITE_H

#include <string>
#include "cross2d/c2d.h"

namespace pplay {

    class IconSprite : public c2d::Sprite {

    public:

        // that icon, height high (interface units); false if there is none
        bool setIcon(const std::string &name, float height, bool smooth = false);

        const std::string &getIcon() const {
            return name;
        }

        // made again when the interface scale changed
        void onDraw(c2d::Transform &transform, bool draw = true) override;

    private:

        bool make();

        std::string name;
        float height = 0;
        bool smooth = false;
        float madeScale = 0;
    };
}

#endif //PPLAY_ICON_SPRITE_H
