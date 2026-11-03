#include "graphics/BandModel.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gfx::band {
namespace {

using glm::mat3;
using glm::mat4;
using glm::vec3;

constexpr float pi = 3.14159265358979f;

// ---------------------------------------------------------------- small math helpers

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float smooth(float v) { v = clamp01(v); return v * v * (3.0f - 2.0f * v); }
float mixf(float a, float b, float t) { return a + (b - a) * t; }
float frac(double v) { return static_cast<float>(v - std::floor(v)); }

float hash(std::uint32_t a, std::uint32_t b = 0) {
    std::uint32_t n = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu;
    n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; n ^= n >> 16;
    return static_cast<float>(n & 0xffffffu) / 16777216.0f;
}

mat3 rotX(float a) { return mat3(glm::rotate(mat4(1), a, vec3(1, 0, 0))); }
mat3 rotY(float a) { return mat3(glm::rotate(mat4(1), a, vec3(0, 1, 0))); }
mat3 rotZ(float a) { return mat3(glm::rotate(mat4(1), a, vec3(0, 0, 1))); }

// Orthonormal frame whose Z axis points along `z` and whose Y axis leans toward `up`.
mat3 basisZ(vec3 z, vec3 up) {
    z = glm::normalize(z);
    vec3 x = glm::cross(up, z);
    if (glm::length(x) < 1e-4f) x = glm::cross(vec3(1, 0, 0), z);
    x = glm::normalize(x);
    return mat3(x, glm::cross(z, x), z);
}

mat4 alignY(vec3 direction) {
    const float length = glm::length(direction);
    if (length < 1e-5f) return mat4(1);
    direction /= length;
    const vec3 axis = glm::cross(vec3(0, 1, 0), direction);
    if (glm::length(axis) > 1e-4f)
        return glm::rotate(mat4(1), std::acos(std::clamp(direction.y, -1.0f, 1.0f)), glm::normalize(axis));
    return direction.y < 0 ? glm::rotate(mat4(1), pi, vec3(1, 0, 0)) : mat4(1);
}

// Swing: the second eighth of a beat lands two thirds of the way through it.
float swingPhase(float f) { return f < 2.0f / 3.0f ? f * .75f : .5f + (f - 2.0f / 3.0f) * 1.5f; }

// Two-bone limb: where the middle joint (elbow or knee) lies when the end reaches `target`.
vec3 solveLimb(vec3 root, vec3 target, float upper, float lower, vec3 pole) {
    vec3 delta = target - root;
    float distance = glm::length(delta);
    if (distance < 1e-4f) return root + vec3(0, -upper, 0);
    const float reach = std::clamp(distance, std::abs(upper - lower) + .01f, upper + lower - .005f);
    const vec3 along = delta / distance;
    const float a = (upper * upper - lower * lower + reach * reach) / (2.0f * reach);
    const float h = std::sqrt(std::max(upper * upper - a * a, 0.0f));
    vec3 bend = pole - along * glm::dot(pole, along);
    bend = glm::length(bend) < 1e-4f ? glm::cross(along, vec3(1, 0, 0)) : glm::normalize(bend);
    return root + along * a + bend * h;
}

// ---------------------------------------------------------------- surfaces and the part builder

struct Surface { vec3 color; float specular = .08f, shininess = 32.0f, emission = 0.0f; };

const Surface suit{{.034f, .032f, .042f}, .16f, 40};
const Surface suitEdge{{.060f, .056f, .072f}, .22f, 48};
const Surface shirt{{.80f, .79f, .75f}, .05f, 16};
const Surface shoe{{.020f, .019f, .024f}, .55f, 90};
const Surface hatFelt{{.030f, .029f, .036f}, .12f, 30};
const Surface hatBand{{.10f, .095f, .11f}, .25f, 40};
const Surface brass{{.74f, .47f, .11f}, .95f, 130};
const Surface brassDark{{.52f, .31f, .07f}, .85f, 90};
const Surface chrome{{.56f, .57f, .60f}, .95f, 110};
const Surface blackGloss{{.010f, .010f, .013f}, .70f, 120};
const Surface ivory{{.80f, .77f, .68f}, .30f, 50};
const Surface keyBlack{{.012f, .012f, .015f}, .45f, 80};
const Surface woodDark{{.17f, .085f, .045f}, .35f, 50};
const Surface rosewood{{.075f, .035f, .025f}, .25f, 40};
const Surface guitarRed{{.33f, .035f, .030f}, .75f, 110};
const Surface cream{{.78f, .72f, .55f}, .30f, 40};
const Surface drumShell{{.20f, .018f, .020f}, .70f, 100};
const Surface drumHead{{.72f, .70f, .64f}, .20f, 30};
const Surface cymbal{{.80f, .60f, .20f}, .95f, 120};
const Surface grill{{.12f, .115f, .105f}, .05f, 8};
const Surface stand{{.30f, .30f, .32f}, .85f, 90};

struct Builder {
    std::vector<Part>& out;

    void add(Shape shape, const mat4& model, const Surface& surface) {
        Part part;
        part.shape = shape;
        part.model = model;
        part.color = surface.color;
        part.specular = surface.specular;
        part.shininess = surface.shininess;
        part.emission = surface.emission;
        out.push_back(part);
    }
    void cube(vec3 center, vec3 size, const Surface& surface, const mat3& rotation = mat3(1)) {
        add(Shape::Cube, glm::scale(glm::translate(mat4(1), center) * mat4(rotation), size), surface);
    }
    void ball(vec3 center, vec3 radii, const Surface& surface, const mat3& rotation = mat3(1)) {
        add(Shape::Sphere, glm::scale(glm::translate(mat4(1), center) * mat4(rotation), radii), surface);
    }
    // Upright elliptical cylinder.
    void drum(vec3 center, float radiusX, float height, float radiusZ, const Surface& surface,
              const mat3& rotation = mat3(1)) {
        add(Shape::Cylinder, glm::scale(glm::translate(mat4(1), center) * mat4(rotation), {radiusX, height, radiusZ}), surface);
    }
    // Round tube between two points.
    void rod(vec3 from, vec3 to, float radius, const Surface& surface) {
        const float length = glm::length(to - from);
        if (length < 1e-5f) return;
        add(Shape::Cylinder,
            glm::scale(glm::translate(mat4(1), (from + to) * .5f) * alignY(to - from), {radius, length, radius}), surface);
    }
    // Round plate whose axis points along `axis` (cymbals, drum heads, bell rings).
    void disc(vec3 center, vec3 axis, float radius, float thickness, const Surface& surface) {
        add(Shape::Cylinder, glm::scale(glm::translate(mat4(1), center) * alignY(axis), {radius, thickness, radius}), surface);
    }
    // Tapered tube from a polyline: radius changes along the path, joints are rounded.
    void tube(const std::vector<vec3>& path, const std::vector<float>& radius, const Surface& surface) {
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            rod(path[i], path[i + 1], (radius[i] + radius[i + 1]) * .5f, surface);
            ball(path[i + 1], vec3(radius[i + 1] * 1.02f), surface);
        }
    }
};

