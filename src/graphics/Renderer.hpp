#pragma once

#include <filesystem>
#include <memory>
#include <vector>

namespace gfx {

struct VisualCard {
    int rank = 1; // Ace = 1, Jack = 11, Queen = 12, King = 13.
    int suit = 0; // Clubs, diamonds, hearts, spades (same order as bj::Suit).
    bool faceDown = false;
    float arrival = 1.0f; // 0 = leaving the dealer, 1 = settled in this hand.
    float flip = 1.0f; // 0 = back, 1 = the side selected by faceDown.
};

// Normalized framebuffer rectangle, measured from the top-left like the UI.
struct Viewport {
    float left = 0.0f;
    float top = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

// Presentation data only: inventory ownership and ability rules remain in Game.
struct EquipmentChip {
    int id = 0;
    int rarity = 0; // Common = 0 (ivory), Rare = 1 (blue), Legendary = 2 (gold).
    bool active = false;
    bool available = false;
    int charges = 0;
    float highlight = 0.0f;
};

struct ChipProjection {
    float x = 0, y = 0, radiusX = 0, radiusY = 0;
    bool visible = false;
};

// What the on-stage jazz ensemble is playing (see graphics/BandModel.hpp).
struct BandView {
    bool visible = true;
    bool playing = false;  // False: the musicians idle between numbers.
    double beats = 0.0;    // Continuous beat count; a bar line every four beats.
    float bpm = 110.0f;
    float low = 0.5f, mid = 0.5f, high = 0.5f, hit = 0.5f; // Energies 0..1 of the current soundtrack.
};

struct Scene {
    std::vector<VisualCard> player;
    std::vector<VisualCard> dealer;
    int coins = 0;
    std::vector<EquipmentChip> equipment;
    float time = 0.0f;
    bool menu = false;
    float menuApproach = 0.0f; // Eased progress from establishing view to the threshold.
    float doorOpen = 0.0f; // Hinged leaves, 0 = closed, 1 = open outward into the forecourt.
    float menuPush = 0.0f; // Short final movement through the open doorway.
    Viewport tableViewport;
    float dealerReach = 0.0f;
    bool dealingToPlayer = true;
    float celebration = 0.0f; // Presentation envelope: 1 at the win, decays to 0.
    float legendary = 0.0f; // Presentation envelope for rare gold/violet sparks.
    int deckDesign = 0; // Hands, Life, Coins.
    float bustReaction = 0.0f; // Presentation envelope: 1 at a player bust, decays to 0.
    BandView band;
};

inline constexpr float minLight = .55f, maxLight = 2.5f;
inline constexpr float minZoom = 3.5f, maxZoom = 14.0f;

struct RenderSettings {
    float yaw = 0.0f;
    float pitch = 32.0f;
    float distance = 9.1f;
    float lightIntensity = 1.2f;
    // World-space offset from the scene's default main light; zero restores it.
    float lightX = 0.0f;
    float lightY = 0.0f;
    float lightZ = 0.0f;
    bool textures = true;
    bool lighting = true;
    bool animate = true;
};

// Construct and destroy while the GLFW OpenGL context is current.
class Renderer {
public:
    explicit Renderer(const std::filesystem::path& assetRoot);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void render(const Scene& scene, const RenderSettings& settings,
                int framebufferWidth, int framebufferHeight);

    // Uses the exact render camera and physical chip positions; no GL context required.
    // All bounds are normalized to the complete framebuffer, with a top-left origin.
    static std::vector<ChipProjection> projectEquipment(const Scene& scene,
        const RenderSettings& settings, int framebufferWidth, int framebufferHeight);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gfx
