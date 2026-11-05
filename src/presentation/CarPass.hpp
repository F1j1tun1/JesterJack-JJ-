#pragma once

#include <algorithm>
#include <cmath>

namespace presentation {
// Shared by the street renderer and sound scheduling. The recorded pass peaks at 4.94 s.
struct CarPass {
    static constexpr double cycleSeconds = 32.0;
    static constexpr double visibleSeconds = 9.4;
    static constexpr double recordingPeak = 4.94;
    static constexpr double recordingLead = recordingPeak - visibleSeconds * .5;
    static double phase(double time) { return std::fmod(std::max(0.0, time), cycleSeconds); }
    static bool visible(double time) { return phase(time) < visibleSeconds; }
    static double distance(double time) { return -18.0 + phase(time) / visibleSeconds * 36.0; }
    static double recordingOffset(double time) {
        return phase(time) + recordingLead;
    }
};
} // namespace presentation