// A placed coordinate system: converts local points to band space.
struct Frame {
    vec3 origin{0};
    mat3 rotation{1};
    vec3 at(vec3 local) const { return origin + rotation * local; }
    vec3 dir(vec3 local) const { return rotation * local; }
};

// ---------------------------------------------------------------- the people

struct Look {
    vec3 skin;
    vec3 hair;
    bool hat = false;
    float hatTilt = 0.0f;
};

struct Pose {
    vec3 pelvis{0};
    float yaw = 0, lean = 0, twist = 0, roll = 0;   // Body facing and torso bend (lean > 0 bends forward).
    float nod = 0, headYaw = 0, headTilt = 0;       // Head relative to the torso (nod > 0 looks down).
    std::array<vec3, 2> hand{};                     // Index 0 = the person's right hand, 1 = left.
    std::array<vec3, 2> palm{vec3(0, 1, 0), vec3(0, 1, 0)}; // Direction the back of the hand faces.
    std::array<vec3, 2> foot{};
    vec3 elbowPole[2] = {vec3(-.6f, -1, -.3f), vec3(.6f, -1, -.3f)};
    bool seated = false;
};

struct Joints {
    mat3 torso{1}, head{1};
    vec3 pelvis{0}, neck{0}, headCenter{0};
    std::array<vec3, 2> shoulder{}, elbow{}, wrist{};
};

constexpr float upperArm = .30f, foreArm = .27f, thigh = .45f, shin = .44f;

// The torso orientation a pose will have, so instruments can be attached before the person is drawn.
mat3 torsoOf(const Pose& pose) { return rotY(pose.yaw) * rotY(pose.twist) * rotX(pose.lean) * rotZ(pose.roll); }
mat3 headOf(const Pose& pose) { return torsoOf(pose) * rotY(pose.headYaw) * rotX(pose.nod) * rotZ(pose.headTilt); }

Joints drawPerson(Builder& b, const Pose& pose, const Look& look) {
    Joints j;
    const mat3 body = rotY(pose.yaw);
    j.torso = torsoOf(pose);
    j.head = headOf(pose);
    j.pelvis = pose.pelvis;
    const vec3 p = pose.pelvis;
    j.neck = p + j.torso * vec3(0, .58f, .01f);
    j.headCenter = j.neck + j.head * vec3(0, .15f, .015f);

    // Jacket and shirt.
    b.ball(p + body * vec3(0, -.03f, 0), {.19f, .13f, .125f}, suit, body);
    b.ball(p + j.torso * vec3(0, .17f, 0), {.165f, .19f, .115f}, suit, j.torso);
    b.ball(p + j.torso * vec3(0, .40f, 0), {.215f, .22f, .13f}, suit, j.torso);
    b.cube(p + j.torso * vec3(0, .40f, .121f), {.075f, .30f, .014f}, shirt, j.torso);
    for (const float s : {-1.0f, 1.0f})
        b.cube(p + j.torso * vec3(s * .062f, .41f, .124f), {.045f, .28f, .014f}, suitEdge, j.torso * rotZ(s * -.16f));
    b.ball(j.neck - j.torso * vec3(0, .035f, 0), {.07f, .035f, .07f}, shirt, j.torso);
    b.cube(p + j.torso * vec3(0, .43f, .131f), {.028f, .12f, .008f}, suitEdge, j.torso); // Dark tie.

    // Neck, head, face and hair.
    const vec3 skinBase = look.skin;
    const Surface skin{skinBase, .12f, 24};
    const Surface hair{look.hair, .20f, 30};
    b.rod(p + j.torso * vec3(0, .53f, 0), j.headCenter - j.head * vec3(0, .09f, 0), .042f, skin);
    b.ball(j.headCenter, {.092f, .118f, .10f}, skin, j.head);
    for (const float s : {-1.0f, 1.0f}) {
        b.ball(j.headCenter + j.head * vec3(s * .094f, -.005f, -.005f), {.014f, .03f, .02f}, skin, j.head);
        b.ball(j.headCenter + j.head * vec3(s * .036f, .02f, .088f), {.012f, .008f, .008f}, shoe, j.head);
    }
    b.ball(j.headCenter + j.head * vec3(0, -.012f, .1f), {.016f, .02f, .02f}, skin, j.head);
    b.cube(j.headCenter + j.head * vec3(0, -.052f, .092f), {.04f, .006f, .005f}, Surface{{.12f, .04f, .04f}}, j.head);
    b.ball(j.headCenter + j.head * vec3(0, .035f, -.028f), {.098f, .092f, .098f}, hair, j.head);
    if (look.hat) {
        const mat3 hatFrame = j.head * rotZ(look.hatTilt);
        const vec3 crown = j.headCenter + hatFrame * vec3(0, .075f, 0);
        b.disc(crown, hatFrame * vec3(0, 1, 0), .175f, .012f, hatFelt);
        b.disc(crown + hatFrame * vec3(0, .055f, 0), hatFrame * vec3(0, 1, 0), .098f, .105f, hatFelt);
        b.disc(crown + hatFrame * vec3(0, .02f, 0), hatFrame * vec3(0, 1, 0), .101f, .024f, hatBand);
    }

    // Arms.
    for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? -1.0f : 1.0f;
        j.shoulder[i] = p + j.torso * vec3(side * .205f, .50f, 0);
        j.elbow[i] = solveLimb(j.shoulder[i], pose.hand[i], upperArm, foreArm, body * pose.elbowPole[i]);
        const vec3 forearm = glm::normalize(pose.hand[i] - j.elbow[i]);
        j.wrist[i] = j.elbow[i] + forearm * foreArm;
        b.ball(j.shoulder[i], vec3(.07f), suit);
        b.rod(j.shoulder[i], j.elbow[i], .053f, suit);
        b.ball(j.elbow[i], vec3(.051f), suit);
        b.rod(j.elbow[i], j.wrist[i] - forearm * .03f, .044f, suit);
        b.rod(j.wrist[i] - forearm * .035f, j.wrist[i] + forearm * .005f, .047f, shirt);
        b.ball(j.wrist[i] + forearm * .05f, {.036f, .021f, .07f}, skin, basisZ(forearm, pose.palm[i]));
    }

    // Legs and shoes.
    for (int i = 0; i < 2; ++i) {
        const float side = i == 0 ? -1.0f : 1.0f;
        const vec3 hip = p + body * vec3(side * .095f, -.06f, 0);
        const vec3 ankle = pose.foot[i] + vec3(0, .075f, 0);
        const vec3 knee = solveLimb(hip, ankle, thigh, shin, body * vec3(side * .12f, .25f, 1));
        const vec3 leg = glm::normalize(ankle - knee);
        b.rod(hip, knee, .082f, suit);
        b.ball(knee, vec3(.075f), suit);
        b.rod(knee, ankle + leg * .03f, .062f, suit);
        b.ball(pose.foot[i] + body * vec3(0, .045f, .075f), {.052f, .046f, .125f}, shoe, body);
    }
    return j;
}

