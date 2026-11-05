#pragma once

#include "audio/Playlist.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace audio {

// Every one-shot sound of the game. Files live in assets/audio/<sfxFile()>.
enum class Sfx {
    CardDeal, CardRoll,
    ChipsStart, ChipsBet, ChipsWin,
    TadaMinor, TadaMajor, YouLose,
    UiOpen, UiClose, UiSelection, UiError,
    Count
};
inline constexpr std::size_t sfxCount = static_cast<std::size_t>(Sfx::Count);
const char* sfxFile(Sfx sound);

// Silent: nothing plays. Menu: main theme on repeat. Game: shuffled soundtracks, endlessly.
enum class MusicMode { Silent, Menu, Game };

struct Levels {
    float sfx = 1.0f;
    float ui = 0.8f;
    float music = 0.5f;
    float car = 0.9f;
    float duck = 0.35f; // Music multiplier while a fanfare or the game-over screen needs room.
};

// Platform-independent player primitives. The Windows implementation uses MCI;
// tests use a recording fake. Aliases are short ASCII names chosen by Mixer.
class Backend {
public:
    virtual ~Backend() = default;
    virtual bool open(const std::string& alias, const std::filesystem::path& file) = 0;
    virtual void play(const std::string& alias, bool fromStart) = 0;
    virtual void stop(const std::string& alias) = 0;
    virtual void close(const std::string& alias) = 0;
    virtual void setVolume(const std::string& alias, float volume) = 0; // 0..1
    virtual bool playing(const std::string& alias) = 0;
    // Starts playback `seconds` into the file.
    virtual void playFrom(const std::string& alias, double seconds) = 0;
    // Current position in seconds, or a negative value when the device cannot tell.
    virtual double position(const std::string&) { return -1.0; }
};

// Soundtrack files in play order of the shuffle list: assets/audio/music/soundtrack_*.mp3, sorted.
std::vector<std::filesystem::path> discoverSoundtracks(const std::filesystem::path& root);

// What the music layer is doing, for animation that follows the song.
struct MusicStatus {
    int track = -1;          // Index into discoverSoundtracks(); -1 for the menu theme or silence.
    bool playing = false;
    double position = 0.0;   // Seconds into the track when sampled.
    double sampledAt = 0.0;  // Mixer clock (tick time) of that sample.
};

// Decides what plays when. Single-threaded: call every method from one thread.
class Mixer {
public:
    static constexpr std::size_t voicesPerSound = 2; // Quick repeats may overlap instead of cutting off.

    Mixer(Backend& backend, std::filesystem::path root, std::uint32_t seed, Levels levels = {});
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    void play(Sfx sound);
    void setMode(MusicMode mode);
    void setDucked(bool ducked);
    // Independent 0..1 master gains on top of Levels. Non-finite values keep that channel unchanged.
    // Applies to current and future playback without restarting the music or any effect.
    void setVolumes(float music, float effects);
    // The street ambience: starts the recorded car pass `offsetSeconds` into the file (restarting it
    // if it is already playing) and fades it out over `fadeSeconds` when stopped.
    void startCarPass(double offsetSeconds);
    void stopCarPass(double fadeSeconds);
    // Detects the end of a track and starts the next one. `now` is monotonic seconds.
    void tick(double now);

    MusicMode mode() const { return mode_; }
    std::size_t soundtrackCount() const { return soundtracks_.size(); }
    // File name of the track currently opened for music; empty when none.
    std::string currentTrack() const { return musicOpen_ ? currentTrack_ : std::string(); }
    MusicStatus musicStatus() const { return status_; }
    bool carPassPlaying() const { return carPlaying_; }

private:
    struct Voice {
        std::string alias;
        bool ready = false;
    };

    Backend& backend_;
    std::filesystem::path root_;
    Levels levels_;
    float musicGain_ = 1.0f;
    float effectsGain_ = 1.0f;
    std::vector<std::filesystem::path> soundtracks_;
    ShufflePlaylist playlist_;
    std::array<std::array<Voice, voicesPerSound>, sfxCount> voices_;
    std::array<std::size_t, sfxCount> nextVoice_{};
    MusicMode mode_ = MusicMode::Silent;
    bool musicOpen_ = false;
    bool ducked_ = false;
    std::string currentTrack_;
    int currentIndex_ = -1;
    MusicStatus status_;
    bool carReady_ = false;
    bool carPlaying_ = false;
    bool carFading_ = false;
    double carFadeStart_ = 0.0, carFadeLength_ = 0.0;
    double carStartedAt_ = 0.0;
    double now_ = 0.0;
    double startedAt_ = 0.0;
    int failures_ = 0;

    float musicVolume() const;
    float effectVolume(Sfx sound) const;
    float carVolume() const;
    bool startMusic(const std::filesystem::path& file);
    void startMenuTheme();
    void startNextSoundtrack();
    void stopMusic();
};

} // namespace audio
