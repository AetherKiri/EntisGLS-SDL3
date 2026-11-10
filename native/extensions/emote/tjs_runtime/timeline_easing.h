#pragma once

namespace studysteady::motion {
// PSB timeline decoding already converts authored easing to a positive power.
// The public SetVariable route accepts the original signed easing instead.
// Undo that conversion at this boundary, otherwise a linear track becomes
// quadratic and pose changes accelerate abruptly near their final frame.
inline double TimelineEasingArgument(double power) {
    return power >= 1.0 ? power - 1.0 : 1.0 - 1.0 / power;
}
}
