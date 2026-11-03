#include "audio/Mixer.hpp"
#include "audio/Playlist.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    std::vector<std::pair<std::string, double>> seeks; // playFrom calls: file name, seconds.
    double reportedPosition = -1.0;

    bool open(const std::string& alias, const std::filesystem::path& file) override {
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
    void setVolume(const std::string& alias, float volume) override { volume_[alias] = volume; }
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
    const auto root = std::filesystem::temp_directory_path() / ("jj_audio_test_" + std::to_string(soundtracks));
    std::filesystem::remove_all(root);
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
    mixer.tick(5.0);
    check(!mixer.musicStatus().playing, "Silence reports no track");
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

    const auto bare = std::filesystem::temp_directory_path() / "jj_audio_test_bare";
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

} // namespace

int main() {
    playlistNeverRepeatsAndCoversEveryTrack();
    menuThemeLoopsUntilGameStarts();
    soundtracksShuffleForever();
    startupBlipIsNotTrackEnd();
    soundEffectsAndDucking();
    carPassFollowsTheCar();
    musicStatusTracksTheSoundtrack();
    missingFilesAreHarmless();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
