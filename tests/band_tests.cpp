#include "graphics/BandModel.hpp"
#include "presentation/BandSync.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef BJ_ASSET_DIR
#define BJ_ASSET_DIR "assets"
#endif

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

using gfx::band::Part;
using gfx::band::Playback;

std::vector<Part> pose(double beats, float energy = .8f, float bpm = 110.0f, bool playing = true) {
    Playback playback;
    playback.playing = playing;
    playback.beats = beats;
    playback.bpm = bpm;
    playback.low = playback.mid = playback.high = playback.hit = energy;
    playback.time = static_cast<float>(beats * 60.0 / bpm);
    std::vector<Part> parts;
    gfx::band::build(playback, parts);
    return parts;
}

glm::vec3 centre(const Part& part) { return glm::vec3(part.model[3]); }

void syncParsesAndFollowsBeats() {
    std::istringstream text(
        "# comment\nduration 10\nbpm 120\ndownbeat 1\nbeats 1.0 1.5 2.0 2.6 3.2\nrate 2\n"
        "low 0 99\nmid 50 50 50\nhigh 99\nhit 0 0 99\n");
    const auto sync = presentation::SoundtrackSync::parse(text);
    check(!sync.empty() && sync.bpm() == 120.0 && sync.duration() == 10.0, "A valid sync file is read");
    check(std::abs(sync.beatAt(1.0) - (-1.0)) < 1e-9, "The downbeat offset shifts the first beat to the bar's last beat");
    check(std::abs(sync.beatAt(2.0) - 1.0) < 1e-9 && std::abs(sync.beatAt(1.75) - 0.5) < 1e-9,
          "Beats are interpolated between detected beat times");
    check(std::abs(sync.beatAt(2.3) - 1.5) < 1e-9, "Uneven beat spacing follows the actual grid");
    check(sync.beatAt(4.2) > sync.beatAt(3.2) && std::abs(sync.beatAt(4.2) - (3.0 + 1.0 * 2.0)) < 1e-9,
          "After the last beat the tempo is extrapolated");
    check(sync.beatAt(0.0) < sync.beatAt(1.0), "Before the first beat the tempo is extrapolated backwards");
    double previous = sync.beatAt(0.0);
    bool monotonic = true;
    for (double t = 0.01; t < 6.0; t += 0.01) {
        const double now = sync.beatAt(t);
        monotonic = monotonic && now > previous;
        previous = now;
    }
    check(monotonic, "The beat counter only ever moves forward");
    const auto energy = sync.energyAt(0.25);
    check(std::abs(energy.low - 0.5f) < .01f && std::abs(energy.mid - 0.505f) < .01f, "Energy curves are interpolated");
    check(sync.energyAt(100.0).hit > .99f && sync.energyAt(-5.0).low == 0.0f, "Energy is clamped outside the curve");

    std::istringstream broken("bpm 9000\nbeats 1 2 3\n");
    check(presentation::SoundtrackSync::parse(broken).empty(), "An implausible tempo is rejected");
    std::istringstream unsorted("bpm 100\nbeats 3 2 1\n");
    check(presentation::SoundtrackSync::parse(unsorted).empty(), "Unsorted beats are rejected");
    check(presentation::SoundtrackSync::load("definitely/not/here.sync").empty(), "A missing file gives an empty sync");
    check(presentation::SoundtrackSync{}.beatAt(2.0) > 0.0, "An empty sync still counts beats at a default tempo");
}

void shippedSoundtracksHaveSyncFiles() {
    int found = 0;
    for (int i = 1; i <= 5; ++i) {
        const auto path = std::filesystem::path(BJ_ASSET_DIR) / "audio/music" / ("soundtrack_" + std::to_string(i) + ".sync");
        const auto sync = presentation::SoundtrackSync::load(path);
        if (sync.empty()) continue;
        ++found;
        check(sync.bpm() >= 60.0 && sync.bpm() <= 190.0 && sync.duration() > 30.0, "A shipped sync has a believable tempo and length");
        const double middle = sync.duration() / 2;
        const double beatsPerSecond = (sync.beatAt(middle + 10.0) - sync.beatAt(middle)) / 10.0;
        check(std::abs(beatsPerSecond * 60.0 - sync.bpm()) < sync.bpm() * .12, "The beat grid matches the stated tempo");
        const auto loud = sync.energyAt(middle);
        check(loud.low >= 0 && loud.low <= 1 && loud.hit >= 0 && loud.hit <= 1, "Energies stay in range");
    }
    check(found == 5 || found == 0, "Either every soundtrack has a sync file or none is checked");
}