const Look looks[musicianCount] = {
    {{.55f, .34f, .24f}, {.020f, .014f, .012f}, false, 0},      // Pianist
    {{.33f, .19f, .13f}, {.030f, .020f, .014f}, true, .07f},    // Guitarist
    {{.60f, .39f, .29f}, {.075f, .045f, .025f}, false, 0},      // Drummer
    {{.14f, .075f, .055f}, {.010f, .008f, .008f}, true, -.09f}, // Trumpeter
    {{.48f, .29f, .20f}, {.040f, .040f, .045f}, true, .05f},    // Saxophonist
};

// ---------------------------------------------------------------- shared playback maths

struct Beat {
    double now;     // Pulses since a bar line (a pulse is a beat, or two beats at high tempo).
    float bar;      // Whole bars so far.
    float inBar;    // 0..4.
    float act;      // Playing intensity.
    bool playing;
    float time;
};

// A strike: reaches full strength within ~25 ms (never instantly, so nothing teleports), then decays.
float hitDecay(float since, float rate) {
    return since < 0 ? 0.0f : smooth(since / .04f) * std::exp(-since * rate);
}

struct Stroke { float beat; vec3 target; };

// Moves along a looping list of strokes: touching each target at its beat and rising in between.
struct Path { vec3 position; int index; float since; float length; };

Path followStrokes(const std::vector<Stroke>& strokes, float inBar, float barLength, float arc) {
    const int n = static_cast<int>(strokes.size());
    int last = n - 1;
    for (int i = 0; i < n; ++i)
        if (strokes[static_cast<std::size_t>(i)].beat <= inBar) last = i;
    const int next = (last + 1) % n;
    const auto& a = strokes[static_cast<std::size_t>(last)];
    const auto& c = strokes[static_cast<std::size_t>(next)];
    float start = a.beat, end = c.beat;
    if (end <= start) end += barLength;
    float t = inBar < start ? inBar + barLength : inBar;
    const float length = end - start;
    const float u = clamp01((t - start) / std::max(length, 1e-3f));
    vec3 position = glm::mix(a.target, c.target, smooth(u));
    position.y += std::sin(pi * u) * std::min(arc, .06f + .05f * length * arc / .22f);
    return {position, last, t - start, length};
}


// ---------------------------------------------------------------- pianist and grand piano

