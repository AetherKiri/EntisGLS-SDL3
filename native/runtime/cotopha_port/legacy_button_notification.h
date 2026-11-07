#pragma once
#include <sakuraglx/sakuraglx.h>
#include <sakuraglx/sprite/sglx_sprite_button.h>

// Mark the boundary to a GLS3 script without making the standalone SDK depend
// on the legacy interpreter or maintaining a second sprite ownership registry.
class LegacyButtonCommandTarget {
public:
    virtual ~LegacyButtonCommandTarget() = default;
};

// Called only at the SDK button's status-notification emission point. Other
// command codes and SDK consumers must retain their original numbering.
inline int64_t TranslateLegacyButtonStatusNotification(const SakuraGL::SGLSprite* parent,
                                                      int64_t status) {
    for (; parent; parent = parent->GetParent()) {
        if (!dynamic_cast<const LegacyButtonCommandTarget*>(parent)) continue;
        // GLS3 EButtonSprite::ButtonStatus: disabled=4, push-disabled=5,
        // active-pushed=6. GLS4 has two active states and disabled=6/7.
        using Button = SakuraGL::SGLSpriteButton;
        switch (status) {
        case Button::statusActive:
        case Button::statusPushedActive: return 6;
        case Button::statusDisabled: return 4;
        case Button::statusPushDisabled: return 5;
        default: return status;
        }
    }
    return status;
}
