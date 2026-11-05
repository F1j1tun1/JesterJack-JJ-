#include "audio/Mixer.hpp"
#include "audio/Playlist.hpp"
#include "audio/Engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

// Records every call; a track "plays" until the test finishes it.
class FakeBackend final : public audio::Backend {
public:
    std::set<std::string> open_;
    std::map<std::string, std::filesystem::path> files_;
    std::map<std::string, bool> playing_;
    std::map<std::string, float> volume_;
    std::vector<std::string> started; // File names, in start order.
    std::map<std::string, int> plays;  // Per file name.
    std::set<std::string> refuse;      // File names that fail to open.
    int openAttempts = 0;
    int volumeWrites = 0;
    std::vector<std::pair<std::string, double>> seeks; // playFrom calls: file name, seconds.
    double reportedPosition = -1.0;

    bool open(const std::string& alias, const std::filesystem::path& file) override {
        ++openAttempts;
        if (refuse.count(file.filename().string())) return false;
        open_.insert(alias);
        files_[alias] = file;
        return true;
    }
    void play(const std::string& alias, bool) override {
        playing_[alias] = true;
        const auto name = files_[alias].filename().string();
        started.push_back(name);
        ++plays[name];
    }
    void stop(const std::string& alias) override { playing_[alias] = false; }
    void close(const std::string& alias) override { open_.erase(alias); playing_[alias] = false; }
    void setVolume(const std::string& alias, float volume) override { ++volumeWrites; volume_[alias] = volume; }
    bool playing(const std::string& alias) override { return open_.count(alias) && playing_[alias]; }
    void playFrom(const std::string& alias, double seconds) override {
        play(alias, false);
        seeks.emplace_back(files_[alias].filename().string(), seconds);
    }
    double position(const std::string&) override { return reportedPosition; }

    void finishMusic() { playing_["jjmusic"] = false; }
    int musicStarts() const {
        int count = 0;
        for (const auto& name : started)
            if (name == "main_theme.mp3" || name.rfind("soundtrack_", 0) == 0) ++count;
        return count;
    }
};

std::filesystem::path makeAudioFolder(int soundtracks) {
    static std::size_t serial = 0;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("jj_audio_test_" + std::to_string(nonce) + "_" + std::to_string(++serial));
    std::filesystem::create_directories(root / "music");
    std::filesystem::create_directories(root / "sfx");
    std::filesystem::create_directories(root / "ui");
    std::filesystem::create_directories(root / "ambient");
    const auto touch = [](const std::filesystem::path& file) { std::ofstream(file).put('x'); };
    touch(root / "music/main_theme.mp3");
    touch(root / "ambient/car_pass.mp3");
    for (int i = 1; i <= soundtracks; ++i) touch(root / "music" / ("soundtrack_" + std::to_string(i) + ".mp3"));
    for (std::size_t i = 0; i < audio::sfxCount; ++i) touch(root / audio::sfxFile(static_cast<audio::Sfx>(i)));
    return root;
}

void playlistNeverRepeatsAndCoversEveryTrack() {
    for (std::uint32_t seed = 0; seed < 50; ++seed) {
        audio::ShufflePlaylist list(5, seed);
        std::size_t previous = 99;
        bool ok = true, covered = true;
        for (int cycle = 0; cycle < 20; ++cycle) {
            std::set<std::size_t> seen;
            for (int i = 0; i < 5; ++i) {
                const auto track = list.next();
                ok = ok && track < 5 && track != previous;
                seen.insert(track);
                previous = track;
            }
            covered = covered && seen.size() == 5;
        }
        check(ok, "Playlist never repeats a track back to back, including across cycles");
        check(covered, "Every cycle plays each soundtrack exactly once");
    }
    audio::ShufflePlaylist single(1, 7);
    check(single.next() == 0 && single.next() == 0, "A one-track playlist simply repeats it");
    audio::ShufflePlaylist empty(0, 7);
    check(empty.next() == 0 && empty.size() == 0, "An empty playlist is harmless");
    audio::ShufflePlaylist a(5, 123), b(5, 123);
    bool same = true;
    for (int i = 0; i < 12; ++i) same = same && a.next() == b.next();
    check(same, "The same seed gives the same order");
}