void pianist(Builder& b, const Beat& t) {
    // Local frame: origin at the keyboard's front edge on the floor, +Z into the piano, +X to the
    // player's left (bass). The frame is turned so the pianist looks toward the rest of the band.
    const Frame F{{-3.35f, 0, -0.55f}, rotY(pi / 2)};
    constexpr int whites = 52;
    constexpr float keyWidth = 1.22f / whites;
    const auto keyX = [&](int i) { return .61f - (static_cast<float>(i) + .5f) * keyWidth; };
    std::array<float, whites> press{};

    // Eighth-note grid with swing: event k starts at segment(k) beats.
    const long k = static_cast<long>(std::floor(t.now)) * 2 + (frac(t.now) >= 2.0f / 3.0f ? 1 : 0);
    const auto segment = [](long n) { return static_cast<float>(n >> 1) + ((n & 1) ? 2.0f / 3.0f : 0.0f); };
    const float act = t.act;

    const auto plays = [&](long n, std::uint32_t salt) {
        return hash(static_cast<std::uint32_t>(n), salt) < .30f + .50f * act + ((n & 1) ? 0.0f : .20f);
    };
    const auto melody = [&](long n) {
        static const int steps[4] = {0, 2, 1, 3};
        const std::uint32_t group = static_cast<std::uint32_t>(n >> 2);
        const int base = 33 + static_cast<int>(hash(group, 1) * 9);
        const int direction = hash(group, 2) > .5f ? 1 : -1;
        return std::clamp(base + direction * steps[n & 3], 24, 49);
    };
    // Right hand: the note currently held and the one before it.
    long lastPlayed = k;
    while (lastPlayed > k - 40 && !plays(lastPlayed, 3)) --lastPlayed;
    const bool strikes = plays(k, 3);
    const int note = melody(lastPlayed);
    long before = lastPlayed - 1;
    while (before > lastPlayed - 40 && !plays(before, 3)) --before;
    const int previous = melody(before);
    const float sinceNote = static_cast<float>(t.now) - segment(lastPlayed);
    const float glide = smooth(sinceNote / .22f);
    const float rightX = mixf(keyX(previous), keyX(note), glide);
    const float rightDip = hitDecay(sinceNote, 7.0f) * (strikes || sinceNote < .5f ? 1.0f : .6f);
    const float held = sinceNote < .30f ? 1.0f : std::exp(-(sinceNote - .30f) * 12.0f);
    press[static_cast<std::size_t>(note)] = std::max(press[static_cast<std::size_t>(note)], held * act);

    // Left hand: a three-note chord on beats 1, the "and" of 2 and 3, changing every two bars.
    static const float chordBeats[3] = {0.0f, 1.0f + 2.0f / 3.0f, 3.0f};
    int chord = 0;
    for (int i = 0; i < 3; ++i)
        if (chordBeats[i] <= t.inBar && hash(static_cast<std::uint32_t>(t.bar * 3 + static_cast<float>(i)), 7) < .45f + .5f * act) chord = i + 1;
    if (chord == 0) chord = 1;
    const auto chordRoot = [&](long bar) { return 12 + static_cast<int>(hash(static_cast<std::uint32_t>(bar / 2), 5) * 8); };
    const int root = chordRoot(static_cast<long>(t.bar));
    const int rootBefore = chordRoot(static_cast<long>(t.bar) - ((static_cast<long>(t.bar) & 1) ? 0 : 1));
    const float sinceChord = t.inBar - chordBeats[chord - 1];
    const float leftX = mixf(keyX(rootBefore + 2), keyX(root + 2), smooth(t.inBar / .6f));
    const float leftDip = hitDecay(sinceChord, 6.0f);
    const float leftHeld = sinceChord < .35f ? 1.0f : std::exp(-(sinceChord - .35f) * 8.0f);
    for (int n : {root, root + 2, root + 4})
        press[static_cast<std::size_t>(std::clamp(n, 0, whites - 1))] =
            std::max(press[static_cast<std::size_t>(std::clamp(n, 0, whites - 1))], leftHeld * act);

    // Keyboard.
    for (int i = 0; i < whites; ++i)
        b.cube(F.at({keyX(i), .712f - press[static_cast<std::size_t>(i)] * .012f, .085f}), {keyWidth * .92f, .026f, .15f}, ivory, F.rotation);
    for (int i = 0; i < whites - 1; ++i) {
        const int letter = i % 7;
        if (letter == 1 || letter == 4) continue; // No black key after B or E.
        const float depth = press[static_cast<std::size_t>(i)] * .006f;
        b.cube(F.at({keyX(i) - keyWidth * .5f, .742f - depth, .115f}), {keyWidth * .55f, .022f, .092f}, keyBlack, F.rotation);
    }
    b.cube(F.at({0, .70f, .08f}), {1.26f, .03f, .16f}, keyBlack, F.rotation); // Key bed.

    // Case: a rectangle for the front, a wide ellipse for the curved tail.
    b.cube(F.at({0, .80f, .60f}), {1.44f, .36f, .88f}, blackGloss, F.rotation);
    b.drum(F.at({0, .80f, 1.0f}), .72f, .36f, 1.26f, blackGloss, F.rotation);
    b.cube(F.at({0, .835f, .17f}), {1.30f, .13f, .03f}, blackGloss, F.rotation); // Fallboard.
    b.cube(F.at({0, 1.06f, .31f}), {.62f, .30f, .012f}, blackGloss, F.rotation * rotX(-.30f)); // Music desk.
    // Lid propped open on the treble side, facing the audience.
    {
        const mat3 tilt = rotZ(.72f);
        const vec3 hinge(.72f, .985f, 0);
        b.cube(F.at(hinge + tilt * vec3(-.70f, .012f, 0) + vec3(0, 0, 1.22f)), {1.40f, .022f, 2.0f}, blackGloss, F.rotation * tilt);
        b.rod(F.at({-.38f, .98f, 1.35f}), F.at({-.38f, 1.22f, 1.35f}), .012f, chrome); // Prop stick.
    }
    for (const auto& leg : {vec3(.62f, 0, .35f), vec3(-.62f, 0, .35f), vec3(0, 0, 1.95f)})
        b.rod(F.at(leg), F.at(leg + vec3(0, .66f, 0)), .045f, blackGloss);
    const float pump = hitDecay(std::fmod(static_cast<float>(t.now), 2.0f), 3.5f) * act;
    for (const float x : {-.10f, 0.0f, .10f}) // Pedals; the right one is the sustain pedal.
        b.cube(F.at({x, .045f - (x < 0 ? pump * .02f : 0), .30f}), {.07f, .014f, .12f}, brassDark, F.rotation);
    b.rod(F.at({-.12f, .05f, .30f}), F.at({-.12f, .64f, .30f}), .012f, brassDark);
    b.rod(F.at({.12f, .05f, .30f}), F.at({.12f, .64f, .30f}), .012f, brassDark);
    // Bench.
    b.cube(F.at({0, .47f, -.44f}), {.64f, .07f, .34f}, rosewood, F.rotation);
    for (const float x : {-.27f, .27f})
        for (const float z : {-.57f, -.31f}) b.rod(F.at({x, 0, z}), F.at({x, .44f, z}), .02f, stand);

    // The pianist.
    Pose pose;
    pose.seated = true;
    pose.pelvis = F.at({0, .56f, -.44f});
    pose.yaw = pi / 2;
    pose.lean = .15f + .05f * std::sin(pi * t.inBar * .5f) * act + .03f * rightDip * act;
    pose.roll = .035f * std::sin(pi * t.bar * .5f + .7f) * act;
    pose.twist = (rightX - leftX) * .10f;
    pose.nod = .12f + .06f * std::sin(2.0f * pi * static_cast<float>(t.now)) * act;
    pose.headYaw = (t.playing ? .38f : .12f) * std::sin(t.time * .45f);
    pose.headTilt = .05f * std::sin(t.time * .3f);
    const float hover = .83f - .035f * act;
    pose.hand[0] = F.at({rightX, hover - .05f * rightDip * act, .05f});
    pose.hand[1] = F.at({leftX, hover - .05f * leftDip * act, .05f});
    pose.palm = {F.dir({0, 1, 0}), F.dir({0, 1, 0})};
    pose.foot[0] = F.at({-.10f, pump * .03f, .20f});
    pose.foot[1] = F.at({.14f, 0, .06f});
    drawPerson(b, pose, looks[0]);
}


// ---------------------------------------------------------------- guitarist, guitar and amp