void bandIsWellFormed() {
    bool finite = true, grounded = true, inside = true, valid = true;
    for (double beats = 0; beats < 64; beats += .37)
        for (const float energy : {0.0f, .4f, 1.0f}) {
            const auto parts = pose(beats, energy);
            valid = valid && parts.size() > 300 && parts.size() < 1200;
            for (const auto& part : parts) {
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r) finite = finite && std::isfinite(part.model[c][r]);
                const auto p = centre(part);
                grounded = grounded && p.y > -.1f && p.y < 3.0f;
                inside = inside && p.x > -4.6f && p.x < 4.6f && p.z > -4.6f && p.z < 2.0f;
            }
        }
    check(finite, "Every part has a finite transform at every beat and energy");
    check(valid, "The ensemble has a sensible number of parts");
    check(grounded, "Nothing sinks through the floor or floats above the stage");
    check(inside, "Everyone stays inside the stage area, clear of the wing curtains");
}

void bandIsDeterministicAndAlive() {
    const auto a = pose(13.25), b = pose(13.25);
    bool same = a.size() == b.size();
    for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i].model == b[i].model && a[i].color == b[i].color;
    check(same, "The same beat always gives the same picture");

    const auto later = pose(13.75);
    int moved = 0;
    for (std::size_t i = 0; i < std::min(a.size(), later.size()); ++i)
        if (glm::length(centre(a[i]) - centre(later[i])) > .005f) ++moved;
    check(moved > 150, "Half a beat later many parts have moved (the players are really playing)");

    // A fast tempo plays on every second beat: 160 BPM at beat 8 looks like 80 BPM at beat 4.
    const auto fast = pose(8.0, .8f, 160.0f), half = pose(4.0, .8f, 80.0f);
    bool equal = fast.size() == half.size();
    for (std::size_t i = 0; equal && i < fast.size(); ++i) equal = fast[i].model == half[i].model;
    check(equal, "A tempo above the pulse limit moves on every second beat");
}

void bandIsSmooth() {
    // At 110 BPM one 60 Hz frame is 0.0303 beats; no part may jump visibly between frames.
    float worst = 0;
    double worstBeat = 0;
    int mismatched = 0;
    for (double beats = 8.0; beats < 72.0; beats += 60.0 / 110.0 / 60.0) {
        const auto a = pose(beats), b = pose(beats + 60.0 / 110.0 / 60.0);
        if (a.size() != b.size()) { ++mismatched; continue; }
        for (std::size_t i = 0; i < a.size(); ++i) {
            const float jump = glm::length(centre(a[i]) - centre(b[i]));
            if (jump > worst) { worst = jump; worstBeat = beats; }
        }
    }
    if (worst > .12f) std::cerr << "worst per-frame jump " << worst << " m at beat " << worstBeat << '\n';
    check(worst < .12f, "No part teleports between two 60 Hz frames");
    check(mismatched < 40, "The part list stays stable from frame to frame");
}