void menuThemeLoopsUntilGameStarts() {
    const auto root = makeAudioFolder(5);
    FakeBackend backend;
    audio::Mixer mixer(backend, root, 1);
    check(mixer.soundtrackCount() == 5, "All soundtrack_*.mp3 files are discovered");
    mixer.setMode(audio::MusicMode::Menu);
    check(mixer.currentTrack() == "main_theme.mp3" && backend.musicStarts() == 1, "Menu mode starts the main theme");
    double now = 0;
    for (int loop = 0; loop < 4; ++loop) {
        for (int i = 0; i < 20; ++i) mixer.tick(now += 0.1); // Still playing: nothing restarts.
        backend.finishMusic();
        mixer.tick(now += 0.1);
    }
    check(backend.plays["main_theme.mp3"] == 5, "The main theme repeats every time it ends");
    check(backend.musicStarts() == 5, "Only the main theme plays in the menu");
    mixer.setMode(audio::MusicMode::Game);
    check(!backend.playing("jjmusic") || mixer.currentTrack().rfind("soundtrack_", 0) == 0,
          "Leaving the menu stops the main theme");
    const int themeAtStart = backend.plays["main_theme.mp3"];
    backend.finishMusic();
    for (int i = 0; i < 50; ++i) mixer.tick(now += 0.1);
    check(backend.plays["main_theme.mp3"] == themeAtStart, "The main theme never returns during the game");
    std::filesystem::remove_all(root);
}

void soundtracksShuffleForever() {
    const auto root = makeAudioFolder(5);
    FakeBackend backend;
    audio::Mixer mixer(backend, root, 42);
    mixer.setMode(audio::MusicMode::Game);
    double now = 0;
    for (int track = 0; track < 60; ++track) {
        for (int i = 0; i < 15; ++i) mixer.tick(now += 0.1);
        backend.finishMusic();
        mixer.tick(now += 0.1);
    }
    check(backend.musicStarts() == 61, "A new soundtrack starts every time one ends, with no gaps");
    bool noRepeat = true, onlySoundtracks = true;
    for (std::size_t i = 0; i < backend.started.size(); ++i) {
        onlySoundtracks = onlySoundtracks && backend.started[i].rfind("soundtrack_", 0) == 0;
        if (i > 0) noRepeat = noRepeat && backend.started[i] != backend.started[i - 1];
    }
    check(onlySoundtracks, "Game mode plays soundtracks only");
    check(noRepeat, "The same soundtrack never plays twice in a row");
    int least = 1000, most = 0;
    for (int i = 1; i <= 5; ++i) {
        const int count = backend.plays["soundtrack_" + std::to_string(i) + ".mp3"];
        least = std::min(least, count);
        most = std::max(most, count);
    }
    check(least >= 11 && most <= 13, "Every soundtrack gets an equal share over many cycles");
    mixer.setMode(audio::MusicMode::Menu);
    check(mixer.currentTrack() == "main_theme.mp3", "Returning to the menu brings the main theme back");
    mixer.setMode(audio::MusicMode::Silent);
    check(mixer.currentTrack().empty() && !backend.playing("jjmusic"), "Silent mode stops all music");
    std::filesystem::remove_all(root);
}

void startupBlipIsNotTrackEnd() {
    const auto root = makeAudioFolder(3);
    FakeBackend backend;
    audio::Mixer mixer(backend, root, 5);
    mixer.setMode(audio::MusicMode::Game);
    backend.finishMusic(); // A device may briefly report "stopped" right after play().
    mixer.tick(0.2);
    mixer.tick(0.5);
    check(backend.musicStarts() == 1, "A track is not restarted during its first second");
    mixer.tick(1.5);
    check(backend.musicStarts() == 2, "A track that really stopped is replaced afterwards");
    std::filesystem::remove_all(root);
}