void guitarist(Builder& b, const Beat& t) {
    const Frame F{{-0.68f, 0, 0.60f}, rotY(.14f)};
    const float act = t.act;
    const float beatPhase = frac(t.now);
    const float strumPhase = swingPhase(beatPhase) * 2.0f; // Two strums per beat, swung.

    Pose pose;
    pose.pelvis = F.at({0, .93f - .025f * hitDecay(beatPhase, 5.0f) * act, 0});
    pose.yaw = .14f;
    pose.lean = .05f + .03f * std::sin(pi * t.inBar * .5f) * act;
    pose.roll = .06f * std::sin(pi * static_cast<float>(t.now) * .25f) * act;
    pose.twist = .05f * std::sin(pi * t.bar * .5f) * act;
    pose.nod = .10f + .05f * std::sin(2.0f * pi * static_cast<float>(t.now)) * act;
    pose.headYaw = (t.playing ? .30f : .08f) * std::sin(t.time * .35f + 1.0f);
    pose.headTilt = .04f * std::sin(t.time * .5f);
    const mat3 T = torsoOf(pose);

    // The guitar hangs at the hip on a strap, neck pointing up and out to the player's left.
    const Frame G{pose.pelvis + T * vec3(-.07f, .13f, .20f), T * rotZ(.40f) * rotY(-.22f)};
    const auto g = [&](vec3 v) { return G.at(v); };
    b.ball(g({-.04f, 0, 0}), {.20f, .165f, .026f}, guitarRed, G.rotation);
    b.ball(g({.105f, .118f, 0}), {.072f, .056f, .025f}, guitarRed, G.rotation);
    b.ball(g({.105f, -.118f, 0}), {.072f, .056f, .025f}, guitarRed, G.rotation);
    b.ball(g({-.03f, -.01f, .027f}), {.125f, .105f, .004f}, cream, G.rotation);
    for (const float x : {.07f, -.01f}) b.cube(g({x, 0, .034f}), {.026f, .078f, .012f}, chrome, G.rotation);
    b.cube(g({-.115f, 0, .034f}), {.030f, .092f, .010f}, chrome, G.rotation);
    for (const float y : {.075f, .04f}) b.disc(g({-.14f, y, .034f}), G.dir({0, 0, 1}), .014f, .016f, keyBlack);
    b.cube(g({.48f, 0, .008f}), {.64f, .050f, .022f}, rosewood, G.rotation);
    b.cube(g({.48f, 0, .021f}), {.64f, .044f, .006f}, keyBlack, G.rotation);
    for (int n = 1; n <= 14; ++n) {
        const float x = .80f - .62f * (1.0f - std::pow(2.0f, -static_cast<float>(n) / 12.0f));
        b.cube(g({x, 0, .025f}), {.0035f, .046f, .004f}, chrome, G.rotation);
    }
    b.cube(g({.885f, 0, .004f}), {.15f, .068f, .018f}, rosewood, G.rotation);
    for (int i = 0; i < 6; ++i) {
        const float y = (static_cast<float>(i) - 2.5f) * .0085f;
        b.rod(g({-.115f, y, .036f}), g({.80f, y, .027f}), .0011f, chrome);
        b.ball(g({.84f + (i % 3) * .035f, (i < 3 ? 1.0f : -1.0f) * .042f, .004f}), vec3(.008f), chrome);
    }
    // Strap over the player's left shoulder.
    const vec3 strapTop = pose.pelvis + T * vec3(.12f, .55f, -.01f);
    b.rod(g({.12f, .16f, 0}), strapTop, .012f, shoe);
    b.rod(g({-.22f, -.02f, 0}), strapTop + T * vec3(0, 0, -.06f), .012f, shoe);

    // Fretting hand slides along the neck between chord shapes; picking hand strums across the strings.
    const auto position = [](long bar) { return .30f + .34f * hash(static_cast<std::uint32_t>(bar / 2), 11); };
    const long bar = static_cast<long>(t.bar);
    const float slide = smooth((t.inBar + ((bar & 1) ? 4.0f : 0.0f)) / 1.2f);
    const float s = mixf(position(bar - ((bar & 1) ? 0 : 1)), position(bar), slide)
        + .012f * std::sin(t.time * 6.0f) * act;
    const float press = .5f + .5f * std::cos(2.0f * pi * strumPhase);
    pose.hand[1] = g({s + .02f, -.012f, -.012f});
    pose.palm[1] = G.dir({0, 0, -1});
    const float strum = std::cos(2.0f * pi * strumPhase) * (.025f + .055f * act);
    pose.hand[0] = g({-.095f, strum, .075f});
    pose.palm[0] = G.dir({0, 0, 1});
    pose.elbowPole[1] = vec3(.5f, -1, -.2f);
    for (int i = 0; i < 3; ++i) // Fingertips on the strings.
        b.ball(g({s + .035f - i * .028f, (i - 1) * .011f, .034f - press * .001f}), vec3(.0075f), Surface{looks[1].skin, .1f, 20});
    pose.foot[0] = F.at({-.15f, 0, .06f});
    pose.foot[1] = F.at({.22f, .035f * hitDecay(beatPhase, 6.0f) * (act > .35f ? 1.0f : 0.0f), -.05f});
    drawPerson(b, pose, looks[1]);

    // Combo amplifier behind, with its cable.
    const vec3 amp(-.62f, 0, -.85f);
    b.cube(amp + vec3(0, .25f, 0), {.56f, .50f, .27f}, suit);
    b.cube(amp + vec3(0, .235f, .137f), {.46f, .36f, .012f}, grill);
    b.cube(amp + vec3(0, .465f, .137f), {.50f, .05f, .012f}, chrome);
    for (int i = 0; i < 5; ++i) b.ball(amp + vec3(-.19f + i * .095f, .465f, .147f), vec3(.012f), brassDark);
    b.rod(g({-.22f, -.1f, 0}), vec3(-.52f, .06f, .30f), .006f, shoe);
    b.rod(vec3(-.52f, .06f, .30f), vec3(-.56f, .01f, -.4f), .006f, shoe);
}


// ---------------------------------------------------------------- drummer and drum kit

