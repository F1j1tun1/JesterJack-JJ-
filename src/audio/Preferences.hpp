#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <random>
#include <sstream>
#include <string>
#include <system_error>

namespace audio {
namespace preferences_detail {

inline std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

inline float normalized(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 1.0f;
}

inline void discard(const std::filesystem::path& file) noexcept {
    try {
        std::error_code error;
        std::filesystem::remove(file, error);
    } catch (...) {}
}

// Ignore relative environment paths, which could put settings in the checkout.
#ifdef _WIN32
inline std::filesystem::path environmentPath(const wchar_t* name) {
    std::size_t size = 0;
    if (_wgetenv_s(&size, nullptr, 0, name) != 0 || size < 2) return {};
    std::wstring value(size, L'\0');
    if (_wgetenv_s(&size, value.data(), value.size(), name) != 0) return {};
    value.resize(size - 1);
    std::filesystem::path path(value);
    return path.is_absolute() ? path : std::filesystem::path{};
}
#else
inline std::filesystem::path environmentPath(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value) return {};
    std::filesystem::path path(value);
    return path.is_absolute() ? path : std::filesystem::path{};
}
#endif

} // namespace preferences_detail

// Master gains over the existing mix: 1 preserves the original relative levels.
struct Preferences {
    float music = 1.0f;
    float effects = 1.0f;

    static Preferences load(const std::filesystem::path& file) noexcept {
        Preferences result;
        try {
            std::ifstream input(file);
            std::string line;
            while (std::getline(input, line)) {
                line = line.substr(0, line.find_first_of("#;"));
                const auto separator = line.find('=');
                if (separator == std::string::npos) continue;
                const auto key = preferences_detail::trim(line.substr(0, separator));
                if (key != "music" && key != "effects") continue;
                std::istringstream value(line.substr(separator + 1));
                value.imbue(std::locale::classic());
                float parsed = 1.0f;
                if (!(value >> parsed) || !std::isfinite(parsed)) continue;
                value >> std::ws;
                if (!value.eof()) continue;
                (key == "music" ? result.music : result.effects) = preferences_detail::normalized(parsed);
            }
        } catch (...) {}
        return result;
    }

    bool save(const std::filesystem::path& file) const noexcept {
        std::filesystem::path temporary;
        try {
            if (file.empty() || !file.has_filename() || file.filename() == "." || file.filename() == "..")
                return false;
            std::error_code error;
            if (!file.parent_path().empty()) {
                std::filesystem::create_directories(file.parent_path(), error);
                if (error) return false;
            }
            // A sibling temporary file keeps replacement on the same filesystem.
            temporary = file;
            std::random_device random;
            temporary += ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "." + std::to_string(random());
            std::ofstream output(temporary, std::ios::trunc);
            output.imbue(std::locale::classic());
            output << std::setprecision(std::numeric_limits<float>::max_digits10)
                   << "music=" << preferences_detail::normalized(music)
                   << "\neffects=" << preferences_detail::normalized(effects) << '\n';
            output.flush();
            const bool written = output.good();
            output.close();
            if (!written || output.fail()) {
                preferences_detail::discard(temporary);
                return false;
            }
            // rename replaces a regular destination; never remove the old file first.
            std::filesystem::rename(temporary, file, error);
            if (error) {
                preferences_detail::discard(temporary);
                return false;
            }
            return true;
        } catch (...) {
            if (!temporary.empty()) preferences_detail::discard(temporary);
            return false;
        }
    }
};

// An empty result disables persistence if no usable user or temporary directory exists.
inline std::filesystem::path defaultPreferencesPath() noexcept {
    try {
        using preferences_detail::environmentPath;
#ifdef _WIN32
        auto root = environmentPath(L"LOCALAPPDATA");
        if (root.empty()) root = environmentPath(L"APPDATA");
        if (!root.empty()) return root / "JesterJack" / "audio.ini";
        root = environmentPath(L"USERPROFILE");
        if (!root.empty()) return root / "AppData" / "Local" / "JesterJack" / "audio.ini";
        root = environmentPath(L"HOME");
#else
        auto root = environmentPath("XDG_CONFIG_HOME");
        if (!root.empty()) return root / "JesterJack" / "audio.ini";
        root = environmentPath("HOME");
#endif
        if (!root.empty()) return root / ".config" / "JesterJack" / "audio.ini";
        std::error_code error;
        root = std::filesystem::temp_directory_path(error);
        if (!error && root.is_absolute()) return root / "JesterJack" / "audio.ini";
    } catch (...) {}
    return {};
}

} // namespace audio