void soundEffectsAndDucking() {
    const auto root = makeAudioFolder(2);
    FakeBackend backend;
    audio::Levels levels;
    audio::Mixer mixer(backend, root, 3, levels);
    mixer.play(audio::Sfx::CardDeal);
    mixer.play(audio::Sfx::CardDeal);
    mixer.play(audio::Sfx::CardDeal);
    check(backend.plays["card_deal.mp3"] == 3, "Every request plays the sound");
    int overlapping = 0;
    for (const auto& entry : backend.playing_)
        if (entry.second && backend.files_[entry.first].filename() == "card_deal.mp3") ++overlapping;
    check(overlapping == 2, "Quick repeats overlap on a second voice instead of cutting off");
    mixer.play(audio::Sfx::UiError);
    check(backend.plays["ui_error.mp3"] == 1, "UI sounds play too");
    mixer.play(audio::Sfx::UiOpen);
    mixer.play(audio::Sfx::UiClose);
    mixer.play(audio::Sfx::UiSelection);
    check(backend.plays["ui_open.mp3"] == 1 && backend.plays["ui_close.mp3"] == 1 &&
          backend.plays["ui_selection.mp3"] == 1,
          "Opening, closing and selection each keep their own supplied UI recording");
    mixer.play(audio::Sfx::CardRoll);
    check(backend.plays["card_roll.mp3"] == 1 && backend.plays["card_deal.mp3"] == 3,
          "Drawing an extra card uses its separate roll recording");
    bool dealTrimmed = true;
    for (const auto& voice : backend.files_)
        if (voice.second.filename() == "card_deal.mp3")
            dealTrimmed = dealTrimmed && backend.volume_[voice.first] <= .9f;
    check(dealTrimmed, "The original deal recording is played with headroom for decoder overshoot");

    mixer.setMode(audio::MusicMode::Game);
    check(backend.volume_["jjmusic"] == levels.music, "Music starts at its own level");
    mixer.setDucked(true);
    check(backend.volume_["jjmusic"] < levels.music, "Ducking lowers the music");
    mixer.setDucked(false);
    check(backend.volume_["jjmusic"] == levels.music, "Music returns to full level after ducking");
    mixer.setDucked(true);
    backend.finishMusic();
    mixer.tick(5.0);
    check(backend.volume_["jjmusic"] < levels.music, "The next track starts ducked while a duck is active");
    std::filesystem::remove_all(root);
}

void carPassFollowsTheCar() {
    const auto root = makeAudioFolder(2);
    FakeBackend backend;
    audio::Levels levels;
    audio::Mixer mixer(backend, root, 4, levels);
    mixer.tick(10.0);
    mixer.startCarPass(3.25);
    check(backend.seeks.size() == 1 && backend.seeks[0].first == "car_pass.mp3" && backend.seeks[0].second == 3.25,
          "The car recording starts at the offset matching the car's position");
    check(mixer.carPassPlaying() && backend.volume_["jjcar"] == levels.car, "The car pass plays at its own level");
    mixer.startCarPass(-1.0);
    check(backend.seeks.size() == 2 && backend.seeks[1].second == 0.0, "A restart is clamped to the start of the file");

    mixer.tick(11.0);
    mixer.stopCarPass(1.0);
    mixer.tick(11.5);
    const float half = backend.volume_["jjcar"];
    check(mixer.carPassPlaying() && half > 0.0f && half < levels.car, "The car fades out gradually");
    mixer.tick(12.1);
    check(!mixer.carPassPlaying() && !backend.playing("jjcar"), "The car stops once the fade has finished");
    mixer.startCarPass(1.0);
    mixer.stopCarPass(0.0);
    check(!mixer.carPassPlaying() && !backend.playing("jjcar"), "A zero-length fade stops the car immediately");
    mixer.stopCarPass(1.0); // Stopping a silent car is harmless.
    std::filesystem::remove_all(root);

    FakeBackend quiet;
    const auto bare = makeAudioFolder(1);
    std::filesystem::remove(bare / "ambient/car_pass.mp3");
    audio::Mixer none(quiet, bare, 4);
    none.startCarPass(2.0);
    check(quiet.seeks.empty() && !none.carPassPlaying(), "A missing car recording is skipped silently");
    std::filesystem::remove_all(bare);
}