void drummer(Builder& b, const Beat& t) {
    const vec3 O(0.42f, 0, -2.95f); // Stool position; the drummer faces the audience (+Z).
    const auto at = [&](vec3 v) { return O + v; };
    const float act = t.act;
    const float beatPhase = frac(t.now);
    const long bar = static_cast<long>(t.bar);

    // Surfaces to hit, in kit coordinates.
    const vec3 ride(-.55f, 1.16f, .92f), snare(.0f, .815f, .62f), tom1(-.15f, 1.05f, .93f), tom2(.17f, 1.055f, .93f);
    const vec3 floorTom(-.66f, .645f, .52f), hatTop(.55f, 1.0f, .48f), crash(.62f, 1.31f, .84f);
    const float swing = 1.0f + 2.0f / 3.0f;

    const bool fill = (bar & 3) == 3 && hash(static_cast<std::uint32_t>(bar), 21) < .45f + .5f * act;
    std::vector<Stroke> right, left;
    if (fill) {
        right = {{0, ride}, {1, ride}, {swing, ride}, {2, ride}, {3, tom1}, {3.5f, tom2}, {3.75f, floorTom}};
        left = {{0, snare}, {1, hatTop}, {2.5f, snare}, {2.75f, snare}, {3.25f, snare}, {3.625f, snare}};
    } else {
        right = {{0, ride}, {1, ride}, {swing, ride}, {2, ride}, {3, ride}, {3.0f + 2.0f / 3.0f, ride}};
        left = {{0, snare}, {1, hatTop}, {swing, snare}, {3, hatTop}, {3.0f + 2.0f / 3.0f, snare}};
        if (hash(static_cast<std::uint32_t>(bar), 22) < .55f) left.insert(left.begin() + 3, Stroke{2.0f + 2.0f / 3.0f, snare});
        if (act > .7f && hash(static_cast<std::uint32_t>(bar), 23) < .4f) right.back() = {3.0f + 2.0f / 3.0f, crash};
    }
    const float reach = t.playing ? 1.0f : .0f;
    const Path r = followStrokes(right, t.inBar, 4.0f, .09f + .13f * act);
    const Path l = followStrokes(left, t.inBar, 4.0f, .08f + .12f * act);
    const vec3 stickDir = glm::normalize(vec3(0, .26f, -.965f));
    const vec3 restRight = ride + vec3(0, .12f, 0), restLeft = snare + vec3(0, .12f, 0);
    const vec3 tipRight = glm::mix(restRight, r.position, reach), tipLeft = glm::mix(restLeft, l.position, reach);

    // Foot work: feathered kick on every beat, hi-hat pedal on 2 and 4.
    const float kickDown = (t.playing ? hitDecay(beatPhase, 9.0f) : 0.0f) * (.4f + .6f * act);
    const long beatIndex = static_cast<long>(std::floor(t.now));
    const float hatDown = (t.playing && (beatIndex & 1)) ? hitDecay(beatPhase, 7.0f) : 0.0f;
    const float hatGap = .045f - .035f * std::max(hatDown, (beatIndex & 1) ? 1.0f - beatPhase : 0.0f);

    Pose pose;
    pose.seated = true;
    pose.pelvis = at({0, .62f - .012f * kickDown, 0});
    pose.lean = .08f + .025f * hitDecay(r.since, 9.0f) * act;
    pose.twist = glm::clamp((tipRight.x + tipLeft.x - snare.x * 2) * -.05f, -.12f, .12f);
    pose.roll = .035f * std::sin(pi * t.inBar * .5f) * act;
    pose.nod = .07f + .05f * std::sin(2.0f * pi * static_cast<float>(t.now)) * act;
    pose.headYaw = .22f * std::sin(t.time * .4f) * (t.playing ? 1.0f : .4f);
    pose.headTilt = .04f * std::sin(t.time * .8f);
    pose.hand[0] = at(tipRight - stickDir * .28f);
    pose.hand[1] = at(tipLeft - stickDir * .28f);
    pose.palm = {vec3(0, 1, 0), vec3(0, 1, 0)};
    pose.foot[0] = at({-.10f, .045f - .035f * kickDown, .50f});
    pose.foot[1] = at({.55f, .045f - .035f * hatDown, .28f});
    pose.elbowPole[0] = vec3(-.9f, -.6f, -.4f);
    pose.elbowPole[1] = vec3(.9f, -.6f, -.4f);
    drawPerson(b, pose, looks[2]);
    b.rod(at(tipRight), at(tipRight - stickDir * .38f), .0075f, ivory);
    b.rod(at(tipLeft), at(tipLeft - stickDir * .38f), .0075f, ivory);

    // Bass drum with its pedal; the beater swings into the head.
    const vec3 axisZ(0, 0, 1);
    b.disc(at({0, .30f, .95f}), axisZ, .28f, .42f, drumShell);
    b.disc(at({0, .30f, .745f}), axisZ, .275f, .012f, drumHead);
    b.disc(at({0, .30f, 1.165f}), axisZ, .275f, .012f, drumHead);
    b.disc(at({0, .30f, 1.172f}), axisZ, .10f, .012f, drumShell);
    for (const float z : {.74f, 1.16f}) b.disc(at({0, .30f, z}), axisZ, .285f, .022f, chrome);
    for (const float x : {-.20f, .20f}) b.rod(at({x * 1.1f, .06f, .90f}), at({x * 1.6f, 0, 1.10f}), .01f, stand);
    b.cube(at({-.10f, .028f + .02f * kickDown, .50f}), {.09f, .012f, .22f}, chrome);
    b.rod(at({-.10f, .05f, .60f}), at({-.10f, .35f, .69f - .09f * kickDown}), .007f, chrome);
    b.ball(at({-.10f, .35f, .715f - .09f * kickDown}), vec3(.022f), ivory);
    // Hi-hat.
    b.rod(at({.55f, 0, .48f}), at({.55f, .96f, .48f}), .009f, stand);
    b.disc(at({.55f, .96f, .48f}), vec3(0, 1, 0), .175f, .006f, cymbal);
    b.disc(at({.55f, .96f + hatGap + .006f, .48f}), vec3(0, 1, 0), .175f, .006f, cymbal);
    b.rod(at({.55f, .01f, .30f}), at({.55f, .03f, .48f}), .008f, stand);
    // Snare drum on its stand.
    b.drum(at({0, .74f, .62f}), .17f, .14f, .17f, chrome);
    b.drum(at({0, .812f, .62f}), .165f, .008f, .165f, drumHead);
    b.rod(at({0, .67f, .62f}), at({0, .0f, .62f}), .008f, stand);
    for (const vec3& leg : {vec3(.18f, 0, .50f), vec3(-.18f, 0, .50f), vec3(0, 0, .86f)}) b.rod(at({0, .30f, .62f}), at(leg + vec3(0, .01f, 0)), .006f, stand);
    // Toms on the bass drum, floor tom on legs.
    b.drum(at({-.15f, .97f, .93f}), .13f, .16f, .13f, drumShell);
    b.drum(at({-.15f, 1.052f, .93f}), .125f, .008f, .125f, drumHead);
    b.drum(at({.17f, .97f, .93f}), .15f, .17f, .15f, drumShell);
    b.drum(at({.17f, 1.057f, .93f}), .145f, .008f, .145f, drumHead);
    b.rod(at({-.15f, .58f, .93f}), at({-.15f, .90f, .93f}), .01f, stand);
    b.rod(at({.17f, .58f, .93f}), at({.17f, .90f, .93f}), .01f, stand);
    b.drum(at({-.66f, .45f, .52f}), .20f, .38f, .20f, drumShell);
    b.drum(at({-.66f, .644f, .52f}), .195f, .008f, .195f, drumHead);
    for (int i = 0; i < 3; ++i) {
        const float a = static_cast<float>(i) * 2.0944f + .5f;
        b.rod(at(vec3(-.66f + .17f * std::cos(a), .36f, .52f + .17f * std::sin(a))),
              at(vec3(-.66f + .19f * std::cos(a), 0, .52f + .19f * std::sin(a))), .009f, stand);
    }
    // Ride and crash cymbals tremble after a hit.
    const float rideWobble = hitDecay(r.since, 6.0f) * act * .05f;
    b.rod(at({-.55f, 0, .92f}), at({-.55f, 1.12f, .92f}), .009f, stand);
    b.disc(at({-.55f, 1.145f, .92f}), glm::normalize(vec3(.03f + std::sin(r.since * 40.0f) * rideWobble, 1, -.10f)), .245f, .006f, cymbal);
    b.rod(at({.62f, 0, .84f}), at({.62f, 1.26f, .84f}), .009f, stand);
    b.disc(at({.62f, 1.29f, .84f}), glm::normalize(vec3(-.12f, 1, -.14f)), .21f, .006f, cymbal);
    // Stool.
    b.drum(at({0, .53f, 0}), .17f, .06f, .17f, suit);
    for (int i = 0; i < 3; ++i) {
        const float a = static_cast<float>(i) * 2.0944f + 1.0f;
        b.rod(at({0, .50f, 0}), at(vec3(.22f * std::cos(a), 0, .22f * std::sin(a))), .012f, stand);
    }
}


// ---------------------------------------------------------------- trumpeter

