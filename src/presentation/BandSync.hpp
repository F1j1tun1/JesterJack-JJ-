#pragma once

#include <filesystem>
#include <istream>
#include <vector>

namespace presentation {

struct BandEnergy {
    float low = 0.0f, mid = 0.0f, high = 0.0f, hit = 0.0f; // 0..1, relative to the track's loud parts.
};

// Beat grid and loudness curves of one soundtrack (made offline by scripts/analyze_soundtrack.py).
// The on-stage band reads it to play in time with the song that is actually sounding.
class SoundtrackSync {
public:
    static SoundtrackSync parse(std::istream& input);
    static SoundtrackSync load(const std::filesystem::path& file); // Empty when missing or invalid.

    bool empty() const { return beats_.size() < 2; }
    double bpm() const { return bpm_; }
    double duration() const { return duration_; }

    // Beats since the track's first bar line at `seconds`: the integer part counts beats, 0 is a
    // downbeat and every fourth beat after it is one too. Before the first and after the last
    // detected beat the median tempo is extrapolated, and the value is continuous throughout.
    double beatAt(double seconds) const;
    BandEnergy energyAt(double seconds) const;

private:
    std::vector<double> beats_;
    std::vector<float> low_, mid_, high_, hit_;
    double bpm_ = 120.0;
    double duration_ = 0.0;
    double rate_ = 4.0;
    int downbeat_ = 0;
};

} // namespace presentation