void musicStatusTracksTheSoundtrack() {
    const auto root = makeAudioFolder(3);
    FakeBackend backend;
    audio::Mixer mixer(backend, root, 8);
    const auto files = audio::discoverSoundtracks(root);
    check(files.size() == 3 && files[0].filename() == "soundtrack_1.mp3" && files[2].filename() == "soundtrack_3.mp3",
          "Soundtracks are discovered in sorted order");
    mixer.setMode(audio::MusicMode::Menu);
    mixer.tick(2.0);
    check(mixer.musicStatus().track == -1 && !mixer.musicStatus().playing, "The menu theme has no soundtrack index");
    mixer.setMode(audio::MusicMode::Game);
    backend.reportedPosition = 12.5;
    mixer.tick(3.0);
    auto status = mixer.musicStatus();
    check(status.playing && status.track >= 0 && status.track < 3 && status.position == 12.5 && status.sampledAt == 3.0,
          "The status reports the track index and the device position");
    check(files[static_cast<std::size_t>(status.track)].filename().string() == mixer.currentTrack(),
          "The reported index names the file that is playing");
    backend.reportedPosition = -1.0;
    mixer.tick(4.0);
    status = mixer.musicStatus();
    check(status.position > 1.9 && status.position < 2.1, "Without a device position the clock falls back to elapsed time");
    mixer.setMode(audio::MusicMode::Silent);
    check(!mixer.musicStatus().playing && mixer.musicStatus().track == -1,
          "Changing mode invalidates the previous soundtrack clock immediately");
    mixer.tick(5.0);
    check(!mixer.musicStatus().playing, "Silence reports no track");
    std::filesystem::remove_all(root);
}

void brokenTracksCannotHideThePlayableOne() {
    const auto root = makeAudioFolder(5);
    bool keptPlaying = true;
    bool failedCycleIsBounded = true;
    for (std::uint32_t seed = 0; seed < 60; ++seed) {
        FakeBackend backend;
        backend.refuse = {"soundtrack_1.mp3", "soundtrack_2.mp3", "soundtrack_3.mp3", "soundtrack_4.mp3"};
        audio::Mixer mixer(backend, root, seed);
        mixer.setMode(audio::MusicMode::Game);
        for (int end = 1; end <= 30; ++end) {
            keptPlaying = keptPlaying && mixer.currentTrack() == "soundtrack_5.mp3" && backend.playing("jjmusic");
            const int before = backend.openAttempts;
            backend.finishMusic();
            mixer.tick(end * 2.0);
            failedCycleIsBounded = failedCycleIsBounded && backend.openAttempts - before <= 5;
        }
        keptPlaying = keptPlaying && backend.plays["soundtrack_5.mp3"] == 31;
    }
    check(keptPlaying, "A single playable soundtrack keeps looping across shuffle boundaries despite four failed files");
    check(failedCycleIsBounded, "Each replacement opens at most one candidate per discovered file");
    FakeBackend broken;
    broken.refuse = {"soundtrack_1.mp3", "soundtrack_2.mp3", "soundtrack_3.mp3", "soundtrack_4.mp3", "soundtrack_5.mp3", "main_theme.mp3"};
    audio::Mixer mixer(broken, root, 3);
    mixer.setMode(audio::MusicMode::Game);
    const int attempts = broken.openAttempts;
    for (int i = 0; i < 100; ++i) mixer.tick(i * 2.0);
    check(broken.openAttempts == attempts && mixer.currentTrack().empty(),
          "A wholly failed soundtrack cycle stays silent without retrying every frame");
    mixer.setMode(audio::MusicMode::Menu);
    for (int i = 100; i < 200; ++i) mixer.tick(i * 2.0);
    check(broken.openAttempts == attempts + 3,
          "An unreadable menu theme is retried only a bounded number of times");
    std::filesystem::remove_all(root);
}