void trumpeter(Builder& b, const Beat& t) {
    const Frame F{{3.00f, 0, -0.30f}, rotY(-.30f)};
    const float act = t.act;
    const float beatPhase = frac(t.now);
    const float up = smooth((act - .16f) / .35f); // 0: resting with the horn lowered, 1: playing.

    // Eighth-note fingering grid, swung.
    const long k = static_cast<long>(std::floor(t.now)) * 2 + (beatPhase >= 2.0f / 3.0f ? 1 : 0);
    const auto segment = [](long n) { return static_cast<float>(n >> 1) + ((n & 1) ? 2.0f / 3.0f : 0.0f); };
    const float since = static_cast<float>(t.now) - segment(k);
    const auto valves = [&](long n) { return static_cast<int>(hash(static_cast<std::uint32_t>(n), 31) * 8.0f) & 7; };
    const auto rests = [&](long n) { return hash(static_cast<std::uint32_t>(n), 32) > .62f + .36f * act; };
    const float blend = smooth(since / .07f);
    std::array<float, 3> valve{};
    for (int i = 0; i < 3; ++i)
        valve[static_cast<std::size_t>(i)] = mixf(static_cast<float>((valves(k - 1) >> i) & 1), static_cast<float>((valves(k) >> i) & 1), blend) * up;
    const bool blowing = up > .5f && !rests(k);

    Pose pose;
    pose.pelvis = F.at({0, .93f - .02f * hitDecay(beatPhase, 5.0f) * act, 0});
    pose.yaw = -.30f;
    const float phrase = .5f + .5f * std::sin(pi * static_cast<float>(t.now) * .125f);
    pose.lean = -.04f - .05f * phrase * up;                      // Leans back into the high notes.
    pose.roll = .07f * std::sin(pi * static_cast<float>(t.now) * .25f) * act;
    pose.twist = .06f * std::sin(pi * t.bar * .5f + 1.0f) * act;
    pose.nod = -.10f * up + .05f * std::sin(2.0f * pi * static_cast<float>(t.now)) * act;
    pose.headYaw = .12f * std::sin(t.time * .3f);
    pose.headTilt = .03f * std::sin(t.time * .6f);
    const mat3 T = torsoOf(pose), H = headOf(pose);
    const vec3 neck = pose.pelvis + T * vec3(0, .58f, .01f);
    const vec3 head = neck + H * vec3(0, .15f, .015f);
    const vec3 mouth = head + H * vec3(0, -.05f, .10f);

    // The horn: its mouthpiece at the lips, bell lifting with the phrase, dropping when resting.
    const float lift = mixf(.75f, -.04f - .14f * phrase, up) + .035f * hitDecay(beatPhase, 6.0f) * act;
    const vec3 origin = mouth + H * vec3(0, -.20f * (1.0f - up), .06f * (1.0f - up));
    const Frame P{origin, H * rotX(-lift)};
    const auto p = [&](vec3 v) { return P.at(v); };
    b.rod(p({0, 0, -.012f}), p({0, 0, .05f}), .0135f, chrome);
    b.rod(p({0, 0, .05f}), p({0, 0, .20f}), .0075f, brass);
    for (int i = 0; i < 3; ++i) { // Valve casings, pistons and finger buttons.
        const float z = .19f + .028f * static_cast<float>(i);
        const float v = valve[static_cast<std::size_t>(i)];
        b.rod(p({0, -.06f, z}), p({0, -.005f, z}), .0115f, brass);
        b.rod(p({0, -.005f - v * .012f, z}), p({0, .02f - v * .012f, z}), .0045f, chrome);
        b.ball(p({0, .024f - v * .012f, z}), {.0095f, .005f, .0095f}, ivory);
    }
    b.tube({p({0, -.03f, .26f}), p({0, -.03f, .45f})}, {.0085f, .0095f}, brass);
    for (int j = 0; j < 8; ++j) { // Bell flare.
        const float f = static_cast<float>(j) / 7.0f;
        b.disc(p({0, -.03f, .45f + .022f * static_cast<float>(j)}), P.dir({0, 0, 1}), .011f + .064f * std::pow(f, 2.2f), .024f, brass);
    }
    b.tube({p({0, -.03f, .19f}), p({0, -.03f, .075f}), p({0, -.065f, .04f}), p({0, -.10f, .075f}), p({0, -.10f, .38f}), p({0, -.07f, .445f})},
           {.0085f, .0085f, .0085f, .0085f, .0085f, .0085f}, brass);
    b.tube({p({0, -.06f, .222f}), p({0, -.125f, .222f}), p({0, -.125f, .30f}), p({0, -.06f, .30f})}, {.0075f, .0075f, .0075f, .0075f}, brassDark);
    b.ball(p({0, -.108f, .40f}), {.01f, .014f, .01f}, brassDark);
    if (blowing) {
        for (const float s : {-1.0f, 1.0f}) // Puffed cheeks.
            b.ball(head + H * vec3(s * .075f, -.045f, .075f), {.032f, .035f, .03f}, Surface{looks[3].skin, .12f, 24}, H);
    }
    pose.hand[0] = p({.004f, .042f - valve[0] * .002f, .222f});
    pose.palm[0] = P.dir({0, 1, 0});
    pose.hand[1] = p({-.012f, -.12f, .27f});
    pose.palm[1] = P.dir({0, 1, 0});
    pose.elbowPole[0] = vec3(-.8f, -1, -.2f);
    pose.elbowPole[1] = vec3(.8f, -1, -.2f);
    pose.foot[0] = F.at({-.15f, 0, .05f});
    pose.foot[1] = F.at({.18f, .03f * hitDecay(beatPhase, 6.0f) * (act > .35f ? 1.0f : 0.0f), -.05f});
    drawPerson(b, pose, looks[3]);
}

// ---------------------------------------------------------------- saxophonist