void sleevesRemainReachable() {
    // The tailored upper/lower sleeves have fixed cross-sections. Inspect the rendered geometry,
    // not a duplicate IK calculation: an unreachable instrument grip must not stretch a forearm.
    float longest = 0;
    double worstBeat = 0;
    glm::vec3 longestCentre(0);
    bool complete = true;
    for (const float bpm : {78.95f, 157.89f})
        for (const float energy : {.05f, .8f, .95f})
            for (double beats = 0; beats < 64; beats += .13) {
                const auto parts = pose(beats, energy, bpm);
                int sleeves = 0;
                for (const auto& part : parts) {
                    if (part.shape != gfx::band::Shape::Cylinder) continue;
                    const float x = glm::length(glm::vec3(part.model[0]));
                    const float z = glm::length(glm::vec3(part.model[2]));
                    if (std::abs(x - .034f) > .00001f || std::abs(z - .034f) > .00001f) continue;
                    ++sleeves;
                    const float length = glm::length(glm::vec3(part.model[1]));
                    if (length > longest) { longest = length; worstBeat = beats; longestCentre = centre(part); }
                }
                complete = complete && sleeves == 10;
            }
    if (longest > .265f) std::cerr << "Longest forearm sleeve " << longest << " m at beat " << worstBeat
        << " centre " << longestCentre.x << ',' << longestCentre.y << ',' << longestCentre.z << '\n';
    check(complete, "All ten forearm sleeves remain present through the performance");
    check(longest <= .265f, "Instrument targets stay within adult arm reach without stretching the forearm");
}

void swingBoundariesStayContinuous() {
    bool finite = true, stable = true;
    float worst = 0;
    glm::vec3 worstCentre(0);
    double worstAt = 0;
    for (const float bpm : {78.95f, 103.45f, 130.43f, 157.89f})
        for (const float energy : {.05f, .95f})
            for (int beat = 0; beat < 32; ++beat)
                for (const double offset : {0.0, 2.0 / 3.0}) {
                    const double at = (beat + offset) * (bpm > gfx::band::maxPulseBpm ? 2.0 : 1.0);
                    const auto before = pose(at - .0001, energy, bpm), after = pose(at + .0001, energy, bpm);
                    stable = stable && before.size() == after.size();
                    for (std::size_t i = 0; i < std::min(before.size(), after.size()); ++i) {
                        const float jump = glm::length(centre(after[i]) - centre(before[i]));
                        finite = finite && std::isfinite(jump);
                        if (jump > worst) { worst = jump; worstCentre = centre(after[i]); worstAt = at; }
                    }
                }
    if (worst > .012f) std::cerr << "Swing boundary jump " << worst << " m at " << worstAt
        << " centre " << worstCentre.x << ',' << worstCentre.y << ',' << worstCentre.z << '\n';
    check(finite && stable, "Quiet and loud performances keep a finite stable rig across swing and bar boundaries");
    check(worst < .012f, "Fingers, valves and wrists do not jump when a swung note changes");
}

void intensityFollowsTheMusic() {
    using gfx::band::Musician;
    Playback quiet, loud;
    quiet.playing = loud.playing = true;
    quiet.low = quiet.mid = quiet.high = quiet.hit = 0.05f;
    loud.low = loud.mid = loud.high = loud.hit = 0.95f;
    Playback idle;
    for (const auto who : {Musician::Pianist, Musician::Guitarist, Musician::Drummer, Musician::Trumpeter, Musician::Saxophonist}) {
        const float i0 = gfx::band::intensity(who, idle), i1 = gfx::band::intensity(who, quiet), i2 = gfx::band::intensity(who, loud);
        check(i0 < i1 && i1 < i2 && i2 <= 1.0f && i0 >= 0.0f, "Each player works harder as the music gets louder, and rests between numbers");
    }
    // The drummer follows attacks, the horns follow the high register.
    Playback attacks, bright;
    attacks.playing = bright.playing = true;
    attacks.hit = 1.0f; attacks.low = attacks.mid = attacks.high = 0.1f;
    bright.high = 1.0f; bright.low = bright.mid = bright.hit = 0.1f;
    check(gfx::band::intensity(Musician::Drummer, attacks) > gfx::band::intensity(Musician::Drummer, bright), "The drummer reacts to attacks");
    check(gfx::band::intensity(Musician::Trumpeter, bright) > gfx::band::intensity(Musician::Trumpeter, attacks), "The trumpeter reacts to the high register");
}
} // namespace

int main() {
    syncParsesAndFollowsBeats();
    shippedSoundtracksHaveSyncFiles();
    bandIsWellFormed();
    bandIsDeterministicAndAlive();
    bandIsSmooth();
    sleevesRemainReachable();
    swingBoundariesStayContinuous();
    intensityFollowsTheMusic();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