void carCompletionAndInvalidRequestsAreHarmless() {
    const auto root = makeAudioFolder(1);
    FakeBackend backend;
    audio::Mixer mixer(backend, root, 4);
    mixer.tick(10.0);
    mixer.startCarPass(0.0);
    backend.playing_["jjcar"] = false;
    mixer.tick(10.2);
    check(mixer.carPassPlaying(), "A short startup status blip does not finish the car recording");
    mixer.tick(11.1);
    check(!mixer.carPassPlaying(), "Natural completion clears the car state without an explicit stop");
    mixer.startCarPass(0.0);
    mixer.stopCarPass(1.0);
    mixer.tick(11.6);
    const auto half = backend.volume_["jjcar"];
    mixer.tick(10.0);
    mixer.tick(std::numeric_limits<double>::quiet_NaN());
    mixer.tick(std::numeric_limits<double>::infinity());
    check(backend.volume_["jjcar"] == half && mixer.carPassPlaying(),
          "Invalid or backwards clocks cannot amplify or corrupt a car fade");
    mixer.startCarPass(std::numeric_limits<double>::infinity());
    check(backend.seeks.back().second == 0.0, "A non-finite car offset is replaced by a valid start");
    mixer.stopCarPass(std::numeric_limits<double>::quiet_NaN());
    check(!mixer.carPassPlaying(), "A non-finite fade duration stops safely instead of producing NaN gain");
    const auto plays = backend.started.size();
    mixer.play(audio::Sfx::Count);
    mixer.play(static_cast<audio::Sfx>(-1));
    check(backend.started.size() == plays, "Invalid sound ids do not access or start any voice");
    check(std::string(audio::sfxFile(audio::Sfx::Count)).empty(), "An invalid sound id has no mapped file");
    std::filesystem::remove_all(root);
}

void missingFilesAreHarmless() {
    const auto root = makeAudioFolder(3);
    std::filesystem::remove(root / "sfx/you_lose.mp3");
    FakeBackend backend;
    backend.refuse = {"soundtrack_1.mp3"};
    {
        audio::Mixer mixer(backend, root, 9);
        mixer.play(audio::Sfx::YouLose);
        check(backend.plays.count("you_lose.mp3") == 0, "A missing effect is skipped silently");
        mixer.setMode(audio::MusicMode::Game);
        double now = 0;
        for (int i = 0; i < 40; ++i) {
            for (int t = 0; t < 12; ++t) mixer.tick(now += 0.1);
            backend.finishMusic();
        }
        check(backend.plays.count("soundtrack_1.mp3") == 0 && backend.musicStarts() > 10,
              "A track that cannot be opened is skipped while the rest keep playing");
    }
    check(backend.open_.empty(), "Destroying the mixer closes every opened sound");

    const auto bare = makeAudioFolder(0);
    std::filesystem::remove_all(bare);
    std::filesystem::create_directories(bare);
    FakeBackend quiet;
    audio::Mixer mixer(quiet, bare, 1);
    mixer.setMode(audio::MusicMode::Menu);
    mixer.setMode(audio::MusicMode::Game);
    for (int i = 0; i < 100; ++i) mixer.tick(i * 0.1);
    mixer.play(audio::Sfx::UiOpen);
    check(quiet.started.empty(), "A folder without audio files stays silent without errors or retries");
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(bare);
}

bool near(float actual, float expected) { return std::abs(actual - expected) < .00001f; }