void saxophonist(Builder& b, const Beat& t) {
    const Frame F{{1.72f, 0, 0.45f}, rotY(-.22f)};
    const float act = t.act;
    const float beatPhase = frac(t.now);
    const float up = smooth((act - .12f) / .3f);
    const long k = static_cast<long>(std::floor(t.now)) * 2 + (beatPhase >= 2.0f / 3.0f ? 1 : 0);
    const auto segment = [](long n) { return static_cast<float>(n >> 1) + ((n & 1) ? 2.0f / 3.0f : 0.0f); };
    const float since = static_cast<float>(t.now) - segment(k);
    const float blend = smooth(since / .08f);
    const auto fingers = [&](long n, std::uint32_t salt) { return hash(static_cast<std::uint32_t>(n), salt); };

    Pose pose;
    const float sway = std::sin(pi * static_cast<float>(t.now) * .25f);
    pose.pelvis = F.at({0, .93f - .03f * hitDecay(beatPhase, 4.0f) * act, 0});
    pose.yaw = -.22f;
    pose.lean = .02f + .06f * std::sin(pi * t.inBar * .5f) * act;
    pose.roll = .13f * sway * up;                                 // Rocks with the phrase.
    pose.twist = .08f * std::sin(pi * t.bar * .5f) * act;
    pose.nod = -.04f + .05f * std::sin(2.0f * pi * static_cast<float>(t.now)) * act;
    pose.headYaw = .10f * std::sin(t.time * .33f + 2.0f);
    pose.headTilt = -.06f * sway * up;
    const mat3 T = torsoOf(pose), H = headOf(pose);
    const vec3 neck = pose.pelvis + T * vec3(0, .58f, .01f);
    const vec3 head = neck + H * vec3(0, .15f, .015f);
    const vec3 mouth = head + H * vec3(0, -.05f, .095f);

    // The horn hangs from the neck strap: it swings out and the bell lifts on long phrases.
    const float swing = .20f * std::sin(pi * static_cast<float>(t.now) * .25f + .6f) * up;
    const float bellLift = (.06f + .16f * (.5f + .5f * std::sin(pi * static_cast<float>(t.now) * .125f))) * up;
    const Frame S{mouth, T * rotZ(swing * .5f) * rotX(-bellLift)};
    const auto s = [&](vec3 v) { return S.at(v); };
    b.rod(s({0, .012f, -.045f}), s({0, 0, .045f}), .0125f, keyBlack);
    b.tube({s({0, 0, .045f}), s({0, -.018f, .12f}), s({0, -.065f, .172f}), s({0, -.12f, .195f})}, {.010f, .012f, .015f, .018f}, brass);
    std::vector<vec3> body;
    std::vector<float> bodyRadius;
    for (int i = 0; i <= 6; ++i) {
        const float f = static_cast<float>(i) / 6.0f;
        body.push_back(s({0, -.12f - .58f * f, .195f + .005f * f}));
        bodyRadius.push_back(.018f + .017f * f);
    }
    b.tube(body, bodyRadius, brass);
    b.tube({s({0, -.70f, .20f}), s({0, -.765f, .215f}), s({0, -.80f, .26f}), s({0, -.775f, .31f}), s({0, -.70f, .335f}), s({0, -.58f, .375f}), s({0, -.48f, .425f})},
           {.035f, .037f, .038f, .038f, .040f, .046f, .052f}, brass);
    const vec3 bellDirection = S.dir(glm::normalize(vec3(0, .55f, .83f)));
    for (int j = 0; j < 6; ++j) { // Flared bell.
        const float f = static_cast<float>(j) / 5.0f;
        b.disc(s({0, -.48f, .425f}) + bellDirection * (.025f * static_cast<float>(j)), bellDirection, .054f + .055f * std::pow(f, 1.6f), .027f, brass);
    }
    for (int i = 0; i < 9; ++i) { // Pearl keys down the front, brass keywork either side.
        const float y = -.20f - .052f * static_cast<float>(i);
        const float down = 0.0f + (fingers(k, 41 + static_cast<std::uint32_t>(i)) > .5f ? blend : 1.0f - blend) * .5f * act;
        b.ball(s({0, y, .164f - down * .008f}), {.011f, .011f, .007f}, ivory);
        b.ball(s({(i % 2 ? .034f : -.034f), y - .01f, .195f}), {.013f, .009f, .009f}, chrome);
    }
    b.rod(s({.03f, -.16f, .20f}), s({.03f, -.66f, .20f}), .0045f, chrome);
    b.rod(s({-.03f, -.16f, .20f}), s({-.03f, -.66f, .20f}), .0045f, chrome);
    b.rod(neck + T * vec3(.05f, .02f, -.04f), s({0, -.30f, .20f}), .006f, shoe); // Strap.
    b.rod(neck + T * vec3(-.05f, .02f, -.04f), s({0, -.30f, .20f}), .006f, shoe);

    // Hands on the keys; the fingers shift a little on every note.
    const float wobbleL = (fingers(k, 51) - .5f) * .03f * act, wobbleR = (fingers(k, 52) - .5f) * .03f * act;
    pose.hand[1] = s({.012f, -.29f + wobbleL, .150f});
    pose.hand[0] = s({-.012f, -.50f + wobbleR, .150f});
    pose.palm = {S.dir({0, 0, -1}), S.dir({0, 0, -1})};
    pose.elbowPole[0] = vec3(-.9f, -.8f, -.2f);
    pose.elbowPole[1] = vec3(.9f, -.8f, -.2f);
    pose.foot[0] = F.at({-.15f, .035f * hitDecay(beatPhase, 6.0f) * (act > .35f ? 1.0f : 0.0f), .05f});
    pose.foot[1] = F.at({.19f, 0, -.04f});
    drawPerson(b, pose, looks[4]);
    if (up > .5f)
        for (const float side : {-1.0f, 1.0f})
            b.ball(head + H * vec3(side * .07f, -.045f, .07f), {.028f, .03f, .026f}, Surface{looks[4].skin, .12f, 24}, H);
}

void ensemble(Builder& b, const Playback& p) {
    const float div = p.bpm > maxPulseBpm ? 2.0f : 1.0f;
    Beat t{};
    t.playing = p.playing;
    t.time = p.time;
    t.now = p.playing ? p.beats / div : static_cast<double>(p.time); // Idle: slow, one pulse a second.
    t.bar = std::floor(static_cast<float>(t.now) / 4.0f);
    t.inBar = static_cast<float>(t.now) - 4.0f * t.bar;
    const auto beatFor = [&](Musician who) {
        Beat copy = t;
        copy.act = intensity(who, p);
        return copy;
    };
    pianist(b, beatFor(Musician::Pianist));
    guitarist(b, beatFor(Musician::Guitarist));
    drummer(b, beatFor(Musician::Drummer));
    trumpeter(b, beatFor(Musician::Trumpeter));
    saxophonist(b, beatFor(Musician::Saxophonist));
}

} // namespace

float intensity(Musician who, const Playback& p) {
    if (!p.playing) return .12f;
    float e = 0;
    switch (who) {
        case Musician::Pianist: e = p.mid * .6f + p.low * .3f + p.hit * .1f; break;
        case Musician::Guitarist: e = p.mid * .7f + p.hit * .3f; break;
        case Musician::Drummer: e = p.hit * .55f + p.low * .3f + p.high * .15f; break;
        case Musician::Trumpeter: e = p.high * .65f + p.mid * .35f; break;
        case Musician::Saxophonist: e = p.mid * .55f + p.high * .45f; break;
    }
    return clamp01(.30f + .85f * e);
}

} // namespace gfx::band

namespace gfx::band {

void build(const Playback& playback, std::vector<Part>& out) {
    Builder builder{out};
    ensemble(builder, playback);
}

} // namespace gfx::band
