#include "presentation/BandSync.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace presentation {
namespace {
std::vector<float> readCurve(std::istringstream& line) {
    std::vector<float> values;
    int value = 0;
    while (line >> value) values.push_back(std::clamp(static_cast<float>(value) / 99.0f, 0.0f, 1.0f));
    return values;
}

float sample(const std::vector<float>& curve, double position) {
    if (curve.empty()) return 0.0f;
    position = std::clamp(position, 0.0, static_cast<double>(curve.size() - 1));
    const auto index = static_cast<std::size_t>(position);
    const std::size_t next = std::min(index + 1, curve.size() - 1);
    const auto fraction = static_cast<float>(position - static_cast<double>(index));
    return curve[index] + (curve[next] - curve[index]) * fraction;
}
} // namespace

SoundtrackSync SoundtrackSync::parse(std::istream& input) {
    SoundtrackSync sync;
    std::string text;
    while (std::getline(input, text)) {
        if (text.empty() || text[0] == '#') continue;
        std::istringstream line(text);
        std::string key;
        line >> key;
        if (key == "duration") line >> sync.duration_;
        else if (key == "bpm") line >> sync.bpm_;
        else if (key == "downbeat") line >> sync.downbeat_;
        else if (key == "rate") line >> sync.rate_;
        else if (key == "beats") {
            double time = 0;
            while (line >> time) sync.beats_.push_back(time);
        } else if (key == "low") sync.low_ = readCurve(line);
        else if (key == "mid") sync.mid_ = readCurve(line);
        else if (key == "high") sync.high_ = readCurve(line);
        else if (key == "hit") sync.hit_ = readCurve(line);
    }
    const bool sorted = std::is_sorted(sync.beats_.begin(), sync.beats_.end());
    if (!sorted || sync.rate_ <= 0.0 || !(sync.bpm_ >= 30.0 && sync.bpm_ <= 300.0)) return SoundtrackSync{};
    return sync;
}

SoundtrackSync SoundtrackSync::load(const std::filesystem::path& file) {
    std::ifstream input(file);
    return input ? parse(input) : SoundtrackSync{};
}

double SoundtrackSync::beatAt(double seconds) const {
    if (empty()) return seconds * bpm_ / 60.0;
    const double perSecond = bpm_ / 60.0;
    double index;
    if (seconds <= beats_.front()) {
        index = (seconds - beats_.front()) * perSecond;
    } else if (seconds >= beats_.back()) {
        index = static_cast<double>(beats_.size() - 1) + (seconds - beats_.back()) * perSecond;
    } else {
        const auto after = std::upper_bound(beats_.begin(), beats_.end(), seconds);
        const auto i = static_cast<std::size_t>(after - beats_.begin()) - 1;
        index = static_cast<double>(i) + (seconds - beats_[i]) / (beats_[i + 1] - beats_[i]);
    }
    return index - downbeat_;
}

BandEnergy SoundtrackSync::energyAt(double seconds) const {
    const double position = seconds * rate_;
    return {sample(low_, position), sample(mid_, position), sample(high_, position), sample(hit_, position)};
}

} // namespace presentation
