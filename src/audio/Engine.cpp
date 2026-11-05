#include "audio/Engine.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cwchar>
#include <deque>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#endif

namespace audio {

#ifdef _WIN32
namespace {

std::wstring widen(const std::string& text) { return std::wstring(text.begin(), text.end()); } // Aliases are ASCII.

float volumeOr(float value, float fallback) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
}

// MP3 through the OS: `open "file" type mpegvideo alias NAME`, then play/stop/setaudio/status.
class MciBackend final : public Backend {
public:
    bool open(const std::string& alias, const std::filesystem::path& file) override {
        std::error_code error;
        auto absolute = std::filesystem::absolute(file, error);
        if (error) absolute = file;
        absolute.make_preferred();
        if (send(L"open \"" + absolute.wstring() + L"\" type mpegvideo alias " + widen(alias))) return true;
        // Some systems reject certain Unicode paths; the 8.3 name is plain ASCII.
        wchar_t shortPath[MAX_PATH]{};
        const DWORD length = GetShortPathNameW(absolute.c_str(), shortPath, MAX_PATH);
        if (length > 0 && length < MAX_PATH &&
            send(L"open \"" + std::wstring(shortPath, length) + L"\" type mpegvideo alias " + widen(alias), false))
            return true;
        return false;
    }
    void play(const std::string& alias, bool fromStart) override {
        send(L"play " + widen(alias) + (fromStart ? L" from 0" : L""), false);
    }
    void playFrom(const std::string& alias, double seconds) override {
        const long long milliseconds = static_cast<long long>(std::llround(std::max(0.0, seconds) * 1000.0));
        send(L"play " + widen(alias) + L" from " + std::to_wstring(milliseconds), false);
    }
    double position(const std::string& alias) override {
        wchar_t reply[64]{};
        const MCIERROR code = mciSendStringW((L"status " + widen(alias) + L" position").c_str(), reply, 64, nullptr);
        if (code != 0) return -1.0;
        return std::wcstod(reply, nullptr) / 1000.0; // The default time format is milliseconds.
    }
    void stop(const std::string& alias) override { send(L"stop " + widen(alias), false); }
    void close(const std::string& alias) override { send(L"close " + widen(alias), false); }
    void setVolume(const std::string& alias, float volume) override {
        const int value = static_cast<int>(std::lround(std::clamp(volume, 0.0f, 1.0f) * 1000.0f));
        send(L"setaudio " + widen(alias) + L" volume to " + std::to_wstring(value), false);
    }
    bool playing(const std::string& alias) override {
        wchar_t reply[64]{};
        const MCIERROR code = mciSendStringW((L"status " + widen(alias) + L" mode").c_str(), reply, 64, nullptr);
        return code == 0 && std::wstring(reply) == L"playing";
    }

private:
    static bool send(const std::wstring& command, bool report = true) {
        const MCIERROR code = mciSendStringW(command.c_str(), nullptr, 0, nullptr);
        if (code != 0 && report) {
            wchar_t message[256]{};
            mciGetErrorStringW(code, message, 256);
            std::wcerr << L"Audio: MCI could not run \"" << command << L"\": " << message << L'\n';
        }
        return code == 0;
    }
};

} // namespace

struct Engine::Impl {
    struct Command {
        enum class Kind { Play, Music, Duck, CarStart, CarStop } kind;
        int value;
        double amount = 0.0;
    };

    std::filesystem::path root;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Command> queue;
    bool quit = false;
    float requestedMusic = 1.0f, requestedEffects = 1.0f; // Guarded by mutex; one latest pair, no command backlog.
    bool volumesDirty = true;
    std::atomic<int> requestedMode{-1}; // Callers repeat these every frame; only changes are queued.
    std::atomic<int> requestedDuck{-1};
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    MusicStatus status; // Guarded by `mutex`.
    std::thread worker;

