#include "presentation/EntranceTransition.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
int checks = 0, failures = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool near(float a, float b) { return std::abs(a - b) < .0001f; }

void choreography() {
    presentation::EntranceTransition entrance;
    check(!entrance.active() && !entrance.switched() && entrance.darkness() == 0 && entrance.menuAlpha() == 1,
          "Idle entrance is clear and leaves the menu visible");
    check(entrance.begin() && !entrance.begin(), "Duplicate starts are rejected");
    entrance.update(.25f);
    check(near(entrance.menuAlpha(), .5f) && entrance.approach() == 0,
          "Buttons fade before the camera moves");
    entrance.update(.25f);
    check(entrance.menuAlpha() == 0 && entrance.approach() > 0 && entrance.doorOpen() == 0,
          "Menu controls are gone during the approach");
    entrance.update(1.6f);
    check(entrance.approach() > .97f && entrance.doorOpen() > 0 && entrance.push() == 0,
          "Doors begin opening as the approach slows");
    entrance.update(1.1f);
    check(near(entrance.approach(), 1) && near(entrance.doorOpen(), 1) && entrance.push() > 0 && entrance.darkness() == 0,
          "The short inward push follows the opening doors");
    check(entrance.update(.8f) && entrance.switched() && entrance.active() && entrance.darkness() == 1,
          "Crossing the midpoint switches once in a fully opaque frame");
    check(!entrance.update(.1f) && entrance.darkness() < 1 && entrance.darkness() > 0,
          "The new scene fades in without a repeated switch");
    entrance.update(1);
    check(!entrance.active() && entrance.switched() && entrance.darkness() == 0,
          "The completed entrance restores gameplay visibility");
    check(!entrance.update(10) && entrance.begin() && !entrance.switched() && entrance.elapsed() == 0,
          "A completed cinematic can be replayed from its initial state");
}

void timingAndSkip() {
    for (float step : {1.0f / 30, 1.0f / 60, 1.0f / 144, .017f, .071f}) {
        presentation::EntranceTransition entrance;
        entrance.begin();
        int switches = 0, frames = 0;
        float time = 0;
        while (entrance.active() && frames++ < 2000) {
            time += step;
            if (entrance.update(step)) {
                ++switches;
                check(entrance.darkness() == 1, "Every cadence changes scenes only in black");
            }
        }
        check(switches == 1 && !entrance.active(), "Each cadence completes with exactly one scene switch");
        check(time >= 4.599f && time <= 4.6f + step + .001f,
              "Cinematic duration is independent of frame rate");
    }
    presentation::EntranceTransition single, split;
    single.begin(); split.begin();
    single.update(2.5f);
    for (int i = 0; i < 100; ++i) split.update(.025f);
    check(near(single.approach(), split.approach()) && near(single.doorOpen(), split.doorOpen())
          && near(single.darkness(), split.darkness()), "Equal elapsed time gives equal camera and door poses");
    const float elapsed = single.elapsed();
    single.update(-1); single.update(std::numeric_limits<float>::infinity());
    single.update(std::numeric_limits<float>::quiet_NaN());
    check(single.elapsed() == elapsed && !single.switched(), "Invalid time cannot corrupt or advance the cinematic");
    single.skip();
    check(single.update(0) && single.darkness() == 1 && near(single.elapsed(), 3.85f),
          "Skip queues a single opaque scene change even on a paused frame");
    single.skip();
    check(!single.update(.1f), "Skip after the scene switch cannot restart or repeat it");
    single.reset();
    check(!single.active() && !single.switched() && single.menuAlpha() == 1 && single.approach() == 0,
          "Reset cancels a partial entrance and all camera progress");
    single.begin();
    check(single.update(100) && single.darkness() == 1, "A stalled frame still presents the switch under black");
    check(!single.update(0) && !single.active(), "Excess frame time is retained after the protected switch");
}

void reducedMotion() {
    presentation::EntranceTransition entrance;
    entrance.begin(true);
    entrance.update(.15f);
    check(near(entrance.darkness(), .5f) && near(entrance.menuAlpha(), .5f),
          "Reduced motion uses a short crossfade");
    check(entrance.approach() == 0 && entrance.doorOpen() == 0 && entrance.push() == 0,
          "Reduced motion does not translate the camera or animate doors");
    check(entrance.update(.15f) && entrance.darkness() == 1,
          "Reduced motion also changes scenes in black");
    entrance.update(.15f);
    check(near(entrance.darkness(), .5f), "Reduced motion fades the gameplay back in");
    entrance.update(.15f);
    check(!entrance.active() && entrance.darkness() == 0, "Reduced motion completes in six tenths of a second");
}
} // namespace

int main() {
    choreography(); timingAndSkip(); reducedMotion();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