bool voicesHaveVolume(const FakeBackend& backend, const char* filename, float expected) {
    int count = 0;
    for (const auto& voice : backend.files_) {
        if (voice.second.filename() != filename) continue;
        ++count;
        if (!near(backend.volume_.at(voice.first), expected)) return false;
    }
    return count == static_cast<int>(audio::Mixer::voicesPerSound);
}

void liveMasterVolumesAreIndependent() {
    const auto root = makeAudioFolder(3);
    FakeBackend backend;
    audio::Levels levels;
    levels.music = .4f;
    levels.sfx = .8f;
    levels.ui = .6f;
    levels.car = .7f;
    levels.duck = .25f;
    audio::Mixer mixer(backend, root, 15, levels);
    mixer.setMode(audio::MusicMode::Game);
    mixer.play(audio::Sfx::CardDeal);
    mixer.play(audio::Sfx::CardRoll);
    mixer.play(audio::Sfx::UiError);
    mixer.startCarPass(.24);
    mixer.tick(.4);
    const auto status = mixer.musicStatus();
    const auto starts = backend.started.size();
    const auto seeks = backend.seeks.size();
    const auto opened = backend.openAttempts;
    mixer.setVolumes(.5f, .25f);
    check(near(backend.volume_["jjmusic"], .2f), "The music master multiplies the configured music level");
    check(voicesHaveVolume(backend, "card_deal.mp3", .18f) && voicesHaveVolume(backend, "card_roll.mp3", .2f),
          "The effects master updates already-open card voices and preserves the deal headroom trim");
    check(voicesHaveVolume(backend, "ui_error.mp3", .15f) && voicesHaveVolume(backend, "ui_open.mp3", .15f),
          "All UI voices share the effects master, including voices not yet played");
    check(near(backend.volume_["jjcar"], .175f), "The car belongs to the effects master");
    const auto after = mixer.musicStatus();
    check(after.track == status.track && after.position == status.position && after.sampledAt == status.sampledAt,
          "Volume changes preserve the soundtrack clock and track identity");
    mixer.setVolumes(0.0f, .25f);
    check(backend.volume_["jjmusic"] == 0 && near(backend.volume_["jjcar"], .175f),
          "Music mute leaves effects and ambience unchanged");
    mixer.setVolumes(.75f, 0.0f);
    bool effectsSilent = true;
    for (const auto& voice : backend.files_)
        if (voice.first != "jjmusic") effectsSilent = effectsSilent && backend.volume_[voice.first] == 0;
    check(effectsSilent && near(backend.volume_["jjmusic"], .3f),
          "Effects mute silences every SFX, UI and car voice while music keeps its own volume");
    check(backend.started.size() == starts && backend.seeks.size() == seeks && backend.openAttempts == opened &&
          backend.playing("jjmusic") && backend.playing("jjcar"),
          "Muting and live changes never restart, seek, reopen or stop an active recording");
    mixer.setDucked(true);
    check(near(backend.volume_["jjmusic"], .075f), "Ducking multiplies the selected master music level");
    mixer.setVolumes(.5f, .5f);
    check(near(backend.volume_["jjmusic"], .05f), "A slider change preserves active music ducking");
    backend.finishMusic();
    mixer.tick(2.0);
    check(near(backend.volume_["jjmusic"], .05f) && backend.musicStarts() == 2,
          "The next shuffled track starts with the current master volume and duck state");
    mixer.setMode(audio::MusicMode::Menu);
    check(near(backend.volume_["jjmusic"], .05f), "The menu theme uses the same current master volume");
    std::filesystem::remove_all(root);
}

