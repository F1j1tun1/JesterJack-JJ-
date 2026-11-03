#include "audio/Mixer.hpp"

#include <algorithm>
#include <system_error>

namespace audio {
namespace {
constexpr const char* musicAlias = "jjmusic";
// A freshly started MCI track may briefly look "stopped"; never treat that as its end.
constexpr double startGrace = 1.0;
constexpr int maxMusicFailures = 3;

constexpr const char* carAlias = "jjcar";
constexpr const char* carFile = "ambient/car_pass.mp3";

constexpr const char* sfxFiles[] = {
    "sfx/card_deal.mp3", "sfx/card_roll.mp3",
    "sfx/chips_start.mp3", "sfx/chips_bet.mp3", "sfx/chips_win.mp3",
    "sfx/tada_minor.mp3", "sfx/tada_major.mp3", "sfx/you_lose.mp3",
    "ui/ui_open.mp3", "ui/ui_close.mp3", "ui/ui_selection.mp3", "ui/ui_error.mp3",
};
static_assert(sizeof(sfxFiles) / sizeof(sfxFiles[0]) == sfxCount, "Every Sfx needs a file");

} // namespace

std::vector<std::filesystem::path> discoverSoundtracks(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> found;
    std::error_code error;
    for (std::filesystem::directory_iterator it(root / "music", error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (it->is_regular_file(error) && name.rfind("soundtrack_", 0) == 0 && it->path().extension() == ".mp3")
            found.push_back(it->path());
    }
    std::sort(found.begin(), found.end());
    return found;
}

namespace {
bool isUi(Sfx sound) { return static_cast<std::size_t>(sound) >= static_cast<std::size_t>(Sfx::UiOpen); }
} // namespace

const char* sfxFile(Sfx sound) { return sfxFiles[static_cast<std::size_t>(sound)]; }

Mixer::Mixer(Backend& backend, std::filesystem::path root, std::uint32_t seed, Levels levels)
    : backend_(backend), root_(std::move(root)), levels_(levels),
      soundtracks_(discoverSoundtracks(root_)), playlist_(soundtracks_.size(), seed) {
    for (std::size_t sound = 0; sound < sfxCount; ++sound) {
        const auto file = root_ / sfxFiles[sound];
        std::error_code error;
        if (!std::filesystem::exists(file, error)) continue;
        const float volume = isUi(static_cast<Sfx>(sound)) ? levels_.ui : levels_.sfx;
        for (std::size_t voice = 0; voice < voicesPerSound; ++voice) {
            auto& slot = voices_[sound][voice];
            slot.alias = "jjs" + std::to_string(sound) + "v" + std::to_string(voice);
            slot.ready = backend_.open(slot.alias, file);
            if (slot.ready) backend_.setVolume(slot.alias, volume);
        }
    }
    std::error_code error;
    if (std::filesystem::exists(root_ / carFile, error)) {
        carReady_ = backend_.open(carAlias, root_ / carFile);
        if (carReady_) backend_.setVolume(carAlias, levels_.car);
    }
}

Mixer::~Mixer() {
    stopMusic();
    if (carReady_) {
        backend_.stop(carAlias);
        backend_.close(carAlias);
    }
    for (auto& voices : voices_)
        for (auto& voice : voices)
            if (voice.ready) {
                backend_.stop(voice.alias);
                backend_.close(voice.alias);
            }
}

void Mixer::play(Sfx sound) {
    const auto index = static_cast<std::size_t>(sound);
    if (index >= sfxCount) return;
    auto& voices = voices_[index];
    // Round-robin, preferring a voice that is ready; a repeat then restarts the older one.
    for (std::size_t attempt = 0; attempt < voicesPerSound; ++attempt) {
        auto& voice = voices[nextVoice_[index]];
        nextVoice_[index] = (nextVoice_[index] + 1) % voicesPerSound;
        if (!voice.ready) continue;
        backend_.play(voice.alias, true);
        return;
    }
}

void Mixer::setMode(MusicMode mode) {
    if (mode == mode_) return;
    stopMusic();
    mode_ = mode;
    failures_ = 0;
    startedAt_ = now_;
    if (mode_ == MusicMode::Menu) startMenuTheme();
    else if (mode_ == MusicMode::Game) startNextSoundtrack();
}

void Mixer::setDucked(bool ducked) {
    if (ducked == ducked_) return;
    ducked_ = ducked;
    if (musicOpen_) backend_.setVolume(musicAlias, musicVolume());
}

void Mixer::startCarPass(double offsetSeconds) {
    if (!carReady_) return;
    carFading_ = false;
    backend_.stop(carAlias);
    backend_.setVolume(carAlias, levels_.car);
    backend_.playFrom(carAlias, std::max(0.0, offsetSeconds));
    carPlaying_ = true;
}

void Mixer::stopCarPass(double fadeSeconds) {
    if (!carReady_ || !carPlaying_) return;
    if (fadeSeconds <= 0.0) {
        backend_.stop(carAlias);
        carPlaying_ = carFading_ = false;
        return;
    }
    if (carFading_) return; // Already fading out.
    carFading_ = true;
    carFadeStart_ = now_;
    carFadeLength_ = fadeSeconds;
}

void Mixer::tick(double now) {
    now_ = now;
    if (carFading_) {
        const double gain = 1.0 - (now_ - carFadeStart_) / carFadeLength_;
        if (gain <= 0.0) {
            backend_.stop(carAlias);
            carPlaying_ = carFading_ = false;
        } else {
            backend_.setVolume(carAlias, levels_.car * static_cast<float>(gain));
        }
    }
    status_ = MusicStatus{};
    if (mode_ == MusicMode::Silent || failures_ >= maxMusicFailures) return;
    if (musicOpen_ && (now_ - startedAt_ < startGrace || backend_.playing(musicAlias))) {
        if (mode_ == MusicMode::Game) {
            const double position = backend_.position(musicAlias);
            status_ = {currentIndex_, true, position >= 0.0 ? position : now_ - startedAt_, now_};
        }
        return;
    }
    // The track ended (or never started): repeat the main theme or move on to the next soundtrack.
    if (mode_ == MusicMode::Menu) startMenuTheme();
    else startNextSoundtrack();
}

float Mixer::musicVolume() const { return levels_.music * (ducked_ ? levels_.duck : 1.0f); }

bool Mixer::startMusic(const std::filesystem::path& file) {
    stopMusic();
    std::error_code error;
    if (!std::filesystem::exists(file, error) || !backend_.open(musicAlias, file)) {
        ++failures_;
        return false;
    }
    backend_.setVolume(musicAlias, musicVolume());
    backend_.play(musicAlias, true);
    musicOpen_ = true;
    currentTrack_ = file.filename().string();
    currentIndex_ = -1;
    startedAt_ = now_;
    failures_ = 0;
    return true;
}

void Mixer::startMenuTheme() { startMusic(root_ / "music/main_theme.mp3"); }

void Mixer::startNextSoundtrack() {
    if (soundtracks_.empty()) {
        failures_ = maxMusicFailures; // Nothing to play; stop polling.
        return;
    }
    // A damaged file is skipped; a whole failed cycle gives up until the mode changes.
    for (std::size_t attempt = 0; attempt < soundtracks_.size(); ++attempt) {
        const std::size_t index = playlist_.next();
        if (startMusic(soundtracks_[index])) {
            currentIndex_ = static_cast<int>(index);
            return;
        }
    }
    failures_ = maxMusicFailures;
}

void Mixer::stopMusic() {
    if (!musicOpen_) return;
    backend_.stop(musicAlias);
    backend_.close(musicAlias);
    musicOpen_ = false;
    currentTrack_.clear();
    currentIndex_ = -1;
}

} // namespace audio
