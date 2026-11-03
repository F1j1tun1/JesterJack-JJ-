#pragma once

#include "audio/Mixer.hpp"

#include <filesystem>
#include <memory>

namespace audio {

// Fire-and-forget sound output. All playback runs on a private thread, so
// opening an MP3 or waiting on the audio device can never stall rendering.
// Windows plays through the built-in MCI (winmm) MP3 decoder; other platforms
// and disabled/headless runs are silent no-ops with the same interface.
class Engine {
public:
    Engine(const std::filesystem::path& audioRoot, bool enabled);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void play(Sfx sound);
    void setMusic(MusicMode mode);
    void setDucked(bool ducked);
    // Street ambience for the main menu: start the car-pass recording `offsetSeconds` into the file
    // (the caller aligns this with the car's position), or fade it out.
    void carPass(double offsetSeconds);
    void carStop(double fadeSeconds);
    // Where the game soundtrack is right now (extrapolated between device samples).
    struct MusicClock {
        int track = -1;       // Index in discoverSoundtracks(); -1 when no soundtrack is playing.
        double position = 0;  // Seconds into that track.
    };
    MusicClock musicClock() const;
    bool active() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace audio
