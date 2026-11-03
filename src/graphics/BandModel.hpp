#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace gfx::band {

// The jazz ensemble is plain geometry: a flat list of primitives with a model matrix and a surface.
// Nothing here touches OpenGL, so the pose and animation logic is deterministic and testable; the
// renderer draws the list with its cube/sphere/cylinder meshes.
enum class Shape { Cube, Sphere, Cylinder }; // Unit cube, unit sphere, unit-radius cylinder of height 1 along Y.

struct Part {
    Shape shape = Shape::Cube;
    glm::mat4 model{1.0f}; // Band space (metres, +Y up, the audience is toward +Z, the floor is Y = 0).
    glm::vec3 color{1.0f};
    float specular = 0.08f;
    float shininess = 32.0f;
    float emission = 0.0f;
};

enum class Musician { Pianist, Guitarist, Drummer, Trumpeter, Saxophonist };
inline constexpr int musicianCount = 5;

// What the song is doing right now.
struct Playback {
    bool playing = false;  // False: the band idles between numbers.
    double beats = 0.0;    // Continuous beat count; integer part 0 mod 4 is a bar line.
    float bpm = 110.0f;
    float low = 0.5f, mid = 0.5f, high = 0.5f, hit = 0.5f; // Band energies, 0..1.
    float time = 0.0f;     // Seconds, for slow idle motion.
};

// Appends the whole ensemble (people, instruments, amp, stands) for this instant.
void build(const Playback& playback, std::vector<Part>& out);

// How hard each musician is playing, 0 (resting) to 1 (flat out), derived from the energies.
float intensity(Musician who, const Playback& playback);

// A tempo above this plays on every second beat, so the movement stays human.
inline constexpr float maxPulseBpm = 140.0f;

} // namespace gfx::band
