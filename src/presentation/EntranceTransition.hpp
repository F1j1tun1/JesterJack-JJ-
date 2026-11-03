#pragma once

#include <algorithm>
#include <cmath>

namespace presentation {

// Pure cinematic clock. The caller alone restarts the game when update returns
// true; a guaranteed opaque frame hides that single scene change at any cadence.
class EntranceTransition {
public:
    bool begin(bool reducedMotion = false) {
        if (active_) return false;
        reset();
        active_ = true;
        reducedMotion_ = reducedMotion;
        return true;
    }

    // Skip is queued so the scene switch still happens on an opaque update.
    void skip() {
        if (active_ && !switched_) skipRequested_ = true;
    }

    bool update(float dt) {
        if (!active_ || !std::isfinite(dt) || dt < 0.0f) return false;
        if (skipRequested_) {
            elapsed_ = switchTime();
            carriedTime_ = 0.0;
            skipRequested_ = false;
            switched_ = true;
            return true;
        }
        const double next = elapsed_ + carriedTime_ + static_cast<double>(dt);
        carriedTime_ = 0.0;
        if (!switched_ && next >= switchTime()) {
            // Preserve excess frame time, but render the actual switch in black.
            carriedTime_ = next - switchTime();
            elapsed_ = switchTime();
            switched_ = true;
            return true;
        }
        elapsed_ = std::min(next, duration());
        if (elapsed_ >= duration()) active_ = false;
        return false;
    }

    void reset() {
        elapsed_ = carriedTime_ = 0.0;
        active_ = switched_ = reducedMotion_ = skipRequested_ = false;
    }

    bool active() const { return active_; }
    bool switched() const { return switched_; }
    float elapsed() const { return static_cast<float>(elapsed_); }
    float menuAlpha() const {
        return switched_ ? 0.0f : 1.0f - smooth(elapsed_, 0.0, reducedMotion_ ? .3 : .5);
    }
    float darkness() const {
        if (!active_) return 0.0f;
        return switched_ ? 1.0f - smooth(elapsed_, switchTime(), duration())
            : smooth(elapsed_, reducedMotion_ ? 0.0 : 3.3, switchTime());
    }
    float approach() const {
        if (reducedMotion_) return 0.0f;
        const float t = segment(elapsed_, .3, 2.8);
        return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    }
    float doorOpen() const { return reducedMotion_ ? 0.0f : smooth(elapsed_, 1.9, 3.2); }
    float push() const { return reducedMotion_ ? 0.0f : smooth(elapsed_, 3.0, 3.7); }

private:
    double elapsed_ = 0.0;
    double carriedTime_ = 0.0;
    bool active_ = false;
    bool switched_ = false;
    bool reducedMotion_ = false;
    bool skipRequested_ = false;

    double switchTime() const { return reducedMotion_ ? .3 : 3.85; }
    double duration() const { return reducedMotion_ ? .6 : 4.6; }
    static float segment(double value, double begin, double end) {
        return static_cast<float>(std::clamp((value - begin) / (end - begin), 0.0, 1.0));
    }
    static float smooth(double value, double begin, double end) {
        const float t = segment(value, begin, end);
        return t * t * (3.0f - 2.0f * t);
    }
};

} // namespace presentation
