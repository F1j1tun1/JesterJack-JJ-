#include "audio/Preferences.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool closeTo(float a, float b) { return std::abs(a - b) < 0.000001f; }

class TemporaryDirectory {
public:
    std::filesystem::path path;
    TemporaryDirectory() {
        // CTest runs in its writable build directory, including restricted Windows sessions.
        const auto base = std::filesystem::current_path();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 20; ++attempt) {
            const auto candidate = base / ("jester_audio_preferences_test_" + std::to_string(stamp)
                                          + "_" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) {
                path = candidate.lexically_normal();
                return;
            }
        }
        throw std::runtime_error("Cannot create an isolated preferences test directory");
    }
    ~TemporaryDirectory() {
        // Only the unique directory successfully created by this instance is owned.
        std::error_code error;
        if (!path.empty()) std::filesystem::remove_all(path, error);
    }
};

void write(const std::filesystem::path& file, const std::string& text) {
    std::ofstream output(file);
    output << text;
    if (!output) throw std::runtime_error("Cannot write test fixture");
}

void loadDefaultsAndMalformed(const std::filesystem::path& root) {
    const auto file = root / "partial.ini";
    auto prefs = audio::Preferences::load(file);
    check(prefs.music == 1 && prefs.effects == 1, "A missing file preserves the current mix");
    write(file, "# ignored\nunknown=0\nnot an assignment\nmusic = 0.375\neffects = broken\n");
    prefs = audio::Preferences::load(file);
    check(closeTo(prefs.music, 0.375f) && prefs.effects == 1,
          "A valid key survives malformed and missing settings independently");
    write(file, "music=nan\neffects=inf\nmusic=0.25 garbage\neffects=-infinity\n");
    prefs = audio::Preferences::load(file);
    check(prefs.music == 1 && prefs.effects == 1, "Nonfinite and incomplete numeric values use defaults");
    write(file, "music=0.25\nmusic=nan\neffects=.5 ; comment\neffects=no\n");
    prefs = audio::Preferences::load(file);
    check(closeTo(prefs.music, .25f) && closeTo(prefs.effects, .5f),
          "Later invalid duplicates do not discard earlier valid values");
    write(file, "music=-0.5\neffects=2.75\n");
    prefs = audio::Preferences::load(file);
    check(prefs.music == 0 && prefs.effects == 1, "Finite values clamp to the normalized interval");
    write(file, "music=1e9999\neffects=-1e9999\n");
    prefs = audio::Preferences::load(file);
    check(prefs.music == 1 && prefs.effects == 1, "Overflowing numeric input is ignored safely");
    check(audio::Preferences::load(root).music == 1, "An unreadable non-file path uses defaults");
}

void roundTrip(const std::filesystem::path& root) {
    const auto file = root / std::filesystem::path(L"\u0417\u0432\u0443\u043a") / "nested" / "audio.ini";
    audio::Preferences prefs{0.12345679f, 0.7654321f};
    check(prefs.save(file), "Saving creates missing parent directories and accepts Unicode paths");
    auto loaded = audio::Preferences::load(file);
    check(loaded.music == prefs.music && loaded.effects == prefs.effects,
          "Both master gains round-trip without precision loss");
    prefs = {0, 1};
    check(prefs.save(file), "Saving replaces an existing preferences file");
    loaded = audio::Preferences::load(file);
    check(loaded.music == 0 && loaded.effects == 1, "Mute and full-volume values round-trip");
    prefs = {-5, 3};
    check(prefs.save(file), "Saving sanitizes out-of-range programmatic values");
    loaded = audio::Preferences::load(file);
    check(loaded.music == 0 && loaded.effects == 1, "Persisted finite gains stay normalized");
    prefs = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    check(prefs.save(file), "Saving nonfinite programmatic values remains safe");
    loaded = audio::Preferences::load(file);
    check(loaded.music == 1 && loaded.effects == 1, "Nonfinite programmatic gains persist as defaults");
}

void failedWritesPreserveData(const std::filesystem::path& root) {
    const auto file = root / "retained.ini";
    const audio::Preferences original{0.2f, 0.7f};
    const audio::Preferences changed{0.9f, 0.1f};
    check(original.save(file), "A valid preferences fixture is written");
    check(!changed.save(file / "unreachable.ini"), "A regular-file parent reports failure without throwing");
    auto loaded = audio::Preferences::load(file);
    check(closeTo(loaded.music, original.music) && closeTo(loaded.effects, original.effects),
          "A failed parent creation leaves the existing valid data intact");
    check(!changed.save({}) && !changed.save(root), "Empty and directory destinations fail safely");
#ifdef _WIN32
    // The target can still be read, but Windows disallows replacing it until closed.
    const HANDLE locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "The existing destination can be locked against replacement");
    if (locked != INVALID_HANDLE_VALUE) {
        check(!changed.save(file), "A failed replacement reports failure without deleting the old file");
        loaded = audio::Preferences::load(file);
        check(closeTo(loaded.music, original.music) && closeTo(loaded.effects, original.effects),
              "The old valid preferences survive a failed final rename");
        CloseHandle(locked);
    }
#endif
    bool temporaryLeft = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        temporaryLeft = temporaryLeft || entry.path().filename().string().find(".tmp.") != std::string::npos;
    check(!temporaryLeft, "Failed saves leave no temporary preferences files behind");
    const auto defaults = audio::defaultPreferencesPath();
    check(defaults.empty() || (defaults.is_absolute() && defaults.filename() == "audio.ini"),
          "The default location is absolute user storage or disabled");
}
} // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        loadDefaultsAndMalformed(temporary.path);
        roundTrip(temporary.path);
        failedWritesPreserveData(temporary.path);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