void changingEffectsVolumePreservesCarFade() {
    const auto root = makeAudioFolder(1);
    FakeBackend backend;
    audio::Mixer mixer(backend, root, 2);
    mixer.tick(10);
    mixer.startCarPass(.24);
    mixer.stopCarPass(2);
    mixer.tick(10.5);
    mixer.setVolumes(1, .4f);
    check(near(backend.volume_["jjcar"], .27f), "Changing effects gain preserves the car fade's current three-quarter envelope");
    mixer.tick(11.0);
    check(near(backend.volume_["jjcar"], .18f), "The car fade continues under its new master gain");
    mixer.setVolumes(1, 0);
    mixer.tick(11.5);
    check(backend.volume_["jjcar"] == 0 && mixer.carPassPlaying(), "The car fade continues silently while effects are muted");
    mixer.setVolumes(1, .8f);
    check(near(backend.volume_["jjcar"], .18f), "Unmuting a fading car restores only its remaining quarter envelope");
    mixer.tick(12.01);
    check(!mixer.carPassPlaying() && !backend.playing("jjcar") && backend.seeks.size() == 1,
          "Volume changes cannot extend, restart or seek the original car fade");
    std::filesystem::remove_all(root);
}

void volumeGuardsAndDeduplication() {
    const auto root = makeAudioFolder(3);
    FakeBackend backend;
    audio::Mixer mixer(backend, root, 9);
    mixer.setVolumes(.25f, .6f);
    mixer.setMode(audio::MusicMode::Menu);
    check(near(backend.volume_["jjmusic"], .125f) && voicesHaveVolume(backend, "ui_open.mp3", .48f),
          "Volume preferences set before playback apply to the first music and effect voices");
    const int writes = backend.volumeWrites;
    for (int i = 0; i < 100; ++i) mixer.setVolumes(.25f, .6f);
    mixer.setVolumes(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    check(backend.volumeWrites == writes, "Identical and non-finite slider values do not generate repeated backend writes");
    mixer.setVolumes(-1.0f, 2.0f);
    check(backend.volume_["jjmusic"] == 0 && voicesHaveVolume(backend, "card_roll.mp3", 1.0f),
          "Finite out-of-range master values clamp to mute or full volume");
    mixer.setVolumes(std::numeric_limits<float>::infinity(), .25f);
    check(backend.volume_["jjmusic"] == 0 && voicesHaveVolume(backend, "ui_open.mp3", .2f),
          "An invalid music value preserves that channel without discarding a valid effects update");
    mixer.setVolumes(.5f, std::numeric_limits<float>::quiet_NaN());
    check(near(backend.volume_["jjmusic"], .25f) && voicesHaveVolume(backend, "ui_open.mp3", .2f),
          "An invalid effects value preserves that channel without discarding a valid music update");

    FakeBackend referenceBackend;
    audio::Mixer reference(referenceBackend, root, 9);
    reference.setMode(audio::MusicMode::Game);
    mixer.setMode(audio::MusicMode::Game);
    bool sameTracks = true;
    for (int end = 1; end < 20; ++end) {
        sameTracks = sameTracks && mixer.currentTrack() == reference.currentTrack();
        mixer.setVolumes(end % 2 ? 0.0f : .6f, .3f);
        backend.finishMusic();
        referenceBackend.finishMusic();
        mixer.tick(end * 2.0);
        reference.tick(end * 2.0);
    }
    check(sameTracks && mixer.currentTrack() == reference.currentTrack(),
          "Muting and changing volume across tracks never reset or consume the shuffle playlist");
    audio::Engine disabled(root, false, .2f, 0.0f);
    disabled.setVolumes(.8f, .4f);
    disabled.setVolumes(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    check(!disabled.active() && disabled.musicClock().track == -1, "Disabled audio safely accepts startup and live volume preferences");
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    playlistNeverRepeatsAndCoversEveryTrack();
    menuThemeLoopsUntilGameStarts();
    soundtracksShuffleForever();
    startupBlipIsNotTrackEnd();
    soundEffectsAndDucking();
    carPassFollowsTheCar();
    musicStatusTracksTheSoundtrack();
    brokenTracksCannotHideThePlayableOne();
    carCompletionAndInvalidRequestsAreHarmless();
    missingFilesAreHarmless();
    liveMasterVolumesAreIndependent();
    changingEffectsVolumePreservesCarFade();
    volumeGuardsAndDeduplication();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
