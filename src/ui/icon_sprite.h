//
// A skin icon made at screen pixels, rebuilt after a stable resize.
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

        // Rebuild only after the interface scale has stayed stable for one second.
        void onUpdate() override;

    private:

        bool make();

        std::string name;
        float height = 0;
        bool smooth = false;
        float madeScale = 0;
        float pendingScale = 0;
        bool scalePending = false;
        bool textureDirty = true;
        c2d::C2DClock scaleClock;
    };
}

#endif //PPLAY_ICON_SPRITE_H
