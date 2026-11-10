#pragma once
#include "EmoteEngine.h"

namespace studysteady::motion {
inline void PlaySdkTimeline(::motion::EmoteEngine& engine, const ttstr& label, uint32_t flags) {
    // SDK bit 0 means parallel; the reconstructed engine's bit 0 means
    // replace. Preserve the differential/controller bits when crossing the
    // boundary. This also replaces an old idle loop when DefaultTimeline
    // starts another one, instead of accumulating both loops.
    engine.playTimeline_guess(label, flags ^ 1u);
}
}