    Impl(std::filesystem::path audioRoot, float music, float effects)
        : root(std::move(audioRoot)), requestedMusic(volumeOr(music, 1.0f)), requestedEffects(volumeOr(effects, 1.0f)) {
        const std::uint32_t seed = std::random_device{}();
        worker = std::thread([this, seed] { run(seed); });
    }
    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            quit = true;
        }
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }

    void post(Command command) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            // The queue only grows if the thread stalls; drop the oldest sound rather than lag behind.
            if (queue.size() > 256) queue.pop_front();
            queue.push_back(command);
        }
        wake.notify_one();
    }

    void setVolumes(float music, float effects) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            music = volumeOr(music, requestedMusic);
            effects = volumeOr(effects, requestedEffects);
            if (music == requestedMusic && effects == requestedEffects) return;
            requestedMusic = music;
            requestedEffects = effects;
            volumesDirty = true;
        }
        wake.notify_one();
    }

    void run(std::uint32_t seed) {
        MciBackend backend;
        Mixer mixer(backend, root, seed);
        const auto clock = [this] {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        };
        // Opening the effect voices can take longer than the music startup grace.
        mixer.tick(clock());
        std::unique_lock<std::mutex> lock(mutex);
        while (!quit) {
            wake.wait_for(lock, std::chrono::milliseconds(50), [this] { return quit || volumesDirty || !queue.empty(); });
            if (quit) break;
            if (volumesDirty) {
                const float music = requestedMusic, effects = requestedEffects;
                volumesDirty = false;
                lock.unlock();
                mixer.setVolumes(music, effects);
                lock.lock();
            }
            while (!queue.empty()) {
                const Command command = queue.front();
                queue.pop_front();
                if (command.kind == Command::Kind::Music) status = MusicStatus{};
                lock.unlock();
                if (command.kind == Command::Kind::Music || command.kind == Command::Kind::CarStart ||
                    command.kind == Command::Kind::CarStop) mixer.tick(clock());
                switch (command.kind) {
                    case Command::Kind::Play: mixer.play(static_cast<Sfx>(command.value)); break;
                    case Command::Kind::Music: mixer.setMode(static_cast<MusicMode>(command.value)); break;
                    case Command::Kind::Duck: mixer.setDucked(command.value != 0); break;
                    case Command::Kind::CarStart: mixer.startCarPass(command.amount); break;
                    case Command::Kind::CarStop: mixer.stopCarPass(command.amount); break;
                }
                lock.lock();
            }
            if (quit) break;
            lock.unlock();
            mixer.tick(clock());
            MusicStatus sample = mixer.musicStatus();
            // MCI position/status queries may block; timestamp the result after them.
            sample.sampledAt = clock();
            lock.lock();
            status = sample;
        }
    }
};

Engine::Engine(const std::filesystem::path& audioRoot, bool enabled, float music, float effects)
    : impl_(enabled ? std::make_unique<Impl>(audioRoot, music, effects) : nullptr) {}
Engine::~Engine() = default;
void Engine::play(Sfx sound) {
    if (impl_) impl_->post({Impl::Command::Kind::Play, static_cast<int>(sound)});
}
void Engine::setMusic(MusicMode mode) {
    const int value = static_cast<int>(mode);
    if (impl_ && impl_->requestedMode.exchange(value) != value) impl_->post({Impl::Command::Kind::Music, value});
}
void Engine::setDucked(bool ducked) {
    const int value = ducked ? 1 : 0;
    if (impl_ && impl_->requestedDuck.exchange(value) != value) impl_->post({Impl::Command::Kind::Duck, value});
}
void Engine::setVolumes(float music, float effects) {
    if (impl_) impl_->setVolumes(music, effects);
}
void Engine::carPass(double offsetSeconds) {
    if (impl_) impl_->post({Impl::Command::Kind::CarStart, 0, offsetSeconds});
}
void Engine::carStop(double fadeSeconds) {
    if (impl_) impl_->post({Impl::Command::Kind::CarStop, 0, fadeSeconds});
}
Engine::MusicClock Engine::musicClock() const {
    if (!impl_) return {};
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->status.track < 0 || !impl_->status.playing) return {};
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - impl_->begin).count();
    return {impl_->status.track, impl_->status.position + std::max(0.0, now - impl_->status.sampledAt)};
}
bool Engine::active() const { return impl_ != nullptr; }

#else // Non-Windows builds keep the interface but stay silent.

struct Engine::Impl {};
Engine::Engine(const std::filesystem::path&, bool, float, float) {}
Engine::~Engine() = default;
void Engine::play(Sfx) {}
void Engine::setMusic(MusicMode) {}
void Engine::setDucked(bool) {}
void Engine::setVolumes(float, float) {}
void Engine::carPass(double) {}
void Engine::carStop(double) {}
Engine::MusicClock Engine::musicClock() const { return {}; }
bool Engine::active() const { return false; }

#endif

} // namespace audio
