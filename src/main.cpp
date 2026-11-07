#include "presentation/ScoreHistory.hpp"
#include "presentation/CollectionOrder.hpp"
#include "presentation/EntranceTransition.hpp"
#include "presentation/PalaceStyle.hpp"
#include "presentation/CarPass.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "audio/Engine.hpp"
#include "audio/Preferences.hpp"
#include "game/Game.hpp"
#include "graphics/Renderer.hpp"
#include "graphics/ArtTexture.hpp"
#include "presentation/CardSuit.hpp"
#include "presentation/BandSync.hpp"
#include "presentation/Presentation.hpp"
#include <iomanip>
#include <fstream>
#include <sstream>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr ImVec4 gold{0.84f, 0.69f, 0.42f, 1.0f};
constexpr ImVec4 muted{0.68f, 0.64f, 0.61f, 1.0f};
constexpr ImVec4 cream{0.97f, 0.92f, 0.82f, 1.0f};
constexpr ImVec4 green{0.48f, 0.79f, 0.61f, 1.0f};
constexpr ImVec4 red{0.92f, 0.42f, 0.39f, 1.0f};
constexpr ImVec4 blue{0.39f, 0.66f, 0.88f, 1.0f};
constexpr ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoDecoration |
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

// Sound output used by free UI helpers. main() points it at the live engine for the
// interactive loop only, so previews, screenshots and tests stay silent.
audio::Engine* soundOut = nullptr;
audio::Sfx pendingUiSound = audio::Sfx::Count;
float effectTime = 0.0f; // Shared scene clock; hover feedback remains interactive during pause.
int uiSoundPriority(audio::Sfx sound) {
    switch (sound) {
        case audio::Sfx::UiError: return 4;
        case audio::Sfx::UiClose: return 3;
        case audio::Sfx::UiOpen: return 2;
        case audio::Sfx::UiSelection: return 1;
        default: return 0;
    }
}
void playSound(audio::Sfx sound) {
    if (uiSoundPriority(sound)>0) {
        if (uiSoundPriority(sound)>uiSoundPriority(pendingUiSound)) pendingUiSound=sound;
        return;
    }
    if (soundOut) soundOut->play(sound);
}
struct SoundScope {
    explicit SoundScope(audio::Engine& engine) { soundOut = &engine; }
    ~SoundScope() { soundOut = nullptr; }
};

void playCue(presentation::Cue cue) {
    using presentation::Cue;
    using audio::Sfx;
    switch (cue) {
        case Cue::BetPlaced: playSound(Sfx::ChipsBet); break;
        case Cue::DealCard: playSound(Sfx::CardDeal); break;
        case Cue::PlayerCardRoll: playSound(Sfx::CardRoll); break;
        case Cue::DealerCardRoll: playSound(Sfx::CardRoll); break; // The dealer's draw uses the same roll.
        case Cue::HandWon: playSound(Sfx::TadaMinor); break;
        case Cue::RoundCleared: playSound(Sfx::TadaMajor); break;
        case Cue::ChipsWon: playSound(Sfx::ChipsWin); break;
        case Cue::HandLost:
        case Cue::RunLost:
            playSound(Sfx::ChipsWin); // The dealer collects the chips at settlement.
            playSound(Sfx::YouLose);
            break;
    }
}

// One selection sound on entering a control. A click or error in the same frame takes priority.
bool hoverSoundsEnabled = false;
ImGuiID hoveredButtonBefore = 0, hoveredButtonNow = 0;
void beginSoundFrame() {
    pendingUiSound=audio::Sfx::Count;
    hoveredButtonBefore = hoveredButtonNow;
    hoveredButtonNow = 0;
}
void buttonSounds(bool pressed, bool closing) {
    if (ImGui::IsItemHovered()) {
        const ImGuiID id = ImGui::GetItemID();
        if (!pressed && hoverSoundsEnabled && hoveredButtonBefore != id) playSound(audio::Sfx::UiSelection);
        hoveredButtonNow = id;
    }
    if (pressed) playSound(closing ? audio::Sfx::UiClose : audio::Sfx::UiOpen);
}

std::uint32_t freshSeed() {
    std::random_device random;
    return random();
}

std::filesystem::path executableDirectory(const char* argument) {
#ifdef _WIN32
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length > 0 && length < path.size())
        return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) == 0)
        return std::filesystem::weakly_canonical(path.data()).parent_path();
#elif defined(__linux__)
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) return path.parent_path();
#endif
    return std::filesystem::absolute(argument).parent_path();
}

struct Options {
    std::uint32_t seed = 0;
    bool fixedSeed = false;
    int frames = 0;
    bool hidden = false;
    bool demo = false;
    bool autoplay = false;
    bool smoke = false;
    bool noAnimation = false;
    bool noAmbientMotion = false;
    bool cameraOverride = false;
    float light = 1.2f;
    bool lightSmoke = false;
    bool controlsSmoke = false;
    bool benchmark = false;
    bool lightingOff = false;
    bool audioSmoke = false;
    std::filesystem::path audioSettings;
    bool entrancePreview = false;
    bool equipmentSmoke = false;
    bool abilitySmoke = false;
    bool abilityMouse = false;
    int abilityTarget = -1;
    int stopOwned = -1;
    std::filesystem::path replay;
    std::filesystem::path animationFrames;
    std::string previewScreen;
    int previewRelic=-1;
    float cameraYaw=0, cameraPitch=32, cameraDistance=9.1f;
    int width = 1440;
    int height = 900;
    bj::StartDeck deck = bj::StartDeck::ExtraHand;
    std::filesystem::path screenshot;
    std::filesystem::path captures;
    std::filesystem::path assets;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string {
            if (++i >= argc) throw std::runtime_error("Missing value after " + arg);
            return argv[i];
        };
        if (arg == "--seed") {
            const auto seed = std::stoull(value());
            if (seed > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("Seed must fit in uint32.");
            options.seed = static_cast<std::uint32_t>(seed);
            options.fixedSeed = true;
        } else if(arg=="--deck") {
            const auto deck=value();
            if(deck=="hands") options.deck=bj::StartDeck::ExtraHand;
            else if(deck=="life") options.deck=bj::StartDeck::SecondLife;
            else if(deck=="coins") options.deck=bj::StartDeck::Rich;
            else throw std::runtime_error("Deck must be hands, life or coins.");
        } else if (arg == "--frames") {
            options.frames = std::stoi(value());
            if (options.frames < 1) throw std::runtime_error("Frames must be positive.");
        } else if (arg == "--size") {
            const auto size = value();
            const auto separator = size.find('x');
            if (separator == std::string::npos) throw std::runtime_error("Size must be WIDTHxHEIGHT.");
            const auto parseDimension = [](const std::string& text) {
                if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("Size must be WIDTHxHEIGHT.");
                return std::stoi(text);
            };
            options.width = parseDimension(size.substr(0, separator));
            options.height = parseDimension(size.substr(separator + 1));
            if (options.width < 1100 || options.height < 720 || options.width > 7680 || options.height > 4320)
                throw std::runtime_error("Size must be between 1100x720 and 7680x4320.");
        } else if (arg == "--camera") {
            std::istringstream input(value());
            char comma1=0,comma2=0;
            if(!(input>>options.cameraYaw>>comma1>>options.cameraPitch>>comma2>>options.cameraDistance)
                || comma1!=',' || comma2!=',' || !input.eof()
                || !std::isfinite(options.cameraYaw) || !std::isfinite(options.cameraPitch) || !std::isfinite(options.cameraDistance)
                || std::abs(options.cameraYaw)>35 || options.cameraPitch<32 || options.cameraPitch>68
                || options.cameraDistance<gfx::minZoom || options.cameraDistance>gfx::maxZoom)
                throw std::runtime_error("Camera must be yaw,pitch,distance within -35..35,32..68,3.5..14.");
            options.cameraOverride=true;
        } else if (arg == "--light") {
            options.light=std::stof(value());
            if(!std::isfinite(options.light)||options.light<gfx::minLight||options.light>gfx::maxLight)
                throw std::runtime_error("Light must be within .55..2.5.");
        } else if(arg=="--entrance-preview") options.entrancePreview=true;
        else if(arg=="--equipment-smoke") options.equipmentSmoke=true;
        else if(arg=="--ability-smoke") options.abilitySmoke=true;
        else if(arg=="--ability-target") options.abilityTarget=std::stoi(value());
        else if(arg=="--ability-mouse") {options.abilityMouse=true;options.abilitySmoke=true;}
        else if(arg=="--replay") options.replay=value();
        else if(arg=="--capture-animation") options.animationFrames=value();
        else if(arg=="--preview-screen") options.previewScreen=value();
        else if(arg=="--preview-relic") {
            options.previewRelic=std::stoi(value());
            if(options.previewRelic<0 || options.previewRelic>=static_cast<int>(bj::relicCatalog().size()))
                throw std::runtime_error("Preview relic must be a valid catalog ID");
        }
        else if(arg=="--stop-owned") {
            options.stopOwned=std::stoi(value());
            if(options.stopOwned<0||options.stopOwned>bj::maxEquipmentSlots) throw std::runtime_error("--stop-owned must be 0..8");
        } else if(arg=="--light-smoke") options.lightSmoke=true;
        else if(arg=="--controls-smoke") options.controlsSmoke=true;
        else if(arg=="--benchmark") options.benchmark=true;
        else if(arg=="--lighting-off") options.lightingOff=true;
        else if(arg=="--audio-smoke") options.audioSmoke=true;
        else if(arg=="--audio-settings") options.audioSettings=value();
        else if (arg == "--screenshot") options.screenshot = value();
        else if (arg == "--capture-states") options.captures = value();
        else if (arg == "--assets") options.assets = value();
        else if (arg == "--hidden") options.hidden = true;
        else if (arg == "--demo") options.demo = true;
        else if (arg == "--autoplay") options.autoplay = true;
        else if (arg == "--no-animation") options.noAnimation = true;
        else if (arg == "--no-ambient-motion") options.noAmbientMotion = true;
        else if (arg == "--smoke") {
            options.hidden = true;
            options.autoplay = true;
            options.smoke = true;
            options.frames = 1200;
        } else if (arg == "--help") {
            std::cout << "Jester Jack\n"
                "  --seed N          Reproducible run (otherwise a fresh random seed)\n"
                "  --demo            Skip menu and deal the first hand\n"
                "  --deck NAME       Select hands, life or coins deck\n"
                "  --frames N        Exit after N rendered frames\n"
                "  --size WxH        Initial window size (minimum 1100x720)\n"
                "  --screenshot FILE Save the final frame as PNG\n"
                "  --camera Y,P,D    Diagnostic orbit: yaw,pitch,distance\n"
                "  --light VALUE     Lamp intensity (.55..2.5)\n"
                "  --light-smoke     Exercise live light slider at three values\n"
                "  --controls-smoke Verify help, pause, lighting and light movement\n"
                "  --benchmark       Measure 600 frames at 1280x720 after warm-up\n"
                "  --lighting-off    Start with lighting disabled for comparison\n"
                "  --audio-smoke     Exercise the two volume sliders\n"
                "  --audio-settings FILE Use a separate audio preferences file\n"
                "  --replay FILE     Replay verified public game commands for diagnostics\n"
                "  --stop-owned N    Stop replay with 0..8 acquired relics\n"
                "  --equipment-smoke Verify safe inspection via mouse and keys 1-8\n"
                "  --ability-smoke   Verify inspection, explicit A activation and Q/E choices\n"
                "  --entrance-preview Start cinematic on frame 1\n"
                "  --capture-animation DIR Save numbered PNG frames\n"
                "  --capture-states DIR Save menu, table, shop and end screens\n"
                "  --assets DIR      Override assets directory\n"
                "  --hidden          Create an invisible graphics window\n"
                "  --no-animation    Reduced-motion entrance fade\n"
                "  --no-ambient-motion Disable decorative scene motion\n"
                "  --autoplay        Exercise the game state machine\n"
                "  --smoke           Hidden integration run, including menu and resize\n";
            std::exit(0);
        } else throw std::runtime_error("Unknown option: " + arg);
    }
    if (options.benchmark) {
        options.demo = true;
        options.width = 1280;
        options.height = 720;
        options.frames = 660;
        options.seed = 42;
        options.fixedSeed = true;
    }
    if (!options.fixedSeed) options.seed = options.smoke ? 42 : freshSeed();
    if (!options.screenshot.empty() && options.frames == 0) options.frames = 3;
    return options;
}

struct Window {
    GLFWwindow* handle = nullptr;
    explicit Window(bool hidden, int width, int height) {
        glfwSetErrorCallback([](int code, const char* description) {
            std::cerr << "GLFW " << code << ": " << description << '\n';
        });
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed.");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        glfwWindowHint(GLFW_SAMPLES, 4);
        glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
        // Keep capture dimensions exact on Windows, including full desktop sizes.
        if (hidden) glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        handle = glfwCreateWindow(width, height, "Jester Jack", nullptr, nullptr);
        if (!handle) {
            glfwTerminate();
            throw std::runtime_error("OpenGL 3.3 window creation failed. Check the GPU driver.");
        }
#ifdef _WIN32
        const auto native = glfwGetWin32Window(handle);
        const auto module = GetModuleHandleW(nullptr);
        SendMessageW(native, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIconW(module, MAKEINTRESOURCEW(101))));
        SendMessageW(native, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadImageW(module, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_SHARED)));
#endif
        glfwMakeContextCurrent(handle);
        if (!gladLoadGL(glfwGetProcAddress)) {
            glfwDestroyWindow(handle);
            glfwTerminate();
            throw std::runtime_error("Could not load OpenGL functions.");
        }
        glfwSetWindowSizeLimits(handle, 1100, 720, GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwSwapInterval(hidden ? 0 : 1);
        std::cout << "OpenGL: " << glGetString(GL_VERSION) << '\n'
                  << "Renderer: " << glGetString(GL_RENDERER) << '\n';
    }
    ~Window() {
        if (handle) glfwDestroyWindow(handle);
        glfwTerminate();
    }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
};

struct Interface {
    bool glfwBackend = false;
    bool glBackend = false;
    ImFont* title = nullptr;
    ImFont* heading = nullptr;
    ImFont* button = nullptr;
    ImFont* menuButton = nullptr;
    ImFont* number = nullptr;
    ImFont* largeNumber = nullptr;
    ImFont* small = nullptr;
    explicit Interface(GLFWwindow* window, const std::filesystem::path& assets) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImFontConfig font;
        font.OversampleH = 3;
        font.OversampleV = 2;
        const auto semibold = (assets / "fonts/CormorantGaramond-SemiBold.ttf").string();
        const auto body = (assets / "fonts/SourceSans3-Regular.ttf").string();
        const auto bold = (assets / "fonts/SourceSans3-Semibold.ttf").string();
        io.FontDefault = io.Fonts->AddFontFromFileTTF(body.c_str(), 22.0f, &font);
        heading = io.Fonts->AddFontFromFileTTF(semibold.c_str(), 36.0f, &font);
        title = io.Fonts->AddFontFromFileTTF(semibold.c_str(), 200.0f, &font);
        button = io.Fonts->AddFontFromFileTTF(bold.c_str(), 26.0f, &font);
        menuButton = io.Fonts->AddFontFromFileTTF(bold.c_str(), 34.0f, &font);
        number = io.Fonts->AddFontFromFileTTF(bold.c_str(), 46.0f, &font);
        largeNumber = io.Fonts->AddFontFromFileTTF(bold.c_str(), 104.0f, &font);
        small = io.Fonts->AddFontFromFileTTF(body.c_str(), 18.0f, &font);
        if (!io.FontDefault || !heading || !title || !button || !menuButton || !number || !largeNumber || !small) {
            ImGui::DestroyContext();
            throw std::runtime_error("Missing Jester Jack fonts. Keep assets beside the executable.");
        }
        ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle();
        style.WindowRounding = 8.0f;
        style.FrameRounding = 5.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.FramePadding = ImVec2(16, 8);
        style.WindowPadding = ImVec2(20, 16);
        style.ItemSpacing = ImVec2(12, 9);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.085f, 0.09f, 0.985f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.11f, 0.115f, 0.97f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.38f, 0.31f, 0.24f, 0.82f);
        style.Colors[ImGuiCol_Text] = cream;
        style.Colors[ImGuiCol_TextDisabled] = muted;
        style.Colors[ImGuiCol_Button] = ImVec4(0.12f, 0.19f, 0.20f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.30f, 0.30f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.09f, 0.15f, 0.16f, 1.0f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.15f, 0.18f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.22f, 0.25f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.32f, 0.24f, 0.30f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.25f, 0.20f, 0.23f, 1.0f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.31f, 0.25f, 0.28f, 1.0f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.36f, 0.29f, 0.32f, 1.0f);
        style.Colors[ImGuiCol_Separator] = ImVec4(0.42f, 0.34f, 0.25f, 0.65f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.09f, 0.07f, 0.09f, 0.8f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.40f, 0.32f, 0.28f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.51f, 0.42f, 0.34f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = gold;
        style.Colors[ImGuiCol_CheckMark] = gold;
        style.Colors[ImGuiCol_SliderGrab] = gold;
        style.Colors[ImGuiCol_SliderGrabActive] = cream;
        style.Colors[ImGuiCol_NavCursor] = gold;
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.11f, 0.16f, 1.0f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 0.17f, 0.24f, 1.0f);
        palace::applyStyle();
        glfwBackend = ImGui_ImplGlfw_InitForOpenGL(window, true);
        if (glfwBackend) glBackend = ImGui_ImplOpenGL3_Init("#version 330 core");
        if (!glfwBackend || !glBackend) {
            if (glfwBackend) ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            throw std::runtime_error("ImGui backend initialization failed.");
        }
    }
    ~Interface() {
        if (glBackend) ImGui_ImplOpenGL3_Shutdown();
        if (glfwBackend) ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    Interface(const Interface&) = delete;
    Interface& operator=(const Interface&) = delete;
};

struct ArtAssets {
    gfx::ArtTexture relics;
    gfx::ArtTexture expansion;
    gfx::ArtTexture hands;
    gfx::ArtTexture life;
    gfx::ArtTexture coins;
    explicit ArtAssets(const std::filesystem::path& assets)
        : relics(assets / "art/relics.tga"), hands(assets / "art/deck-hands.tga"),
          life(assets / "art/deck-life.tga"), coins(assets / "art/deck-coins.tga") {
        // Keep the original twenty-four illustrations in their untouched atlas.
        const auto extra=assets / "art/relics-expansion.tga";
        if(std::filesystem::exists(extra)) {
            try {
                expansion.load(extra);
                if(expansion.width()!=1254 || expansion.height()!=1254)
                    throw std::runtime_error("Expansion artwork must retain its native 1254x1254 dimensions");
            } catch(const std::exception& error) {
                expansion=gfx::ArtTexture{};
                std::cerr<<"Expansion artwork fallback: "<<error.what()<<'\n';
            }
        }
        else {
            std::cerr << "Expansion artwork missing: " << extra.string() << "; using engraved fallback chips.\n";
        }
    }
    const gfx::ArtTexture& deck(bj::StartDeck choice) const {
        return choice == bj::StartDeck::SecondLife ? life : choice == bj::StartDeck::Rich ? coins : hands;
    }
};

struct Session {
    ImVec2 lightMin{},lightMax{};
    ImVec2 volumeMin[2]{},volumeMax[2]{};
    presentation::ScoreHistory history;
    bool menu = true;
    bool controlsTab = false;
    bool collection = false;
    bool deckSelection = false;
    int collectionFilter = 0;
    bj::Relic selectedRelic = bj::Relic::RoyalPurse;
    bj::StartDeck selectedDeck = bj::StartDeck::ExtraHand;
    float deckMotion = 0;
    int deckDirection = 1;
    bj::Relic inspectedRelic = bj::Relic::RoyalPurse;
    float relicHover[bj::maxEquipmentSlots]{};
    int selectedEquipment = -1;
    int shopReplacement = -1;
    bool physicalChipHovered = false;
    bool suppressEquipmentHover = false;
    int contextRound=-1,contextHands=-1;
    std::uint32_t contextSeed=0;
    std::vector<bj::Relic> contextOwned;
    void clearEquipmentContext() {selectedEquipment=-1;hoveredRelic=-1;suppressEquipmentHover=true;}
    void syncEquipmentContext(const bj::Snapshot& state) {
        const bool initialized=contextRound>=0;
        if(contextRound!=state.round||contextHands!=state.handsLeft||contextSeed!=state.seed||contextOwned!=state.owned)
            clearEquipmentContext();
        if(!initialized) suppressEquipmentHover=false;
        contextRound=state.round;contextHands=state.handsLeft;contextSeed=state.seed;contextOwned=state.owned;
    }
    std::vector<float> collectionHover = std::vector<float>(bj::relicCatalog().size(),0.0f);
    int hoveredRelic = -1;
    float animationSpeed = 0.0f;
    bool settings = false;
    bool rules = false;
    bool started = false;
    bool fixedSeed = false;
    bool transitions = true;
    presentation::EntranceTransition entrance;
    bool switched = false;
    std::uint32_t nextSeed = 0;
    float gameOverElapsed = 0.0f;
    bool gameOverFocused = false;
    int relicReactionStep = -1;
    float relicReactionElapsed = 0.0f;
    float bustReaction = 0.0f;
    bool showingBust = false;

    bool transitioning() const { return entrance.active(); }
    void returnToMenu() { menu=true; entrance.reset(); selectedEquipment=-1; }
    void start(bj::Game& game) {
        if (transitioning()) return;
        nextSeed = !started ? game.state().seed :
            (fixedSeed ? game.state().seed + 1 : freshSeed());
        settings = false;
        collection = false;
        deckSelection = false;
        rules = false;
        switched = false;
        entrance.begin(!transitions);
    }
    void update(float dt, bj::Game& game) {
        if (entrance.update(dt)) {
            game.restart(nextSeed, selectedDeck);
            history.reset();
            selectedEquipment=-1;
            shopReplacement=-1;
            hoveredRelic=-1;
            std::fill(std::begin(relicHover),std::end(relicHover),0.0f);
            menu = false;
            started = true;
            switched = true;
        }
    }
    float darkness() const { return entrance.darkness(); }

    void updateGameOver(float dt, bool showing) {
        if (showing) gameOverElapsed = std::min(3.0f, gameOverElapsed + dt);
        else {
            gameOverElapsed = 0.0f;
            gameOverFocused = false;
        }
    }
    void updateRelicReaction(float dt, const presentation::Presentation& timeline) {
        const auto& state=timeline.visible();
        const int step=timeline.rewardStep();
        const bool active=timeline.busy() && step>0 &&
            step<=static_cast<int>(state.score.appliedEffects.size());
        if(!active) {
            relicReactionStep=-1;
            relicReactionElapsed=0;
        } else if(relicReactionStep!=step) {
            relicReactionStep=step;
            relicReactionElapsed=0;
        } else relicReactionElapsed+=dt;
    }
    void updateBustReaction(float dt, const bj::Snapshot& state) {
        const bool bust=(state.phase==bj::Phase::RoundOver || state.phase==bj::Phase::RunLost) &&
            state.outcome==bj::Outcome::Loss && bj::evaluateHand(state.player).bust;
        if(bust && !showingBust) bustReaction=1.0f;
        else if(!bust) bustReaction=0.0f;
        else bustReaction=std::max(0.0f,bustReaction-dt/.8f);
        showingBust=bust;
    }
};

// At most one accepted game command per frame, shared by keyboard and buttons.
struct Commands {
    bool blocked = false;
    bool used = false;
    bj::Game* game = nullptr;
    presentation::Presentation* timeline = nullptr;
    template <typename Action> void apply(Action action, bool ability = false) {
        if (blocked || used) return;
        const auto before = game->state();
        if (!action()) {
            playSound(audio::Sfx::UiError);
            return;
        }
        used = true;
        playSound(audio::Sfx::UiOpen);
        const auto& after = game->state();
        auto kind = ability ? presentation::Action::Ability : presentation::Action::Other;
        if (!ability && before.phase == bj::Phase::Ready && after.handsLeft < before.handsLeft)
            kind = presentation::Action::Deal;
        else if (!ability && before.phase == bj::Phase::PlayerTurn)
            kind = after.player.size() > before.player.size() ||
                before.usedSecondChance != after.usedSecondChance ? presentation::Action::Hit : presentation::Action::Stand;
        timeline->begin(before, after, kind);
    }
};

struct HandFeedback {
    std::vector<bj::Card> previous;
    std::uint32_t seed = 0;
    int round = 0;
    int handsLeft = -1;
    bool usedSecondChance = false;
    bj::Card card{1, bj::Suit::Clubs};
    int delta = 0;
    float remaining = 0;

    void observe(const bj::Snapshot& state, float dt, bool visible) {
        remaining = std::max(0.0f, remaining - dt);
        const bool sameHand = seed == state.seed && round == state.round && handsLeft == state.handsLeft;
        const bool appended = state.player.size() == previous.size() + 1 && previous.size() >= 2 &&
            std::equal(previous.begin(), previous.end(), state.player.begin(), [](const bj::Card& a, const bj::Card& b) {
                return a.rank == b.rank && a.suit == b.suit;
            });
        if (!visible || !sameHand || state.phase == bj::Phase::Ready || state.phase == bj::Phase::Shop ||
            state.player.empty() || usedSecondChance != state.usedSecondChance) {
            remaining = 0;
        } else if (appended) {
            card = state.player.back();
            delta = bj::evaluateHand(state.player).total - bj::evaluateHand(previous).total;
            remaining = 1.5f;
        }
        previous = state.player;
        seed = state.seed;
        round = state.round;
        handsLeft = state.handsLeft;
        usedSecondChance = state.usedSecondChance;
    }
};

void beginPanel(const char* name, ImVec2 position, ImVec2 size, bool transparent = false,
                ImGuiWindowFlags extraFlags = 0) {
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin(name, nullptr, panelFlags | extraFlags | (transparent ? ImGuiWindowFlags_NoBackground : 0));
    if(!transparent) palace::panelFrame(ImGui::GetWindowDrawList(),position,size,size.x>600);
}

void heading(const char* text, const Interface& ui) {
    ImGui::PushFont(ui.heading);
    ImGui::TextColored(gold, "%s", text);
    ImGui::PopFont();
    ImGui::Separator();
    palace::sectionRule(ImGui::GetWindowDrawList(),ImGui::GetCursorScreenPos(),ImGui::GetContentRegionAvail().x);
    ImGui::Spacing();
}

bool themedButton(const char* label, ImVec2 size, ImVec4 tone, bool closing) {
    // Keep the native hit box, layout and keyboard navigation. Only the face moves inside it.
    const ImVec4 transparent{0,0,0,0};
    ImGui::PushStyleColor(ImGuiCol_Button,transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,transparent);
    ImGui::PushStyleColor(ImGuiCol_Text,transparent);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
    const bool pressed=ImGui::Button(label,size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    const auto p=ImGui::GetItemRectMin(),end=ImGui::GetItemRectMax();
    const bool disabled=ImGui::GetStyle().Alpha<.8f;
    const bool highlighted=!disabled&&(ImGui::IsItemHovered()||(ImGui::GetIO().NavVisible&&ImGui::IsItemFocused()));
    const bool held=!disabled&&ImGui::IsItemActive();
    ImGui::PushID(label);
    const auto hoverId=ImGui::GetID("hover-motion"),pressId=ImGui::GetID("press-motion"),frameId=ImGui::GetID("motion-frame");
    ImGui::PopID();
    auto* storage=ImGui::GetStateStorage();
    const bool continuous=storage->GetInt(frameId,-2)==ImGui::GetFrameCount()-1;
    const float amount=1-std::exp(-std::min(ImGui::GetIO().DeltaTime,.1f)*14.0f);
    const float priorHover=continuous?storage->GetFloat(hoverId):0;
    const float priorPress=continuous?storage->GetFloat(pressId):0;
    const float hover=priorHover+((highlighted?1.0f:0.0f)-priorHover)*amount;
    const float press=priorPress+((held?1.0f:0.0f)-priorPress)*amount;
    storage->SetFloat(hoverId,hover);storage->SetFloat(pressId,press);storage->SetInt(frameId,ImGui::GetFrameCount());
    const bool handAction=std::strcmp(label,"HIT")==0||std::strcmp(label,"STAND")==0||std::strcmp(label,"ACCEPT BUST")==0;
    const float travel=handAction?1.0f:std::min(2.4f,(end.y-p.y)*.04f);
    const float lift=travel*hover*(1-press);
    const ImVec2 faceMin{p.x+1.5f,p.y+3.0f-lift+press},faceMax{end.x-1.5f,end.y-2.0f-lift+press};
    auto* draw=ImGui::GetWindowDrawList();
    draw->PushClipRect(p,end,true);
    draw->AddRectFilled({p.x+2,p.y+4},{end.x-2,end.y-1},ImGui::GetColorU32({0,0,0,.36f}),6);
    const float brightness=.065f*hover-.045f*press;
    const ImVec4 face{std::clamp(tone.x+brightness,0.0f,1.0f),std::clamp(tone.y+brightness,0.0f,1.0f),std::clamp(tone.z+brightness,0.0f,1.0f),tone.w};
    draw->AddRectFilled(faceMin,faceMax,ImGui::GetColorU32(face),5);
    palace::buttonFrame(draw,faceMin,faceMax,hover,press,disabled);
    const auto textSize=ImGui::CalcTextSize(label,nullptr,true);
    const char* labelEnd=std::strstr(label,"##");
    draw->AddText({(faceMin.x+faceMax.x-textSize.x)*.5f,(faceMin.y+faceMax.y-textSize.y)*.5f},ImGui::GetColorU32(ImGuiCol_Text),label,labelEnd);
    draw->PopClipRect();
    buttonSounds(pressed,closing);
    return pressed;
}

bool theatreButton(const char* label, ImVec2 size, ImVec4 tone = {.25f,.18f,.26f,1}) {
    return themedButton(label,size,tone,false);
}

// Buttons that go back or dismiss something use the "close" sound instead of "open".
bool backButton(const char* label, ImVec2 size, ImVec4 tone = {.25f,.18f,.26f,1}) {
    return themedButton(label,size,tone,true);
}

float approach(float current, bool active) {
    const float amount=std::clamp(ImGui::GetIO().DeltaTime*12.0f,0.0f,1.0f);
    return current+((active?1.0f:0.0f)-current)*amount;
}

const bj::RelicInfo& relicInfo(bj::Relic id) {
    const auto& catalog = bj::relicCatalog();
    return *std::find_if(catalog.begin(), catalog.end(), [id](const auto& item) { return item.id == id; });
}

void primaryAction(bj::Game& game, Commands& commands) {
    commands.apply([&]() {
        switch (game.state().phase) {
            case bj::Phase::Ready: return game.deal();
            case bj::Phase::PlayerTurn: return game.stand();
            case bj::Phase::RoundOver: return game.advance();
            case bj::Phase::Shop: return game.leaveShop();
            default: return false;
        }
    });
}

struct MenuLayout {
    ImVec2 origin;
    float width = 350;
    float height = 76;
    float step = 88;
};

MenuLayout menuLayout(ImVec2 display) {
    return {{display.x * 0.075f, display.y * 0.39f}, 350, display.y < 800 ? 64.0f : 76.0f, display.y < 800 ? 76.0f : 88.0f};
}

struct TextInkBounds { ImVec2 min, max; };

// The menu wordmark is ASCII. Glyph quads include Cormorant's ascenders and J
// descenders, unlike CalcTextSizeA's line box; use the actual rendered bounds.
TextInkBounds menuTitleInk(ImFont* font,float size,ImVec2 origin) {
    TextInkBounds bounds{{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},
                         {std::numeric_limits<float>::lowest(),std::numeric_limits<float>::lowest()}};
    const float scale=size/font->FontSize;
    float pen=std::floor(origin.x);
    const float y=std::floor(origin.y);
    for(const char letter:std::string("Jester Jack")) {
        const auto* glyph=font->FindGlyph(static_cast<ImWchar>(letter));
        if(glyph->Visible) {
            bounds.min.x=std::min(bounds.min.x,pen+glyph->X0*scale);
            bounds.min.y=std::min(bounds.min.y,y+glyph->Y0*scale);
            bounds.max.x=std::max(bounds.max.x,pen+glyph->X1*scale);
            bounds.max.y=std::max(bounds.max.y,y+glyph->Y1*scale);
        }
        pen+=glyph->AdvanceX*scale;
    }
    return bounds;
}

void drawMenu(bj::Game&, Session& session, const Interface& ui, GLFWwindow* window) {
    const auto display = ImGui::GetIO().DisplaySize;
    const float alpha=session.entrance.menuAlpha();
    auto* backdrop = ImGui::GetBackgroundDrawList();
    const auto ink=[&](int r,int g,int b,int a=255){return IM_COL32(r,g,b,static_cast<int>(a*alpha));};
    backdrop->AddRectFilledMultiColor({0,0},{display.x*.53f,display.y},
        ink(16,8,28,180),ink(16,8,28,0),ink(16,8,28,0),ink(16,8,28,210));
    const float scale=std::min(display.x/1440.0f,display.y/900.0f);
    const ImVec2 p{display.x*.07f,display.y*.045f};
    const float titleSize=std::min(208.0f*scale,display.x*.15f);
    const char* name="Jester Jack";
    const auto bounds=menuTitleInk(ui.title,titleSize,p);
    // A compact motley cap belongs to the wordmark, with its lower edge just
    // above the visible ascenders. Every layer shares the entrance fade.
    const float cx=(bounds.min.x+bounds.max.x)*.5f,cy=bounds.min.y-5*scale;
    backdrop->AddTriangleFilled({cx-29*scale,cy},{cx-45*scale,cy-24*scale},{cx-8*scale,cy-7*scale},ink(154,54,88));
    backdrop->AddTriangleFilled({cx-16*scale,cy},{cx,cy-33*scale},{cx+16*scale,cy},ink(212,170,82));
    backdrop->AddTriangleFilled({cx+8*scale,cy-7*scale},{cx+45*scale,cy-24*scale},{cx+29*scale,cy},ink(75,136,143));
    for(const ImVec2 bell: {ImVec2{cx-45*scale,cy-24*scale},ImVec2{cx,cy-33*scale},ImVec2{cx+45*scale,cy-24*scale}})
        backdrop->AddCircleFilled(bell,3*scale,ink(244,208,120),20);
    backdrop->AddText(ui.title,titleSize,{p.x+5*scale,p.y+6*scale},ink(18,7,24,220),name);
    backdrop->AddText(ui.title,titleSize,{p.x+1*scale,p.y+2*scale},ink(156,101,45),name);
    backdrop->AddText(ui.title,titleSize,p,ink(255,232,181),name);
    const float lineY=bounds.max.y+12*scale; // Below the descender AND its shadow.
    backdrop->AddLine({bounds.min.x,lineY},{bounds.max.x,lineY},ink(211,164,78,180),1.5f);
    const char* subtitle="THE HOUSE OF BEAUTIFUL MISFORTUNE";
    backdrop->AddText(ui.small,18*scale,{bounds.min.x+6*scale,lineY+10*scale},ink(228,196,144),subtitle);
    if(session.collection || session.deckSelection) return;
    const auto layout=menuLayout(display);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    beginPanel("##menu",layout.origin,{layout.width,4*layout.step},true);
    ImGui::BeginDisabled(session.transitioning()||session.settings||session.rules);
    ImGui::PushFont(ui.menuButton);
    const auto button=[&](const char* label,ImVec4 tone){return theatreButton(label,{layout.width,layout.height},tone);};
    if(button("START",{.16f,.26f,.23f,1})) session.deckSelection=true;
    ImGui::SetCursorPos({0,layout.step});
    if(button("OPTIONS",{.20f,.15f,.24f,1})) session.settings=true;
    ImGui::SetCursorPos({0,2*layout.step});
    if(button("COLLECTION",{.24f,.15f,.29f,1})) session.collection=true;
    ImGui::SetCursorPos({0,3*layout.step});
    if(button("EXIT",{.28f,.13f,.19f,1})) glfwSetWindowShouldClose(window,GLFW_TRUE);
    ImGui::PopFont();ImGui::EndDisabled();ImGui::End();ImGui::PopStyleVar(2);
    backdrop->AddText({layout.origin.x,display.y-49},ink(234,213,190,225),"Choose your deck. Challenge the house.");
}

ImVec4 rarityColor(bj::Rarity rarity) {
    switch (rarity) {
        case bj::Rarity::Common: return {0.70f, 0.72f, 0.72f, 1};
        case bj::Rarity::Rare: return {0.39f, 0.66f, 0.88f, 1};
        case bj::Rarity::Legendary: return {0.98f, 0.73f, 0.32f, 1};
    }
    return cream;
}

void rarityBadge(bj::Rarity rarity, const Interface& ui) {
    const auto color = ImGui::ColorConvertFloat4ToU32(rarityColor(rarity));
    const char* label = bj::rarityName(rarity);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 text = ui.button->CalcTextSizeA(26, 1000, 0, label);
    ImGui::Dummy({text.x + 20, 32});
    auto* draw = ImGui::GetWindowDrawList();
    const auto tint=rarityColor(rarity);
    draw->AddRectFilled(origin, {origin.x + text.x + 20, origin.y + 32},
        ImGui::ColorConvertFloat4ToU32({tint.x*.18f,tint.y*.18f,tint.z*.18f,1}), 5);
    draw->AddRect(origin,{origin.x+text.x+20,origin.y+32},color,5,0,1);
    draw->AddText(ui.button, 26, {origin.x + 10, origin.y + 3}, color, label);
}

void drawCoinIcon(ImDrawList* draw, ImVec2 center, float radius, const Interface& ui) {
    const float scale=radius/18.0f;
    const ImU32 rim = ImGui::GetColorU32({166/255.0f,103/255.0f,27/255.0f,1});
    draw->AddCircleFilled({center.x - 7*scale, center.y + 4*scale}, radius, rim, 32);
    draw->AddCircleFilled(center, radius, ImGui::GetColorU32(gold), 32);
    draw->AddCircle(center, radius - 4*scale, rim, 32, std::max(.75f,2*scale));
    const float size=26*scale;
    const ImVec2 mark = ui.button->CalcTextSizeA(size, 1000, 0, "$");
    draw->AddText(ui.button, size, {center.x - mark.x * .5f, center.y - mark.y * .5f}, rim, "$");
}

// These reserve their complete text/icon footprint; no monetary value is encoded in an image.
void shopCoinText(const std::string& label, const Interface& ui, ImVec4 color=gold) {
    const float fontSize=ImGui::GetFontSize(),radius=fontSize*.38f,gap=6;
    const auto textSize=ImGui::CalcTextSize(label.c_str());
    const auto origin=ImGui::GetCursorScreenPos();
    const float iconLeft=origin.x+textSize.x+gap;
    ImGui::Dummy({textSize.x+gap+radius*(2+7.0f/18.0f),std::max(fontSize,radius*(2+4.0f/18.0f))});
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddText(origin,ImGui::GetColorU32(color),label.c_str());
    drawCoinIcon(draw,{iconLeft+radius*(1+7.0f/18.0f),origin.y+fontSize*.5f},radius,ui);
}

bool shopCoinButton(const std::string& caption, int amount, ImVec2 size, ImVec4 tone,
                    const Interface& ui, const char* id) {
    const float radius=std::min(size.y*.22f,ImGui::GetFontSize()*.35f);
    const std::string text=caption+" "+std::to_string(amount);
    const float gap=5,iconWidth=radius*(2+7.0f/18.0f);
    // Blank display text leaves painting to this wrapper while the native button retains input,
    // focus, disabled behavior, sound and a stable ID across changing prices.
    const std::string label="##"+std::string(id);
    const bool pressed=theatreButton(label.c_str(),size,tone);
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    ImGui::PushID(label.c_str());
    const auto hoverId=ImGui::GetID("hover-motion"),pressId=ImGui::GetID("press-motion");
    ImGui::PopID();
    const auto* storage=ImGui::GetStateStorage();
    const float hover=storage->GetFloat(hoverId),press=storage->GetFloat(pressId);
    const float lift=std::min(2.4f,(b.y-a.y)*.04f)*hover*(1-press);
    const float fontSize=ImGui::GetFontSize();
    const float available=std::max(1.0f,b.x-a.x-16-gap-iconWidth);
    const float textWidth=ImGui::CalcTextSize(text.c_str()).x;
    const float sizeText=fontSize*std::min(1.0f,available/std::max(1.0f,textWidth));
    const auto fitted=ImGui::GetFont()->CalcTextSizeA(sizeText,10000,0,text.c_str());
    const float x=(a.x+b.x-fitted.x-gap-iconWidth)*.5f;
    const float y=(a.y+b.y)*.5f+.5f-lift+press;
    auto* draw=ImGui::GetWindowDrawList();
    draw->PushClipRect(a,b,true);
    draw->AddText(ImGui::GetFont(),sizeText,{x,y-fitted.y*.5f},ImGui::GetColorU32(ImGuiCol_Text),text.c_str());
    drawCoinIcon(draw,{x+fitted.x+gap+radius*(1+7.0f/18.0f),y},radius,ui);
    draw->PopClipRect();
    return pressed;
}

void drawChip(ImDrawList* draw, ImVec2 c, float r, const bj::RelicInfo& item,
              const ArtAssets& art, float tilt=0.0f) {
    const auto accent=ImGui::ColorConvertFloat4ToU32(rarityColor(item.rarity));
    draw->AddCircleFilled({c.x+2,c.y+6},r,IM_COL32(0,0,0,125),64);
    draw->AddCircleFilled(c,r,IM_COL32(45,34,35,255),64);
    draw->AddCircle(c,r-1,accent,64,1.5f);
    const float cs=std::cos(tilt),sn=std::sin(tilt);
    const auto corner=[&](float x,float y) {return ImVec2{c.x+x*cs-y*sn,c.y+x*sn+y*cs};};
    const int id=static_cast<int>(item.id);
    if(id>=24 && !art.expansion) {palace::relicSigil(draw,c,r,id,accent);return;}
    const auto& atlas=id>=24?art.expansion:art.relics;
    ImVec2 a,b;
    if(id>=24) {
        // Native painterly atlas: its medallions are packed in uneven rows.
        // These are real image bounds in ID order (24..39), not a nominal grid.
        // Sampling the outer pixel centers keeps complete rims without pulling
        // pixels from the adjacent medallion at these tightly packed boundaries.
        constexpr int bounds[16][4]={
            {15,6,318,303}, {323,6,622,302}, {630,5,932,303}, {941,6,1243,302},
            {10,305,316,604}, {322,305,623,604}, {628,305,933,604}, {940,306,1243,604},
            {11,604,316,905}, {321,606,621,905}, {628,604,934,905}, {940,606,1244,905},
            {11,906,319,1210}, {320,905,623,1210}, {627,905,933,1212}, {939,909,1245,1212}
        };
        const auto& rect=bounds[id-24];
        a={(rect[0]+.5f)/atlas.width(),(rect[1]+.5f)/atlas.height()};
        b={(rect[2]-.5f)/atlas.width(),(rect[3]-.5f)/atlas.height()};
    } else {
        const int col=id%6,row=id/6;
        // Preserve the original atlas's slightly irregular vertical packing.
        constexpr int rowTop[]={8,252,500,748};
        a={(col*256+4.5f)/atlas.width(),(rowTop[row]+.5f)/atlas.height()};
        b={(col*256+251.5f)/atlas.width(),(rowTop[row]+247.5f)/atlas.height()};
    }
    const auto texture=static_cast<ImTextureID>(atlas.id());
    const float image=r*.96f;
    draw->AddImageQuad(texture,corner(-image,-image),corner(image,-image),corner(image,image),corner(-image,image),
        a,{b.x,a.y},b,{a.x,b.y});
    draw->AddCircle(c,r*.88f,IM_COL32(255,237,203,30),64,1);
}

void drawHandIcon(ImDrawList* draw, ImVec2 c, float r) {
    const auto color=ImGui::ColorConvertFloat4ToU32(blue);
    draw->AddRectFilled({c.x-r*.6f,c.y-r*.1f},{c.x+r*.55f,c.y+r*.7f},color,r*.25f);
    for(int i=0;i<4;++i) {
        const float x=c.x-r*.5f+i*r*.29f;
        const float top=c.y-r*(i==1?1.05f:i==0?.75f:i==2?.92f:.6f);
        draw->AddRectFilled({x,top},{x+r*.21f,c.y+r*.2f},color,r*.11f);
    }
    draw->AddLine({c.x-r*.45f,c.y+r*.35f},{c.x-r*.98f,c.y-r*.08f},color,r*.32f);
}

struct TableLayout {
    float sidebar;
    float contentX;
    float contentWidth;
    float boardY;
    float boardHeight;
    float actionY;
};

struct EquipmentLayout {
    float cell, radius, centerY, detailTop, detailHeight;
    float bodyTop() const { return detailTop+40; }
    float bottom() const { return detailTop+detailHeight; }
};

EquipmentLayout equipmentLayout(float contentWidth) {
    const float radius=contentWidth<900?34.0f:40.0f;
    const float centerY=28+radius;
    return {contentWidth/8,radius,centerY,centerY+radius+8,94};
}

TableLayout tableLayout(ImVec2 display) {
    const float sidebar = std::clamp(display.x * .24f, 280.0f, 340.0f);
    const float contentX = sidebar + 24;
    const float actionY = display.y - 154;
    const float contentWidth=display.x-contentX-20;
    const float boardY=equipmentLayout(contentWidth).bottom()+8;
    return {sidebar, contentX, contentWidth, boardY, display.y - boardY - 110, actionY};
}

void drawGameplayShading(const Session& session, const presentation::Presentation& timeline) {
    const auto phase=timeline.visible().phase;
    if(session.menu || phase==bj::Phase::Shop || (phase==bj::Phase::RunLost && !timeline.busy())) return;
    const auto display=ImGui::GetIO().DisplaySize;
    const auto layout=tableLayout(display);
    auto* draw=ImGui::GetBackgroundDrawList();
    constexpr ImU32 shade=IM_COL32(0,0,0,48),clear=IM_COL32(0,0,0,0);
    draw->AddRectFilledMultiColor({layout.contentX,0},{display.x,140},shade,shade,clear,clear);
    draw->AddRectFilledMultiColor({layout.contentX,display.y-140},display,clear,clear,shade,shade);
}

const char* ownedRelicDescription(const bj::RelicInfo& item, bool rescueSpent) {
    return item.id==bj::Relic::SecondChance && rescueSpent
        ?"Rescue spent this round. Win the rescued hand: +1.0 multiplier.":item.description;
}

struct WrappedRelicText {
    std::string text;
    float longestWord=0;
};

WrappedRelicText wrapRelicWords(ImFont* font, float size, float width, const char* text) {
    WrappedRelicText wrapped;
    std::istringstream words(text);
    std::string word,line;
    while(words>>word) {
        wrapped.longestWord=std::max(wrapped.longestWord,font->CalcTextSizeA(size,10000,0,word.c_str()).x);
        const std::string candidate=line.empty()?word:line+" "+word;
        if(!line.empty() && font->CalcTextSizeA(size,10000,0,candidate.c_str()).x>width) {
            wrapped.text+=line+'\n';
            line=word;
        } else line=candidate;
    }
    wrapped.text+=line;
    return wrapped;
}

void verifyRelicTextLayout(const Interface& ui, const TableLayout& layout, ImVec2 display) {
    const auto strip=equipmentLayout(layout.contentWidth);
    const float width=layout.contentWidth-220;
    float maxBottom=0;
    for(const auto& item:bj::relicCatalog()) {
        const auto wrapped=wrapRelicWords(ImGui::GetIO().FontDefault,22,width,item.description);
        const float bottom=strip.bodyTop()+ImGui::GetIO().FontDefault->CalcTextSizeA(22,10000,0,wrapped.text.c_str()).y;
        const std::string title=std::string(item.name)+" / "+bj::rarityName(item.rarity);
        if(wrapped.longestWord>width || bottom>strip.bottom()-6 ||
            ui.button->CalcTextSizeA(24,10000,0,title.c_str()).x>width)
            throw std::runtime_error(std::string("Equipment detail overflows: ")+item.name);
        maxBottom=std::max(maxBottom,bottom);
    }
    if(strip.radius*2+12>strip.cell) throw std::runtime_error("Equipment chips overlap");
    std::cout<<"Equipment layout "<<static_cast<int>(display.x)<<'x'<<static_cast<int>(display.y)
             <<": 8 slots, detail bottom "<<maxBottom<<" / "<<layout.boardY<<'\n';
}

float controlsLeft(const TableLayout& layout) {
    constexpr float width = 456;
    const float tableCenter = layout.contentX + layout.contentWidth * .5f;
    const float handCenter = layout.contentX + layout.contentWidth - 105;
    // Leave room for two large hand digits at the minimum window width.
    const float center = std::min(tableCenter, handCenter - 80 - width * .5f);
    return center - width * .5f;
}

void relicTooltip(const bj::RelicInfo& item, const Interface& ui) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20);
    ImGui::TextColored(rarityColor(item.rarity), "%s / %s", item.name, bj::rarityName(item.rarity));
    ImGui::TextWrapped("%s", item.description);
    ImGui::PushFont(ui.small);
    shopCoinText("Buy: "+std::to_string(item.cost),ui);
    ImGui::SameLine(0,18);
    shopCoinText("Sell: "+std::to_string(item.cost/2),ui);
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void drawRelicStrip(const bj::Snapshot& state, const presentation::Presentation& timeline,
                    const TableLayout& layout, Session& session, const Interface& ui,
                    bj::Game& game, Commands& commands, const ArtAssets& art) {
    if(session.selectedEquipment>=static_cast<int>(state.owned.size())) session.selectedEquipment=-1;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{4,4});
    beginPanel("##equipment",{layout.contentX,0},{layout.contentWidth,layout.boardY},true,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    auto* draw=ImGui::GetWindowDrawList();const auto origin=ImGui::GetWindowPos();
    ImGui::PushFont(ui.small);
    ImGui::TextColored(gold,"CURIOSITIES  /  %d OF 8",static_cast<int>(state.owned.size()));
    if(state.owned.empty()) {
        ImGui::SameLine();ImGui::TextColored(muted,"Win a round to visit the cabinet.");
    } else {
        ImGui::SameLine();ImGui::TextColored(muted,"1-8 inspect  /  A activate  /  Esc close");
    }
    const auto strip=equipmentLayout(layout.contentWidth);
    const float cell=strip.cell;
    bool pointerOnOccupiedSlot=false;
    for(int slot=0;slot<static_cast<int>(bj::maxEquipmentSlots);++slot) {
        ImGui::PushID(slot);
        const bool filled=slot<static_cast<int>(state.owned.size());
        const ImVec4 accent=filled?rarityColor(relicInfo(state.owned[static_cast<std::size_t>(slot)]).rarity):ImVec4(.35f,.31f,.29f,1);
        const float x=(slot+.5f)*cell;
        const ImVec2 center{origin.x+x,origin.y+strip.centerY};
        if(!filled) {
            const std::string number=std::to_string(slot+1);
            draw->AddCircle(center,strip.radius-1,IM_COL32(160,143,118,40),64,1);
            draw->AddCircle(center,strip.radius-5,IM_COL32(160,143,118,18),64,1);
            const auto size=ui.small->CalcTextSizeA(18,1000,0,number.c_str());
            draw->AddText(ui.small,18,{center.x-size.x*.5f,center.y-size.y*.5f},IM_COL32(160,143,118,82),number.c_str());
            ImGui::PopID();continue;
        }
        const auto id=state.owned[static_cast<std::size_t>(slot)];const auto status=bj::abilityStatus(game.state(),id);
        ImGui::SetCursorPos({x-strip.radius-4,strip.centerY-strip.radius-4});
        ImGui::PushStyleColor(ImGuiCol_Header,{0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,{0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,{0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_NavCursor,{0,0,0,0});
        const bool selectedSlot=ImGui::Selectable("##slot",session.selectedEquipment==slot,0,{strip.radius*2+8,strip.radius*2+8});
        ImGui::PopStyleColor(4);
        buttonSounds(selectedSlot,false);
        if(selectedSlot) {
            if(session.selectedEquipment==slot) session.clearEquipmentContext();
            else {session.selectedEquipment=slot;session.inspectedRelic=id;}
        }
        pointerOnOccupiedSlot=pointerOnOccupiedSlot||ImGui::IsItemHovered();
        const bool hovered=!session.suppressEquipmentHover&&!session.settings&&!session.rules&&!session.transitioning()&&ImGui::IsItemHovered();
        session.relicHover[slot]=approach(session.relicHover[slot],hovered||session.hoveredRelic==slot);
        if(hovered) {session.inspectedRelic=id;session.hoveredRelic=slot;}
        const bool reacting=session.relicReactionStep>0&&static_cast<std::size_t>(session.relicReactionStep)<=state.score.appliedEffects.size()&&state.score.appliedEffects[session.relicReactionStep-1].relic==id;
        const bool focused=session.selectedEquipment==slot||(ImGui::GetIO().NavVisible&&ImGui::IsItemFocused());
        const ImVec2 lifted{center.x,center.y-session.relicHover[slot]*4};
        draw->AddCircleFilled({center.x,center.y+3},strip.radius+2,IM_COL32(0,0,0,95),64);
        if(focused||hovered||reacting) {
            const float glow=reacting?.22f:focused?.13f:.08f;
            draw->AddCircle(lifted,strip.radius+3,ImGui::ColorConvertFloat4ToU32({accent.x,accent.y,accent.z,glow}),64,6);
            draw->AddCircle(lifted,strip.radius+2,reacting?IM_COL32(255,221,154,235):ImGui::ColorConvertFloat4ToU32(accent),64,focused||reacting?1.7f:1);
        }
        drawChip(draw,lifted,strip.radius,relicInfo(id),art);
        const std::string number=std::to_string(slot+1);
        const ImVec2 key{lifted.x-strip.radius+3,lifted.y+strip.radius-5};
        draw->AddCircleFilled(key,10,IM_COL32(19,15,22,235),32);
        const auto keySize=ui.small->CalcTextSizeA(16,1000,0,number.c_str());
        draw->AddText(ui.small,16,{key.x-keySize.x*.5f,key.y-keySize.y*.5f},ImGui::ColorConvertFloat4ToU32(accent),number.c_str());
        if(bj::isActiveRelic(id)) {
            const std::string charge=std::to_string(status.remaining)+"/"+std::to_string(status.maximum);
            const auto color=ImGui::ColorConvertFloat4ToU32(status.usable?green:muted);
            const ImVec2 badge{lifted.x+strip.radius-6,lifted.y+strip.radius-8};
            draw->AddCircleFilled(badge,14,IM_COL32(24,17,29,245),32);
            draw->AddCircle(badge,14,color,32,1);
            const auto size=ui.small->CalcTextSizeA(14,1000,0,charge.c_str());
            draw->AddText(ui.small,14,{badge.x-size.x*.5f,badge.y-size.y*.5f},color,charge.c_str());
        }
        ImGui::PopID();
    }
    // Preserve a click on an occupied icon, its context card or a physical chip.
    // Empty slots, header space, the sidebar and the rest of the table dismiss it.
    const bool hasDetail=session.hoveredRelic>=0||session.selectedEquipment>=0;
    const bool pointerOnDetail=hasDetail && state.cardChoices.empty() &&
        ImGui::IsMouseHoveringRect({origin.x+4,origin.y+strip.detailTop},{origin.x+layout.contentWidth-5,origin.y+strip.bottom()});
    if(ImGui::IsMouseClicked(0) && !pointerOnOccupiedSlot && !pointerOnDetail && !session.physicalChipHovered)
        session.clearEquipmentContext();
    const int selected=session.hoveredRelic>=0?session.hoveredRelic:session.selectedEquipment;
    if(state.cardChoices.empty()&&selected>=0&&selected<static_cast<int>(state.owned.size())) {
        const auto& item=relicInfo(state.owned[static_cast<std::size_t>(selected)]);
        const auto status=bj::abilityStatus(game.state(),item.id);
        draw->AddRectFilled({origin.x+4,origin.y+strip.detailTop},{origin.x+layout.contentWidth-5,origin.y+strip.bottom()},IM_COL32(24,19,29,235),6);
        palace::panelFrame(draw,{origin.x+4,origin.y+strip.detailTop},{layout.contentWidth-9,strip.detailHeight},false);
        const std::string title=std::string(item.name)+" / "+bj::rarityName(item.rarity);
        draw->AddText(ui.button,24,{origin.x+16,origin.y+strip.detailTop+7},ImGui::GetColorU32(rarityColor(item.rarity)),title.c_str());
        const auto wrapped=wrapRelicWords(ImGui::GetIO().FontDefault,22,layout.contentWidth-220,item.description);
        draw->AddText(ImGui::GetIO().FontDefault,22,{origin.x+16,origin.y+strip.bodyTop()},ImGui::GetColorU32(ImGuiCol_Text),wrapped.text.c_str());
        if(bj::isActiveRelic(item.id)) {
            ImGui::SetCursorPos({layout.contentWidth-188,strip.detailTop+8});
            ImGui::BeginDisabled(commands.blocked||commands.used||!status.usable);
            if(theatreButton("ACTIVATE / A",{170,34},{.19f,.28f,.24f,1})) {
                session.selectedEquipment=selected;
                commands.apply([&]{return game.activate(item.id);},true);
            }
            ImGui::EndDisabled();
            const auto reason=wrapRelicWords(ui.small,18,170,status.reason);
            draw->AddText(ui.small,18,{origin.x+layout.contentWidth-188,origin.y+strip.detailTop+50},
                ImGui::GetColorU32(status.usable?green:muted),reason.text.c_str());
        } else {
            ImGui::SetCursorPos({layout.contentWidth-160,strip.detailTop+16});ImGui::TextColored(muted,"PASSIVE EFFECT");
        }
    }
    ImGui::PopFont();ImGui::End();ImGui::PopStyleVar();(void)timeline;
}

void drawSidebar(const bj::Snapshot& state, const TableLayout& layout, Session& session, const Commands& commands,
                 const Interface& ui, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 14});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8, 8});
    beginPanel("##run", {16, 16}, {layout.sidebar - 16, height - 32});
    ImGui::PushFont(ui.button);
    ImGui::TextColored(gold, "ROUND %d", state.round);
    ImGui::PopFont();
    ImGui::Separator();

    ImGui::Spacing();
    ImGui::PushFont(ui.small);
    ImGui::TextColored(muted, "ROUND PROGRESS");
    ImGui::PopFont();
    ImGui::PushFont(ui.number);
    ImGui::TextColored(cream, "%d", state.roundScore);
    ImGui::PopFont();
    ImGui::SameLine(0, 10);
    ImGui::PushFont(ui.button);
    ImGui::TextColored(gold, "/ %d", state.goal);
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, gold);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, {.12f,.19f,.20f,1});
    ImGui::ProgressBar(std::min(1.0f, static_cast<float>(state.roundScore) / state.goal), {-1, 6}, "");
    ImGui::PopStyleColor(2);
    ImGui::PushFont(ui.small);
    if(state.roundScore >= state.goal) ImGui::TextColored(green, "TARGET REACHED");
    else ImGui::TextColored(muted, "%d points to the next round", state.goal-state.roundScore);
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float half = (ImGui::GetContentRegionAvail().x - 8) * .5f;
    for (int column = 0; column < 2; ++column) {
        if (column) ImGui::SameLine();
        ImGui::PushID(column);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.075f, .115f, .12f, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14, 8});
        ImGui::BeginChild("counter", {half, 92}, ImGuiChildFlags_Borders);
        ImGui::PushFont(ui.small);
        ImGui::TextUnformatted(column ? "COINS" : "HANDS LEFT");
        ImGui::PopFont();
        ImGui::PushFont(ui.number);
        ImGui::TextColored(column ? gold : blue, "%d", column ? state.coins : state.handsLeft);
        const auto pos=ImGui::GetWindowPos();
        const ImVec2 center{pos.x+half-32,pos.y+56};
        if(column) drawCoinIcon(ImGui::GetWindowDrawList(),center,18,ui);
        else drawHandIcon(ImGui::GetWindowDrawList(),center,18);
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::PushFont(ui.small);
    if (state.startDeck == bj::StartDeck::SecondLife) ImGui::TextColored(gold, "REVIVE: %d / 1", state.revivesLeft);
    ImGui::TextColored(muted, "RUN TOTAL  %d", state.totalScore);
    ImGui::Spacing();
    ImGui::TextColored(gold, "SCORE JOURNAL");
    const float journalHeight=std::max(60.0f,height-32-120-ImGui::GetCursorPosY());
    ImGui::BeginChild("score-journal",{-1,journalHeight},ImGuiChildFlags_None);
    if(session.history.records().empty()) {
        ImGui::TextColored(cream,"Your story starts here.");
        ImGui::TextWrapped("Finish a hand to record its score and the relics that contributed.");
    }
    int recordIndex=0;
    for(const auto& record:session.history.records()) {
        ImGui::PushID(recordIndex++);
        const auto& score=record.score;
        const char* result=record.outcome==bj::Outcome::Win?"WIN":
            record.outcome==bj::Outcome::Loss?"LOSS":"PUSH";
        palace::sectionRule(ImGui::GetWindowDrawList(),ImGui::GetCursorScreenPos(),ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(muted,"R%d / HAND %d%s",record.round,record.hand,record.attempt>1?" / RETRY":"");
        ImGui::TextColored(record.outcome==bj::Outcome::Win?green:cream,
                           "%s   +%d pts / +%d coins",result,score.total,score.coins);
        if(record.outcome==bj::Outcome::Win) {
            ImGui::TextColored(cream,"(%d base + %d bonus) x %.2f",score.base,score.bonus,score.multiplier);
            for(const auto& effect:score.appliedEffects) {
                const auto& catalog=bj::relicCatalog();
                const auto item=std::find_if(catalog.begin(),catalog.end(),[&](const auto& info){return info.id==effect.relic;});
                if(item==catalog.end()) continue;
                std::ostringstream contribution;
                contribution<<item->name<<":";
                if(effect.points) contribution<<" +"<<effect.points<<" pts";
                if(effect.multiplierBonus>0) contribution<<" +"<<std::fixed<<std::setprecision(2)<<effect.multiplierBonus<<" mult";
                if(effect.coins) contribution<<" +"<<effect.coins<<" coins";
                ImGui::TextWrapped("%s",contribution.str().c_str());
            }
        } else ImGui::TextColored(muted,"No points awarded");
        if(record.roundScore>=record.goal) ImGui::TextColored(gold,"ROUND TARGET REACHED");
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopFont();

    ImGui::SetCursorPosY(height - 32 - 108);
    ImGui::BeginDisabled(commands.used || session.settings || session.rules || session.transitioning());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12, 4});
    if (theatreButton("HOW TO PLAY", {-1, 36})) session.rules = true;
    ImGui::PopStyleVar();
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - 8) * .5f;
    ImGui::PushFont(ui.button);
    if (backButton("MENU", {buttonWidth, 46})) session.returnToMenu();
    ImGui::SameLine();
    if (theatreButton("OPTIONS", {buttonWidth, 46})) session.settings = true;
    ImGui::PopFont();
    ImGui::EndDisabled();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// Effects stay in the border gutter; rarity text remains the non-color signal.
void drawRarityFrame(ImDrawList* draw, ImVec2 origin, ImVec2 size, bj::Rarity rarity, bool animate) {
    const bool legendary=rarity==bj::Rarity::Legendary;
    const bool rare=rarity==bj::Rarity::Rare;
    const auto accent=rarityColor(rarity);
    const ImU32 ink=ImGui::GetColorU32(accent);
    const ImVec2 end{origin.x+size.x-2,origin.y+size.y-2};
    for(int layer=0;layer<3;++layer) {
        const float inset=3.5f+layer*2;
        draw->AddRect({origin.x+inset,origin.y+inset},{end.x-inset+2,end.y-inset+2},
            ImGui::GetColorU32({accent.x,accent.y,accent.z,(legendary?.07f:rare?.045f:.022f)/(layer+1)}),
            5,0,3);
    }
    draw->AddRect({origin.x+2,origin.y+2},end,ink,5,0,legendary?2.5f:rare?1.5f:1.0f);
    if(!legendary&&!rare) return;
    if(legendary) draw->AddRect({origin.x+6,origin.y+6},{end.x-4,end.y-4},
                              ImGui::GetColorU32({207/255.0f,150/255.0f,62/255.0f,.39f}),3,0,1);
    for(float side : {0.0f,1.0f}) {
        const float x=origin.x+8+side*(size.x-16),dir=side? -1.0f:1.0f;
        for(float bottom : {0.0f,1.0f}) {
            const float y=origin.y+8+bottom*(size.y-16),dy=bottom?-1.0f:1.0f;
            draw->AddLine({x,y},{x+dir*(legendary?26:15),y},ink,2);
            draw->AddLine({x,y},{x,y+dy*(legendary?26:15)},ink,2);
        }
    }
    if(rare) {
        const float t=animate?effectTime:0;
        const float x=origin.x+20+(size.x-40)*(.5f+.5f*std::sin(t*.32f));
        draw->AddRect({origin.x+4,origin.y+4},{end.x-2,end.y-2},ImGui::GetColorU32({.39f,.66f,.88f,.14f}),4,0,3);
        draw->AddLine({x-14,origin.y+3},{x+14,origin.y+3},ImGui::GetColorU32({.71f,.87f,.98f,.48f}),1.5f);
    }
    if(legendary) {
        const float t=animate?effectTime:0;
        const float x=origin.x+12+(size.x-24)*(.5f+.5f*std::sin(t*.45f));
        draw->AddLine({x-18,origin.y+3},{x+18,origin.y+3},ImGui::GetColorU32({1,.92f,.65f,.76f}),2);
        for(int i=0;i<3;++i) {
            const float px=origin.x+20+(size.x-40)*(i+1)/4;
            const float alpha=.45f+.15f*std::sin(t*.8f+i*2);
            const auto glow=ImGui::GetColorU32({1,.83f,.43f,alpha});
            draw->AddLine({px-3,end.y-4},{px+3,end.y-4},glow,1);
            draw->AddLine({px,end.y-7},{px,end.y-1},glow,1);
        }
    }
}

float shopRelicHover(bool hovered, bool animate) {
    auto* storage=ImGui::GetStateStorage();
    const auto id=ImGui::GetID("shop-relic-hover"),frameId=ImGui::GetID("shop-relic-frame");
    const bool continuous=storage->GetInt(frameId,-2)==ImGui::GetFrameCount()-1;
    const float value=animate?approach(continuous?storage->GetFloat(id):0,hovered):0;
    storage->SetFloat(id,value);storage->SetInt(frameId,ImGui::GetFrameCount());
    return value;
}

void drawShopRelic(ImDrawList* draw, ImVec2 center, float radius, const bj::RelicInfo& item,
                   const ArtAssets& art, bool animate, float hover) {
    center.y-=std::min(3.0f,radius*.08f)*hover;
    const auto accent=rarityColor(item.rarity);
    const float glow=item.rarity==bj::Rarity::Legendary?.10f:item.rarity==bj::Rarity::Rare?.07f:.035f;
    for(int layer=3;layer>0;--layer)
        draw->AddCircle(center,radius+layer*1.8f,
            ImGui::GetColorU32({accent.x,accent.y,accent.z,(glow+hover*.08f)/layer}),64,2);
    drawChip(draw,center,radius,item,art);
    const float phase=animate?effectTime*.24f+static_cast<int>(item.id)*.41f:.7f;
    draw->PathArcTo(center,radius*.93f,phase,phase+.52f,12);
    draw->PathStroke(ImGui::GetColorU32({1,.94f,.79f,.14f+hover*.14f}),0,1.4f);
}

void drawShop(bj::Game& game, Commands& commands, Session& session, const Interface& ui, const ArtAssets& art, bool animate) {
    const auto state=game.state();
    const auto display=ImGui::GetIO().DisplaySize;
    const float width=std::min(1100.0f,display.x-48),height=std::min(790.0f,display.y-40);
    ImGui::GetBackgroundDrawList()->AddRectFilled({0,0},display,IM_COL32(8,9,18,200));
    beginPanel("##shop",{(display.x-width)*.5f,(display.y-height)*.5f},{width,height});
    ImGui::PushFont(ui.heading);ImGui::TextColored(cream,"The Curiosity Cabinet");ImGui::PopFont();
    const std::string balance=std::to_string(state.coins);
    const float balanceWidth=ui.number->CalcTextSizeA(36,10000,0,balance.c_str()).x+48;
    ImGui::SameLine(width-24-balanceWidth);
    const auto balanceOrigin=ImGui::GetCursorScreenPos();
    ImGui::Dummy({balanceWidth,36});
    drawCoinIcon(ImGui::GetWindowDrawList(),{balanceOrigin.x+23,balanceOrigin.y+18},16,ui);
    ImGui::GetWindowDrawList()->AddText(ui.number,36,{balanceOrigin.x+48,balanceOrigin.y},ImGui::GetColorU32(gold),balance.c_str());
    ImGui::PushFont(ui.small);
    ImGui::TextColored(muted,"%d / %d EQUIPPED   /   Select an equipped relic to trade when full.",static_cast<int>(state.owned.size()),static_cast<int>(bj::maxEquipmentSlots));
    ImGui::PopFont();ImGui::Separator();
    const bool full=state.owned.size()>=bj::maxEquipmentSlots;
    if(session.shopReplacement>=static_cast<int>(state.owned.size())) session.shopReplacement=-1;
    const float offerY=ImGui::GetCursorPosY()+5,offerHeight=height<700?242.0f:277.0f;
    const float cardWidth=(width-54)*.5f;
    for(std::size_t i=0;i<state.offers.size();++i) {
        const auto& item=relicInfo(state.offers[i]);
        ImGui::PushID(static_cast<int>(item.id));ImGui::SetCursorPos({20+static_cast<float>(i)*(cardWidth+14),offerY});
        ImGui::PushStyleColor(ImGuiCol_ChildBg,item.rarity==bj::Rarity::Legendary?ImVec4(.16f,.125f,.07f,1):ImVec4(.085f,.105f,.12f,1));
        ImGui::BeginChild("offer",{cardWidth,offerHeight},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar);
        const auto origin=ImGui::GetWindowPos();
        drawRarityFrame(ImGui::GetWindowDrawList(),origin,{cardWidth,offerHeight},item.rarity,animate);
        const float radius=height<700?55.0f:66.0f;
        const float hover=shopRelicHover(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows),animate);
        drawShopRelic(ImGui::GetWindowDrawList(),{origin.x+radius+17,origin.y+radius+18},radius,item,art,animate,hover);
        const float textX=radius*2+34;
        ImGui::SetCursorPos({textX,14});ImGui::PushFont(ui.button);ImGui::PushTextWrapPos(cardWidth-14);
        ImGui::TextColored(cream,"%s",item.name);ImGui::PopTextWrapPos();ImGui::PopFont();
        ImGui::SetCursorPosX(textX);rarityBadge(item.rarity,ui);
        ImGui::SetCursorPosX(textX);ImGui::PushFont(ui.small);ImGui::PushTextWrapPos(cardWidth-14);
        ImGui::TextWrapped("%s",item.description);ImGui::PopTextWrapPos();ImGui::PopFont();
        const bool owned=game.hasRelic(item.id),purchased=game.boughtThisShop(item.id);
        const bool selected=full&&session.shopReplacement>=0;
        const int refund=selected?relicInfo(state.owned[static_cast<std::size_t>(session.shopReplacement)]).cost/2:0;
        const int net=item.cost-refund;
        const bool affordable=state.coins+refund>=item.cost;
        const bool priced=!owned&&!purchased&&(!full||selected);
        const std::string caption=!affordable?"NEED":selected?(net<0?"TRADE / RECEIVE":"TRADE /"):"BUY /";
        const std::string label=owned?"EQUIPPED":purchased?"SOLD THIS VISIT":"SELECT A RELIC TO TRADE";
        ImGui::SetCursorPos({12,offerHeight-57});ImGui::PushFont(ui.button);
        ImGui::BeginDisabled(commands.blocked||commands.used||owned||purchased||!affordable||(full&&!selected));
        const bool purchase=priced?shopCoinButton(caption,net<0?-net:net,{cardWidth-24,43},{.23f,.18f,.26f,1},ui,"purchase"):
            theatreButton(label.c_str(),{cardWidth-24,43},{.23f,.18f,.26f,1});
        if(purchase) {
            commands.apply([&]{return selected?game.replace(state.owned[static_cast<std::size_t>(session.shopReplacement)],item.id):game.buy(item.id);});
            if(commands.used) {
                playSound(audio::Sfx::ChipsBet); // Coins are paid across the table.
                session.shopReplacement=-1;session.selectedEquipment=-1;
            }
        }
        ImGui::EndDisabled();
        // A click on an offer that cannot be bought (too expensive, already owned, nothing to trade) is an error.
        if(!commands.blocked&&!commands.used&&(owned||purchased||!affordable||(full&&!selected))&&
           ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)&&ImGui::IsMouseClicked(0))
            playSound(audio::Sfx::UiError);
        ImGui::PopFont();ImGui::EndChild();ImGui::PopStyleColor();ImGui::PopID();
    }
    const float inventoryY=offerY+offerHeight+29,cellWidth=(width-76)/4,cellHeight=94;
    ImGui::SetCursorPos({20,inventoryY-24});ImGui::PushFont(ui.small);
    ImGui::TextColored(gold,"YOUR RELICS   /   Trade includes half-price sale credit");
    if(full&&session.shopReplacement>=0) {
        ImGui::SameLine(0,20);
        shopCoinText("CREDIT +"+std::to_string(relicInfo(state.owned[static_cast<std::size_t>(session.shopReplacement)]).cost/2),ui);
    }
    ImGui::PopFont();
    for(int i=0;i<bj::maxEquipmentSlots;++i) {
        ImGui::PushID(100+i);
        const ImVec2 start{20+(i%4)*(cellWidth+12),inventoryY+(i/4)*(cellHeight+8)};
        ImGui::SetCursorPos(start);const auto origin=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();
        const bool selected=session.shopReplacement==i;
        draw->AddRectFilled(origin,{origin.x+cellWidth,origin.y+cellHeight},selected?IM_COL32(70,54,34,255):IM_COL32(34,27,32,255),6);
        draw->AddRect(origin,{origin.x+cellWidth,origin.y+cellHeight},selected?IM_COL32(219,173,88,255):IM_COL32(142,117,85,70),6);
        if(i>=static_cast<int>(state.owned.size())) {
            draw->AddText(ui.small,18,{origin.x+12,origin.y+32},ImGui::ColorConvertFloat4ToU32(muted),"EMPTY SLOT");
            ImGui::PopID();continue;
        }
        const auto& item=relicInfo(state.owned[static_cast<std::size_t>(i)]);
        const float textWidth=ui.small->CalcTextSizeA(18,1000,0,item.name).x;
        draw->AddText(ui.small,std::min(18.0f,18*(cellWidth-64)/textWidth),{origin.x+57,origin.y+10},ImGui::ColorConvertFloat4ToU32(cream),item.name);
        draw->AddText(ui.small,14,{origin.x+57,origin.y+32},ImGui::ColorConvertFloat4ToU32(rarityColor(item.rarity)),bj::rarityName(item.rarity));
        ImGui::SetCursorPos({start.x+6,start.y+4});
        const bool selectReplacement=ImGui::InvisibleButton("select",{cellWidth-12,49});
        buttonSounds(selectReplacement,false);
        if(selectReplacement) session.shopReplacement=selected?-1:i;
        const float hover=shopRelicHover(ImGui::IsItemHovered()||selected,animate);
        drawShopRelic(draw,{origin.x+29,origin.y+29},20,item,art,animate,hover);
        if(ImGui::IsItemHovered()) relicTooltip(item,ui);
        ImGui::SetCursorPos({start.x+7,start.y+59});ImGui::PushFont(ui.small);
        ImGui::BeginDisabled(commands.blocked||commands.used);
        if(theatreButton(selected?"SELECTED":"TRADE",{(cellWidth-22)*.5f,27},{.24f,.22f,.20f,1})) session.shopReplacement=selected?-1:i;
        ImGui::SameLine();
        if(shopCoinButton("SELL /",item.cost/2,{(cellWidth-22)*.5f,27},{.29f,.18f,.17f,1},ui,"sell")) {
            commands.apply([&]{return game.sell(item.id);});
            if(commands.used) {
                playSound(audio::Sfx::ChipsWin); // Half the price comes back as coins.
                session.shopReplacement=-1;session.selectedEquipment=-1;
            }
        }
        ImGui::EndDisabled();ImGui::PopFont();ImGui::PopID();
    }
    ImGui::SetCursorPos({20,height-64});ImGui::PushFont(ui.button);
    ImGui::BeginDisabled(commands.blocked||commands.used);
    if(theatreButton("NEXT ROUND / SPACE",{350,46},{.16f,.29f,.22f,1})) commands.apply([&]{return game.leaveShop();});
    ImGui::EndDisabled();ImGui::PopFont();ImGui::End();
}

void drawGameOver(bj::Game& game, Commands& commands, Session& session, const Interface& ui, GLFWwindow* window) {
    const auto state = game.state();
    const auto display = ImGui::GetIO().DisplaySize;
    const bool compact=display.y<800;
    const float fade=std::clamp(session.gameOverElapsed/.8f,0.0f,1.0f);
    const float darkness=fade*fade*(3.0f-2.0f*fade);
    const float appear=std::clamp((session.gameOverElapsed-.12f)/.68f,0.0f,1.0f);
    const float alpha=appear*appear*(3.0f-2.0f*appear);
    ImGui::GetBackgroundDrawList()->AddRectFilled({0,0},display,
        IM_COL32(13,8,20,static_cast<int>(255*darkness)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    if(!session.gameOverFocused) {
        ImGui::SetNextWindowFocus();
        session.gameOverFocused=true;
    }
    beginPanel("##end",{0,0},display,true,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    auto* draw=ImGui::GetWindowDrawList();
    const float center=display.x*.5f;
    const auto white=IM_COL32(249,232,198,static_cast<int>(255*alpha));
    const auto pale=IM_COL32(195,176,151,static_cast<int>(255*alpha));
    const auto scarlet=IM_COL32(201,155,76,static_cast<int>(255*alpha));
    const auto text=[&](ImFont* font,float size,float x,float y,ImU32 color,const std::string& value) {
        const float width=font->CalcTextSizeA(size,10000,0,value.c_str()).x;
        draw->AddText(font,size,{x-width*.5f,y},color,value.c_str());
    };
    const float titleY=compact?74.0f:100.0f;
    const float desiredTitleSize=compact?148.0f:184.0f;
    const float titleSize=desiredTitleSize*std::min(1.0f,720.0f/ui.title->CalcTextSizeA(desiredTitleSize,10000,0,"GAME OVER").x);
    text(ui.title,titleSize,center,titleY,white,"GAME OVER");
    draw->AddLine({center-100,titleY+titleSize+12},{center+100,titleY+titleSize+12},scarlet,1);
    const float dy=titleY+titleSize+12;
    draw->AddQuadFilled({center,dy-6},{center+6,dy},{center,dy+6},{center-6,dy},scarlet);
    palace::panelFrame(draw,{center-390,compact?28.0f:44.0f},{780,display.y-(compact?56.0f:88.0f)},true);
    const float statsY=titleY+titleSize+(compact?38.0f:50.0f);
    text(ui.small,18,center-160,statsY,pale,"ROUND REACHED");
    text(ui.small,18,center+160,statsY,pale,"TOTAL RUN SCORE");
    text(ui.number,compact?54.0f:64.0f,center-160,statsY+23,white,std::to_string(state.round));
    text(ui.number,compact?54.0f:64.0f,center+160,statsY+23,white,std::to_string(state.totalScore));
    if(state.revivesLeft>0)
        text(ui.small,16,center,statsY+94,pale,"ENCORE: ONE LIFE REMAINS");
    const float buttonWidth=340,buttonHeight=compact?54.0f:60.0f;
    const float buttonY=statsY+126,step=compact?68.0f:76.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,2);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1);
    ImGui::PushStyleColor(ImGuiCol_Text,{1,1,1,1});
    ImGui::PushStyleColor(ImGuiCol_Border,{.65f,.51f,.31f,1});
    ImGui::PushStyleColor(ImGuiCol_NavCursor,{1,1,1,1});
    ImGui::PushStyleColor(ImGuiCol_Button,{0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{.54f,.025f,.055f,1});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,{.72f,.025f,.055f,1});
    ImGui::PushFont(ui.button);
    ImGui::BeginDisabled(commands.blocked || commands.used || session.settings || session.rules || session.transitioning());
    const auto button=[&](const char* label,float y) {
        ImGui::SetCursorPos({center-buttonWidth*.5f,y});
        return theatreButton(label,{buttonWidth,buttonHeight},{.23f,.14f,.25f,1});
    };
    const bool retry=button(state.revivesLeft>0?"REVIVE / SPACE":"RETRY / SPACE",buttonY);
    ImGui::SetItemDefaultFocus();
    if(retry) {
        if(state.revivesLeft>0) commands.apply([&]() {return game.revive();});
        else session.start(game);
    }
    if(button("MAIN MENU",buttonY+step)) session.returnToMenu();
    if(button("QUIT GAME",buttonY+step*2)) glfwSetWindowShouldClose(window,GLFW_TRUE);
    ImGui::EndDisabled();
    ImGui::PopFont();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(3);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void centeredText(ImDrawList* draw, ImFont* font, float size, float centerX, float y, ImU32 color,
                  const std::string& text) {
    const float width = font->CalcTextSizeA(size, 10000, 0, text.c_str()).x;
    draw->AddText(font, size, {centerX - width * .5f, y}, color, text.c_str());
}

void drawHandValue(const bj::Snapshot& state, const HandFeedback& feedback, const TableLayout& layout,
                   const Interface& ui) {
    const ImVec2 origin{layout.contentX + layout.contentWidth - 210, layout.actionY - 38};
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    beginPanel("##hand-value", origin, {210, 184}, true, ImGuiWindowFlags_NoInputs);
    auto* draw = ImGui::GetWindowDrawList();
    const float centerX = origin.x + 105;
    const auto hand = bj::evaluateHand(state.player);
    if (feedback.remaining > 0) {
        const float alpha = std::min(1.0f, feedback.remaining / .25f);
        const std::string change = (feedback.delta >= 0 ? "+" : "") + std::to_string(feedback.delta);
        const float textWidth = ui.menuButton->CalcTextSizeA(34, 1000, 0, change.c_str()).x;
        const float left = centerX - (textWidth + 40) * .5f;
        const auto ink = ImGui::ColorConvertFloat4ToU32({cream.x, cream.y, cream.z, alpha});
        const bool redSuit = feedback.card.suit == bj::Suit::Hearts || feedback.card.suit == bj::Suit::Diamonds;
        const ImVec4 suit = redSuit ? red : cream;
        draw->AddText(ui.menuButton, 34, {left, origin.y}, ink, change.c_str());
        presentation::drawCardSuit(draw, feedback.card.suit, {left + textWidth + 24, origin.y + 18}, 32,
            ImGui::ColorConvertFloat4ToU32({suit.x, suit.y, suit.z, alpha}));
    }
    const char* label = hand.bust ? "YOUR HAND / BUST" : hand.soft ? "YOUR HAND / SOFT" :
        state.phase == bj::Phase::Ready && !state.player.empty() ? "LAST HAND" : "YOUR HAND";
    centeredText(draw, ui.small, 18, centerX, origin.y + 60, ImGui::ColorConvertFloat4ToU32(muted), label);
    centeredText(draw, ui.largeNumber, 104, centerX, origin.y + 78,
        ImGui::ColorConvertFloat4ToU32(hand.bust ? red : cream),
        state.player.empty() ? "--" : std::to_string(hand.total));
    ImGui::End();
    ImGui::PopStyleVar();
}

bj::Snapshot settledState(const presentation::Presentation& timeline) {
    auto state=timeline.visible();
    if(timeline.playerArrival()<1 && !state.player.empty()) state.player.pop_back();
    if(timeline.dealerArrival()<1 && !state.dealer.empty()) state.dealer.pop_back();
    return state;
}

void drawTable(bj::Game& game, Commands& commands, Session& session, HandFeedback& feedback,
               const Interface& ui, const ArtAssets& art, GLFWwindow* window, const presentation::Presentation& timeline, bool animate) {
    const auto state = settledState(timeline);
    const auto display = ImGui::GetIO().DisplaySize;
    const auto layout = tableLayout(display);
    if (state.phase == bj::Phase::RunLost && !timeline.busy()) {
        drawGameOver(game, commands, session, ui, window);
        return;
    }
    if (state.phase == bj::Phase::Shop) {
        drawShop(game, commands, session, ui, art, animate);
        return;
    }
    drawSidebar(state, layout, session, commands, ui, display.y);
    drawRelicStrip(state, timeline, layout, session, ui, game, commands, art);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    beginPanel("##actions", {controlsLeft(layout), layout.actionY + 42}, {460, 104}, true);
    ImGui::PushFont(ui.menuButton);
    ImGui::BeginDisabled(commands.blocked || commands.used);
    if (state.phase == bj::Phase::PlayerTurn) {
        ImGui::BeginDisabled(state.pendingRescue||!state.cardChoices.empty());
        if (theatreButton("HIT", {190, 78},{.11f,.31f,.24f,1})) commands.apply([&]() { return game.hit(); });
        ImGui::EndDisabled();
        ImGui::SameLine(206);
        ImGui::BeginDisabled(!state.cardChoices.empty());
        if (theatreButton(state.pendingRescue?"ACCEPT BUST":"STAND", {250, 78},{.33f,.22f,.13f,1})) commands.apply([&]() { return game.stand(); });
        ImGui::EndDisabled();
    } else if (state.phase == bj::Phase::Ready) {
        ImGui::SetCursorPosX(68);
        if (theatreButton("DEAL HAND", {320, 78},{.26f,.18f,.31f,1})) commands.apply([&]() { return game.deal(); });
    } else if (state.phase == bj::Phase::RoundOver) {
        ImGui::SetCursorPosX(68);
        if (theatreButton(state.roundScore >= state.goal ? "SHOP" : "CONTINUE", {320, 78},{.26f,.18f,.31f,1}))
            commands.apply([&]() { return game.advance(); });
    }
    ImGui::EndDisabled();
    ImGui::PopFont();
    ImGui::PushFont(ui.small);
    ImGui::SetCursorPos({state.phase == bj::Phase::PlayerTurn ? 0.0f : 68.0f, 84});
    ImGui::TextColored(muted, "%s", state.phase == bj::Phase::PlayerTurn ? "H" : "SPACE");
    if (state.phase == bj::Phase::PlayerTurn) {
        ImGui::SameLine(206);
        ImGui::TextColored(muted, "SPACE / S");
    }
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar();

    const auto& current = state;
    feedback.observe(current, 0, !session.menu);
    if (current.phase == bj::Phase::RunLost && !timeline.busy()) {
        drawGameOver(game, commands, session, ui, window);
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    beginPanel("##dealer", {layout.contentX + 18, layout.boardY + 8}, {250, 102}, true, ImGuiWindowFlags_NoInputs);
    ImGui::PushFont(ui.small);
    ImGui::TextColored(muted, "DEALER");
    ImGui::PopFont();
    const auto dealer = bj::evaluateHand(current.dealer);
    const std::string dealerText = current.dealer.empty() ? "--" : !timeline.dealerRevealed()
        ? std::to_string(bj::evaluateHand({current.dealer.front()}).total) + " + ?" : std::to_string(dealer.total);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 dealerOrigin = ImGui::GetWindowPos();
    draw->AddText(ui.largeNumber, 64, {dealerOrigin.x, dealerOrigin.y + 21},
        ImGui::ColorConvertFloat4ToU32(current.phase != bj::Phase::PlayerTurn && dealer.bust ? red : cream), dealerText.c_str());
    ImGui::End();

    beginPanel("##hand-reward", {layout.contentX + 18, layout.actionY - 76}, {layout.contentWidth - 250, 112}, true,
        ImGuiWindowFlags_NoInputs);
    if (current.phase == bj::Phase::RoundOver || timeline.rewardStep() >= 0) {
        const auto hand = bj::evaluateHand(current.player);
        const ImVec4 color = current.outcome == bj::Outcome::Win ? green : current.outcome == bj::Outcome::Loss ? red : gold;
        ImGui::PushFont(ui.small);
        ImGui::TextColored(color, "%s / +%d COINS", current.outcome == bj::Outcome::Win
            ? (hand.blackjack ? "BLACKJACK" : "YOU WIN") : current.outcome == bj::Outcome::Loss
            ? (hand.bust ? "BUST" : "HOUSE WINS") : "PUSH", current.score.coins);
        ImGui::PopFont();
        ImGui::SetCursorPosY(23);
        ImGui::PushFont(ui.number);
        const int step=timeline.rewardStep();
        const int count=static_cast<int>(current.score.appliedEffects.size());
        if(step==0) ImGui::TextColored(color,"%d BASE POINTS",current.score.base);
        else if(step>0 && step<=count) {
            const auto& effect=current.score.appliedEffects[static_cast<std::size_t>(step-1)];
            const auto& item=relicInfo(effect.relic);
            ImGui::PopFont(); ImGui::PushFont(ui.button);
            ImGui::TextColored(rarityColor(item.rarity),"%s",item.name);
        } else ImGui::TextColored(color, "+%d POINTS", current.score.total);
        ImGui::PopFont();
    } else if (current.phase == bj::Phase::Ready) {
        ImGui::SetCursorPosY(26);
        ImGui::PushFont(ui.button);
        ImGui::TextColored(muted, "READY TO DEAL");
        ImGui::PopFont();
    }
    if (!current.score.appliedEffects.empty() && timeline.rewardStep()>0) {
        ImGui::PushFont(ui.small);
        ImGui::SetCursorPosY(72);
        const auto n=std::min(timeline.rewardVisibleEffects(),current.score.appliedEffects.size());
        std::string text;
        for(std::size_t i=0;i<n;++i) {
            const auto& effect=current.score.appliedEffects[i];
            if(!text.empty()) text+=" / ";
            text+=relicInfo(effect.relic).name;
            std::ostringstream bonus;
            if(effect.points) bonus<<" +"<<effect.points;
            if(effect.multiplierBonus>0) bonus<<" +"<<std::setprecision(2)<<effect.multiplierBonus<<" mult";
            if(effect.coins) bonus<<" +"<<effect.coins<<" coin";
            text+=bonus.str();
        }
        ImGui::TextWrapped("%s",text.c_str());
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    drawHandValue(current, feedback, layout, ui);
}

void drawRules(Session& session, const Interface& ui, const gfx::RenderSettings& settings, bool paused) {
    if (!session.rules) return;
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x * .5f - 370, display.y * .5f - 305});
    ImGui::SetNextWindowSize({740, 610});
    ImGui::SetNextWindowFocus();
    const bool visible=ImGui::Begin("HOW TO PLAY", &session.rules, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    if(!session.rules) playSound(audio::Sfx::UiClose);
    if(visible) {
        palace::panelFrame(ImGui::GetWindowDrawList(),ImGui::GetWindowPos(),ImGui::GetWindowSize(),true);
        heading("JESTER JACK", ui);
        ImGui::PushFont(ui.small);
        ImGui::TextColored(gold,"IT-2502 | Yerkingali Saruar | Baibatyr Miras | Yesmagzam Sultan");
        ImGui::PopFont();
        if (ImGui::BeginTabBar("help-tabs")) {
        if (ImGui::BeginTabItem("CONTROLS", nullptr, session.controlsTab ? ImGuiTabItemFlags_SetSelected : 0)) {
            ImGui::PushFont(ui.small);
            ImGui::Spacing();
            ImGui::TextColored(gold,"AT THE TABLE");
            ImGui::TextUnformatted("H / S: Hit / Stand    Space: deal, continue or speed up");
            ImGui::TextUnformatted("1-8: inspect relic    A: activate    Q / E: choose a card");
            ImGui::TextUnformatted("Right-drag: orbit    Wheel: zoom    Double right-click: reset");
            ImGui::TextUnformatted("F1: help    F2: options    Escape: back");
            ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
            ImGui::TextColored(gold,"SCENE AND LIGHT");
            ImGui::Text("P: pause animation and play   [%s]", paused ? "PAUSED" : "RUNNING");
            ImGui::Text("F3: Blinn-Phong lighting   [%s]",settings.lighting?"ON":"OFF");
            ImGui::Text("F4: textures   [%s]",settings.textures?"ON":"OFF");
            ImGui::TextUnformatted("J / L: light left / right    I / K: light away / closer");
            ImGui::TextUnformatted("U / O: light up / down    0: reset light position");
            ImGui::Text("Light offset: %.1f, %.1f, %.1f",settings.lightX,settings.lightY,settings.lightZ);
            ImGui::PopFont();
            ImGui::EndTabItem();
        }
        session.controlsTab=false;
        if (ImGui::BeginTabItem("HOW TO PLAY")) {
        ImGui::PushFont(ui.small);
        ImGui::TextWrapped("Each round gives you five hands to reach the target. The Last Laugh deck adds one more.");
        ImGui::BulletText("Beat the dealer without going over 21.");
        ImGui::BulletText("Aces count as 1 or 11. Dealer stands on 17.");
        ImGui::TextWrapped("A win earns the hand total in points and 4 coins. Natural blackjack is 21 points; only relics add bonus points.");
        ImGui::TextWrapped("A loss or push earns no points and uses a hand. Reach the target to shop. Run out of hands below the target and the run ends.");
        ImGui::TextWrapped("Equip up to 8 relics. Click a physical chip to inspect it. Keys 1-8 inspect; A or ACTIVATE uses the selected ability; charges are limited per hand or round. A pending bust can be rescued before settlement. Shop sales return half the price.");
        ImGui::PopFont();
        ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        }
        ImGui::SetCursorPosY(546);
        if (backButton("GOT IT", {160, 44})) session.rules = false;
    }
    ImGui::End();
}
void resetCamera(gfx::RenderSettings& settings) {
    const gfx::RenderSettings defaults;
    settings.yaw=defaults.yaw;
    settings.pitch=defaults.pitch;
    settings.distance=defaults.distance;
}

bool drawSettings(Session& session, gfx::RenderSettings& settings, const Interface& ui,
                  audio::Preferences& volumes, bool saveFailed) {
    if(!session.settings) return false;
    bool changed=false;
    const auto display=ImGui::GetIO().DisplaySize;
    const float height=std::min(620.0f,display.y-48);
    ImGui::SetNextWindowPos({(display.x-470)*.5f,(display.y-height)*.5f});
    ImGui::SetNextWindowSize({470,height});ImGui::SetNextWindowFocus();
    const bool visible=ImGui::Begin("SETTINGS",&session.settings,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar);
    if(!session.settings) playSound(audio::Sfx::UiClose);
    if(visible) {
        palace::panelFrame(ImGui::GetWindowDrawList(),ImGui::GetWindowPos(),ImGui::GetWindowSize(),true);
        heading("THE PROJECTION ROOM",ui);
        ImGui::BeginChild("settings-body",{0,ImGui::GetContentRegionAvail().y-58},false);
        ImGui::PushFont(ui.small);
        ImGui::TextColored(gold,"SOUND");
        const char* labels[]={"Music","Effects"};
        float* gains[]={&volumes.music,&volumes.effects};
        for(int i=0;i<2;++i) {
            float percent=*gains[i]*100.0f;
            if(ImGui::SliderFloat(labels[i],&percent,0.0f,100.0f,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)) {
                *gains[i]=percent*.01f;
                changed=true;
            }
            buttonSounds(ImGui::IsItemActivated(),false);
            session.volumeMin[i]=ImGui::GetItemRectMin();
            session.volumeMax[i]={session.volumeMin[i].x+ImGui::CalcItemWidth(),ImGui::GetItemRectMax().y};
        }
        ImGui::TextColored(muted,"Effects: cards, chips, buttons and the car.");
        if(saveFailed) ImGui::TextWrapped("Volume works for this session, but could not be saved.");
        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
        ImGui::TextColored(gold,"DISPLAY");
        buttonSounds(ImGui::Checkbox("Lighting",&settings.lighting),false);
        ImGui::SameLine(225);
        buttonSounds(ImGui::Checkbox("Textures",&settings.textures),false);
        buttonSounds(ImGui::Checkbox("Ambient motion",&settings.animate),false);
        buttonSounds(ImGui::Checkbox("Cinematic entrance",&session.transitions),false);
        ImGui::TextColored(muted,"Off: a short fade. Escape skips the entrance.");
        ImGui::Spacing();ImGui::TextColored(gold,"HOUSE LIGHTS");
        ImGui::SliderFloat("Light",&settings.lightIntensity,gfx::minLight,gfx::maxLight);
        buttonSounds(ImGui::IsItemActivated(),false);
        session.lightMin=ImGui::GetItemRectMin();session.lightMax={session.lightMin.x+ImGui::CalcItemWidth(),ImGui::GetItemRectMax().y};
        ImGui::Spacing();ImGui::TextColored(gold,"DEALING SPEED");
        ImGui::SliderFloat("##speed",&session.animationSpeed,0.0f,1.0f,"");
        buttonSounds(ImGui::IsItemActivated(),false);
        ImGui::TextColored(muted,"Natural                                    Fast");
        ImGui::TextWrapped("Click or press Space to speed up a deal or reward.");
        ImGui::Spacing();ImGui::TextColored(gold,"AT THE TABLE");
        ImGui::TextWrapped("Right-drag to orbit. Scroll to zoom. Double right-click to reset. Keys 1-8 inspect a relic; A activates it.");
        ImGui::PopFont();ImGui::EndChild();
        ImGui::SetCursorPosY(height-63);
        if(theatreButton("RESET VIEW",{200,42},{.22f,.17f,.25f,1})) resetCamera(settings);
        ImGui::SameLine();if(backButton("DONE",{200,42},{.17f,.28f,.23f,1})) session.settings=false;
    }
    ImGui::End();
    return changed;
}

gfx::Scene makeScene(const presentation::Presentation& timeline, float time, const Session& session) {
    const auto& state=timeline.visible();
    gfx::Scene scene;
    scene.menu = session.menu;
    scene.menuApproach=session.entrance.approach();
    scene.doorOpen=session.entrance.doorOpen();
    scene.menuPush=session.entrance.push();
    scene.time = time;
    scene.coins = state.coins;
    scene.deckDesign=state.startDeck==bj::StartDeck::SecondLife?1:state.startDeck==bj::StartDeck::Rich?2:0;
    const auto display = ImGui::GetIO().DisplaySize;
    const auto layout = tableLayout(display);
    scene.tableViewport = {layout.contentX / display.x, layout.boardY / display.y,
        layout.contentWidth / display.x, layout.boardHeight / display.y};
    for (const auto& card : state.player)
        scene.player.push_back({card.rank, static_cast<int>(card.suit), false});
    for (std::size_t i = 0; i < state.dealer.size(); ++i) {
        const auto& card = state.dealer[i];
        scene.dealer.push_back({card.rank, static_cast<int>(card.suit),
            i == 1 && !timeline.dealerRevealed() && !timeline.dealerFlipping()});
        if(i==1 && timeline.dealerFlipping()) scene.dealer.back().flip=timeline.dealerFlip();
    }
    if(!scene.player.empty()) scene.player.back().arrival=timeline.playerArrival();
    if(!scene.dealer.empty()) scene.dealer.back().arrival=timeline.dealerArrival();
    scene.dealerReach=timeline.dealerReach();
    scene.dealingToPlayer=timeline.dealingToPlayer();
    scene.celebration=timeline.celebration();
    scene.legendary=timeline.legendPulse();
    scene.bustReaction=session.bustReaction;
    for(std::size_t i=0;i<state.owned.size();++i) {
        const auto id=state.owned[i];const auto status=bj::abilityStatus(state,id);
        const bool reacting=session.relicReactionStep>0 && static_cast<std::size_t>(session.relicReactionStep)<=state.score.appliedEffects.size() &&
            state.score.appliedEffects[session.relicReactionStep-1].relic==id;
        scene.equipment.push_back({static_cast<int>(id),static_cast<int>(relicInfo(id).rarity),bj::isActiveRelic(id),status.usable,
            status.remaining,std::max(session.relicHover[i],reacting?.85f:session.selectedEquipment==static_cast<int>(i)?.5f:0.0f)});
    }
    return scene;
}

void drawPhysicalEquipment(const gfx::Scene& scene, const gfx::RenderSettings& settings,
                           int width,int height,bj::Game& game,Commands& commands,Session& session,const Interface& ui) {
    session.physicalChipHovered=false;session.hoveredRelic=-1;
    const auto& io=ImGui::GetIO();
    if(std::abs(io.MouseDelta.x)+std::abs(io.MouseDelta.y)>1.0f||ImGui::IsMouseClicked(0)) session.suppressEquipmentHover=false;
    if(session.menu||session.settings||session.rules||session.transitioning()||scene.equipment.empty()||
       game.state().phase==bj::Phase::Shop||game.state().phase==bj::Phase::RunLost||session.suppressEquipmentHover) return;
    const auto display=io.DisplaySize;const auto layout=tableLayout(display);
    const auto positions=gfx::Renderer::projectEquipment(scene,settings,width,height);
    for(std::size_t i=0;i<positions.size();++i) {
        const auto& p=positions[i];if(!p.visible) continue;
        const float rx=std::max(14.0f,p.radiusX*display.x),ry=std::max(12.0f,p.radiusY*display.y);
        const ImVec2 center{p.x*display.x,p.y*display.y};
        const float dx=(io.MousePos.x-center.x)/rx,dy=(io.MousePos.y-center.y)/ry;
        if(dx*dx+dy*dy>1||io.MousePos.y<layout.boardY||io.MousePos.y>layout.actionY) continue;
        session.physicalChipHovered=true;session.hoveredRelic=static_cast<int>(i);
        session.relicHover[i]=approach(session.relicHover[i],true);
        session.inspectedRelic=static_cast<bj::Relic>(scene.equipment[i].id);
        if(ImGui::IsMouseClicked(0)) {session.selectedEquipment=static_cast<int>(i);playSound(audio::Sfx::UiOpen);}
        break;
    }
    if(ImGui::IsMouseClicked(0)&&!session.physicalChipHovered&&io.MousePos.x>layout.contentX&&io.MousePos.y>layout.boardY&&
        io.MousePos.y<layout.actionY) session.clearEquipmentContext();
    (void)commands;(void)ui;
}

void drawCardChoices(bj::Game& game,Commands& commands,const Interface& ui,const Session& session) {
    const auto state=game.state();
    if(session.menu||session.settings||session.rules||session.transitioning()||state.cardChoices.empty()) return;
    const auto display=ImGui::GetIO().DisplaySize;const auto layout=tableLayout(display);
    const auto strip=equipmentLayout(layout.contentWidth);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12,6});
    beginPanel("##card-choice",{layout.contentX+4,strip.detailTop},{layout.contentWidth-9,strip.detailHeight});
    ImGui::PushFont(ui.button);ImGui::TextColored(gold,"FORKED PATH / CHOOSE ONE CARD");
    const char* suits[]={"Clubs","Diamonds","Hearts","Spades"};
    for(std::size_t i=0;i<state.cardChoices.size();++i) {
        if(i) ImGui::SameLine();const auto card=state.cardChoices[i];
        const std::string rank=card.rank==1?"Ace":card.rank==11?"Jack":card.rank==12?"Queen":card.rank==13?"King":std::to_string(card.rank);
        const std::string label=rank+" of "+suits[static_cast<int>(card.suit)]+(i==0?" / Q":" / E");
        ImGui::BeginDisabled(commands.blocked||commands.used);
        if(theatreButton(label.c_str(),{(layout.contentWidth-42)*.5f,42},{.19f,.26f,.24f,1})) commands.apply([&]{return game.chooseCard(i);},true);
        ImGui::EndDisabled();
    }
    ImGui::PopFont();ImGui::End();ImGui::PopStyleVar();
}

void drawCollection(Session& session, const Interface& ui, const ArtAssets& art) {
    if(!session.collection) return;
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled({0,0},display,IM_COL32(12,7,24,200));
    const ImVec2 size{std::min(1240.0f,display.x-48),display.y-48};
    beginPanel("##collection",{(display.x-size.x)*.5f,24},size);
    ImGui::BeginDisabled(session.settings||session.rules||session.transitioning());
    heading("The Relic Collection",ui);
    ImGui::SetCursorPos({20,78});
    std::string filters[4];
    int counts[4]{static_cast<int>(bj::relicCatalog().size()),0,0,0};
    for(const auto& item:bj::relicCatalog()) ++counts[static_cast<int>(item.rarity)+1];
    const char* labels[]={"ALL","COMMON","RARE","LEGENDARY"};
    for(int i=0;i<4;++i) filters[i]=std::string(labels[i])+" / "+std::to_string(counts[i]);
    for(int i=0;i<4;++i) {
        if(i) ImGui::SameLine();
        ImGui::PushID(i);
        if(theatreButton(filters[i].c_str(),{160,36},session.collectionFilter==i?ImVec4(.33f,.25f,.18f,1):ImVec4(.20f,.15f,.21f,1)))
            session.collectionFilter=i;
        ImGui::PopID();
    }
    ImGui::SetCursorPos({20,128});
    const float detailWidth=display.x<1200?292.0f:320.0f;
    const float childHeight=size.y-208;
    ImGui::BeginChild("gallery",{size.x-detailWidth-52,childHeight},false);
    const float w=(ImGui::GetContentRegionAvail().x-36)/4;
    const float radius=std::min(57.0f,w*.34f);
    const float cellHeight=radius*2+70;
    int shown=0;
    // Display positions are independent of catalog/enum order and selection IDs.
    for(const auto id:presentation::collectionOrder()) {
        const auto& item=relicInfo(id);
        if(session.collectionFilter && static_cast<int>(item.rarity)!=session.collectionFilter-1) continue;
        if(shown%4) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(item.id));
        const auto pos=ImGui::GetCursorScreenPos();
        const bool selected=session.selectedRelic==item.id;
        const auto accent=rarityColor(item.rarity);
        ImGui::PushStyleColor(ImGuiCol_Header,{0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,{0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,{0,0,0,0});
        const bool selectedRelic=ImGui::Selectable("##relic",selected,0,{w,cellHeight});
        buttonSounds(selectedRelic,false);
        if(selectedRelic) {
            session.selectedRelic=item.id;
        }
        ImGui::PopStyleColor(3);
        const bool hovered=ImGui::IsItemHovered()||ImGui::IsItemFocused();
        auto& lift=session.collectionHover[static_cast<int>(item.id)];
        lift=approach(lift,hovered);
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(pos,{pos.x+w,pos.y+cellHeight},selected?IM_COL32(52,39,44,255):IM_COL32(31,25,31,170),8);
        draw->AddRect(pos,{pos.x+w,pos.y+cellHeight},selected?ImGui::ColorConvertFloat4ToU32(accent):IM_COL32(130,109,83,65),8,0,selected?1.5f:1.0f);
        drawChip(draw,{pos.x+w*.5f,pos.y+radius+12-lift*4},radius,item,art,-.05f*lift);
        const float available=w-14;
        const float nameWidth=ui.button->CalcTextSizeA(22,1000,0,item.name).x;
        const float fontSize=22*std::min(1.0f,available/nameWidth);
        centeredText(draw,ui.button,fontSize,pos.x+w*.5f,pos.y+radius*2+21,ImGui::ColorConvertFloat4ToU32(cream),item.name);
        centeredText(draw,ui.small,18,pos.x+w*.5f,pos.y+radius*2+46,ImGui::ColorConvertFloat4ToU32(accent),bj::rarityName(item.rarity));
        ImGui::PopID(); ++shown;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{18,16});
    ImGui::BeginChild("detail",{detailWidth,childHeight},ImGuiChildFlags_Borders);
    const auto& item=relicInfo(session.selectedRelic);
    const auto pos=ImGui::GetCursorScreenPos();
    const float detailRadius=display.y<800?70.0f:86.0f;
    drawChip(ImGui::GetWindowDrawList(),{pos.x+(detailWidth-36)*.5f,pos.y+detailRadius+6},detailRadius,item,art);
    ImGui::Dummy({detailWidth-36,detailRadius*2+20});
    ImGui::PushFont(ui.heading); ImGui::TextWrapped("%s",item.name); ImGui::PopFont();
    rarityBadge(item.rarity,ui);
    ImGui::Spacing(); ImGui::PushFont(ui.small); ImGui::TextWrapped("%s",item.description); ImGui::PopFont();
    ImGui::Spacing(); ImGui::TextColored(gold,"BUY %d   /   SELL %d",item.cost,item.cost/2);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SetCursorPosY(size.y-64);
    if(backButton("BACK",{180,44})) session.collection=false;
    ImGui::SameLine();
    ImGui::PushFont(ui.small);
    ImGui::TextColored(muted,"%d curiosities. Every one available from the first round.",static_cast<int>(bj::relicCatalog().size()));
    ImGui::PopFont();
    ImGui::EndDisabled();
    ImGui::End();
}

float deckPanelHeight(ImVec2 display) {return std::min(680.0f,display.y-56);}

int deckIndex(bj::StartDeck deck) {
    return deck==bj::StartDeck::SecondLife?1:deck==bj::StartDeck::Rich?2:0;
}

bj::StartDeck deckAt(int index) {
    const bj::StartDeck decks[]={bj::StartDeck::ExtraHand,bj::StartDeck::SecondLife,bj::StartDeck::Rich};
    return decks[(index+3)%3];
}

void browseDeck(Session& session,int direction) {
    playSound(audio::Sfx::UiOpen);
    session.selectedDeck=deckAt(deckIndex(session.selectedDeck)+direction);
    session.deckDirection=direction;
    session.deckMotion=1;
}

struct DeckLayout {
    ImVec2 origin;
    float width;
    float height;
    ImVec2 card;
    ImVec2 cardSize;
    ImVec2 previous;
    ImVec2 next;
};

DeckLayout deckLayout(ImVec2 display) {
    const float width=std::min(1000.0f,display.x-56),height=deckPanelHeight(display);
    const bool compact=display.y<800;
    const ImVec2 size{compact?216.0f:244.0f,compact?324.0f:366.0f};
    const ImVec2 card{124,136};
    return {{(display.x-width)*.5f,(display.y-height)*.5f},width,height,card,size,
        {38,card.y+size.y*.5f-28},{card.x+size.x+28,card.y+size.y*.5f-28}};
}

void drawDeckSelection(bj::Game& game, Session& session, const Interface& ui, const ArtAssets& art) {
    if(!session.deckSelection) return;
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled({0,0},display,IM_COL32(12,7,24,205));
    const auto layout=deckLayout(display);
    const float width=layout.width,height=layout.height;
    beginPanel("##decks",layout.origin,{width,height});
    ImGui::BeginDisabled(session.settings||session.rules||session.transitioning());
    heading("Choose Your Deck",ui);
    ImGui::PushFont(ui.small);
    ImGui::TextColored(muted,"A gift from the house. A different way to tempt fate.");
    ImGui::PopFont();
    const char* names[]={"LAST LAUGH","ENCORE","GOLDEN FOOL"};
    const char* tags[]={"ONE MORE CHANCE TO PLAY","THE SHOW GOES ON","A LITTLE HOUSE MONEY"};
    const char* descriptions[]={"An extra hand in every round.\nSix hands instead of five.","One revival for the whole run.\nReplay a failed round, keeping your relics and coins.","Begin with 10 extra coins.\nFive hands in every round."};
    const ImVec4 colors[]={blue,red,gold};
    ImGui::SetCursorPos(layout.previous);
    if(theatreButton("<",{56,56})) browseDeck(session,-1);
    ImGui::SetCursorPos(layout.next);
    if(theatreButton(">",{56,56})) browseDeck(session,1);
    if(!session.settings && !session.rules && !ImGui::GetIO().WantTextInput) {
        if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow,false)) browseDeck(session,-1);
        if(ImGui::IsKeyPressed(ImGuiKey_RightArrow,false)) browseDeck(session,1);
    }
    const int index=deckIndex(session.selectedDeck);
    auto* draw=ImGui::GetWindowDrawList();
    const float motion=session.deckMotion*session.deckMotion;
    const ImVec2 p{layout.origin.x+layout.card.x+session.deckDirection*motion*16,layout.origin.y+layout.card.y-motion*3};
    const float angle=session.deckDirection*motion*.022f;
    const ImVec2 center{p.x+layout.cardSize.x*.5f,p.y+layout.cardSize.y*.5f};
    const auto corner=[&](float x,float y) {
        return ImVec2{center.x+x*std::cos(angle)-y*std::sin(angle),center.y+x*std::sin(angle)+y*std::cos(angle)};
    };
    const float hw=layout.cardSize.x*.5f,hh=layout.cardSize.y*.5f;
    const ImVec2 corners[]={corner(-hw,-hh),corner(hw,-hh),corner(hw,hh),corner(-hw,hh)};
    ImVec2 shadow[4];
    for(int i=0;i<4;++i) shadow[i]={corners[i].x+6,corners[i].y+9};
    draw->AddConvexPolyFilled(shadow,4,IM_COL32(0,0,0,110));
    draw->AddImageQuad(static_cast<ImTextureID>(art.deck(session.selectedDeck).id()),corners[0],corners[1],corners[2],corners[3]);
    draw->AddPolyline(corners,4,ImGui::ColorConvertFloat4ToU32(gold),ImDrawFlags_Closed,1.5f);
    const std::string count=std::to_string(index+1)+"   /   3";
    centeredText(draw,ui.small,18,p.x+layout.cardSize.x*.5f,p.y+layout.cardSize.y+22,ImGui::ColorConvertFloat4ToU32(muted),count);
    const float textX=476;
    draw->AddText(ui.small,18,{layout.origin.x+textX,layout.origin.y+151},ImGui::ColorConvertFloat4ToU32(colors[index]),tags[index]);
    draw->AddText(ui.heading,50,{layout.origin.x+textX,layout.origin.y+193},ImGui::ColorConvertFloat4ToU32(cream),names[index]);
    draw->AddLine({layout.origin.x+textX,layout.origin.y+259},{layout.origin.x+width-52,layout.origin.y+259},IM_COL32(177,143,93,115));
    draw->AddText(ImGui::GetFont(),22,{layout.origin.x+textX,layout.origin.y+286},ImGui::ColorConvertFloat4ToU32(cream),descriptions[index],nullptr,width-textX-52);
    draw->AddText(ui.small,18,{layout.origin.x+textX,layout.origin.y+403},ImGui::ColorConvertFloat4ToU32(muted),
        "Your chosen back stays with you\nfor the entire run.");
    ImGui::SetCursorPos({36,height-90});
    if(backButton("BACK",{160,54})) session.deckSelection=false;
    ImGui::SetCursorPos({textX,height-90});
    ImGui::PushFont(ui.button);
    if(theatreButton("TAKE A SEAT",{width-textX-36,54},{.14f,.32f,.23f,1})) session.start(game);
    ImGui::PopFont();
    ImGui::EndDisabled();
    if(!session.settings&&!session.rules) session.deckMotion=std::max(0.0f,session.deckMotion-ImGui::GetIO().DeltaTime/.2f);
    ImGui::End();
}

void autoplayStep(bj::Game& game, Commands& commands) {
    if (game.state().phase == bj::Phase::Shop) {
        for (const auto id : game.state().offers) {
            commands.apply([&]() { return game.buy(id); });
            if (commands.used) return;
        }
        commands.apply([&]() { return game.leaveShop(); });
    } else if (game.state().phase == bj::Phase::PlayerTurn) {
        if(!game.state().cardChoices.empty()) {
            std::size_t best=0;int bestScore=-100;
            for(std::size_t i=0;i<game.state().cardChoices.size();++i) {
                auto hand=game.state().player;hand.push_back(game.state().cardChoices[i]);
                const auto v=bj::evaluateHand(hand);const int score=v.bust?-v.total:v.total;
                if(score>bestScore){bestScore=score;best=i;}
            }
            commands.apply([&]{return game.chooseCard(best);},true);return;
        }
        for(const auto id:game.state().owned) {
            const auto v=bj::evaluateHand(game.state().player);
            if(bj::abilityStatus(game.state(),id).usable && (game.state().pendingRescue ||
               (id==bj::Relic::ForkedPath&&v.total<17) || (id==bj::Relic::Redraw&&v.total>=12&&v.total<17))) {
                commands.apply([&]{return game.activate(id);},true);return;
            }
        }
        commands.apply([&]() {
            return bj::evaluateHand(game.state().player).total < 17 ? game.hit() : game.stand();
        });
    } else primaryAction(game, commands);
}

void replayCommands(bj::Game& game,const Options& options) {
    std::ifstream input(options.replay);
    if(!input) throw std::runtime_error("Cannot open replay file");
    if(options.stopOwned==0) return;
    std::string line;int count=0;
    while(std::getline(input,line)) {
        if(line.empty()||line[0]=='#') continue;
        std::istringstream command(line);std::string name;command>>name;int a=0,b=0;
        bool accepted=false;
        if(name=="deal") accepted=game.deal();
        else if(name=="hit") accepted=game.hit();
        else if(name=="stand") accepted=game.stand();
        else if(name=="advance") accepted=game.advance();
        else if(name=="revive") accepted=game.revive();
        else if(name=="leave") accepted=game.leaveShop();
        else if(name=="buy"&&(command>>a)) accepted=game.buy(static_cast<bj::Relic>(a));
        else if(name=="activate"&&(command>>a)) accepted=game.activate(static_cast<bj::Relic>(a));
        else if(name=="choose"&&(command>>a)) accepted=game.chooseCard(static_cast<std::size_t>(a));
        else if(name=="replace"&&(command>>a>>b)) accepted=game.replace(static_cast<bj::Relic>(a),static_cast<bj::Relic>(b));
        if(!accepted) throw std::runtime_error("Rejected replay command: "+line);
        ++count;
        if(options.stopOwned>=0&&static_cast<int>(game.state().owned.size())>=options.stopOwned) {
            if(game.state().phase==bj::Phase::Shop) game.leaveShop();
            if(game.state().phase==bj::Phase::Ready) game.deal();
            break;
        }
    }
    if(options.stopOwned>=0&&static_cast<int>(game.state().owned.size())!=options.stopOwned)
        throw std::runtime_error("Replay did not reach requested equipment count");
    std::cout<<"Replay: "<<count<<" accepted public commands, round="<<game.state().round
             <<" owned="<<game.state().owned.size()<<'\n';
}

void saveScreenshot(const std::filesystem::path& path, int width, int height) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    if (!stbi_write_png(path.string().c_str(), width, height, 3, pixels.data(), width * 3))
        throw std::runtime_error("Could not save screenshot: " + path.string());
    std::cout << "Screenshot: " << path.string() << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        auto assets = options.assets;
        if (assets.empty()) assets = executableDirectory(argv[0]) / "assets";
        if (!std::filesystem::exists(assets) && options.assets.empty()) assets = "assets";
        Window window(options.hidden, options.width, options.height);
        Interface interface(window.handle, assets);
        gfx::Renderer renderer(assets);
        ArtAssets art(assets);
        // Hidden runs (smoke, screenshots, captures) are silent; so is any missing audio folder.
        const auto audioSettingsPath=!options.audioSettings.empty()?options.audioSettings:
            options.hidden?std::filesystem::path{}:audio::defaultPreferencesPath();
        auto volumes=audio::Preferences::load(audioSettingsPath);
        bool volumeDirty=false,volumeSaveFailed=false;
        double volumeEditedAt=0;
        audio::Engine sound(assets / "audio", !options.hidden && !options.benchmark,volumes.music,volumes.effects);
        gfx::RenderSettings settings;
        settings.animate = !options.noAmbientMotion;
        settings.lighting = !options.lightingOff;
        if(options.benchmark) glfwSwapInterval(0);
        settings.lightIntensity=options.light;
        if(options.cameraOverride) {settings.yaw=options.cameraYaw;settings.pitch=options.cameraPitch;settings.distance=options.cameraDistance;}
        const gfx::RenderSettings defaultView;
        bj::Game game(options.seed,options.demo || !options.replay.empty() ? options.deck : bj::StartDeck::Standard);
        if(!options.replay.empty()) replayCommands(game,options);
        Session session;
        HandFeedback feedback;
        presentation::Presentation timeline;
        timeline.reset(game.state());
        bool cameraDragging = false;
        bool smokeDealt=false, smokeStood=false, smokeCollection=false, smokeDeck=false, smokeRevived=false;
        int maxOwned=0,maxRound=1;
        int smokeDeckMasks[2]{};
        bool smokeSelectedCollection[2]{};
        session.selectedDeck=options.deck;
        session.fixedSeed = options.fixedSeed || options.smoke;
        session.transitions = !options.noAnimation;
        if (options.demo && options.replay.empty()) {
            session.menu = false;
            session.started = true;
            const auto before=game.state();
            game.deal();
            timeline.begin(before,game.state(),presentation::Action::Deal);
        }
        if(!options.replay.empty()) {session.menu=false;session.started=true;}
        if(!options.previewScreen.empty()) {
            const auto& preview=options.previewScreen;
            if(preview=="settings") session.settings=true;
            else if(preview=="rules") {session.menu=false;session.rules=true;}
            else if(preview=="collection") {
                session.collection=true;
                if(options.previewRelic>=0) {
                    session.selectedRelic=static_cast<bj::Relic>(options.previewRelic);
                    session.collectionFilter=static_cast<int>(relicInfo(session.selectedRelic).rarity)+1;
                }
            }
            else if(preview=="decks") session.deckSelection=true;
            else if(preview=="end") {
                for(int guard=0;guard<1000&&game.state().phase!=bj::Phase::RunLost;++guard) {
                    if(game.state().phase==bj::Phase::Ready) game.deal();
                    else if(game.state().phase==bj::Phase::PlayerTurn) game.hit();
                    else if(game.state().phase==bj::Phase::RoundOver) game.advance();
                    else if(game.state().phase==bj::Phase::Shop) game.leaveShop();
                }
                if(game.state().phase!=bj::Phase::RunLost) throw std::runtime_error("End preview could not finish run");
                session.menu=false;session.gameOverElapsed=1;timeline.reset(game.state());
            } else if(preview=="shop"||preview=="trade"||preview=="win") {
                presentation::Presentation fast;
                for(int guard=0;guard<2000;++guard) {
                    if(game.state().phase==bj::Phase::Shop || (preview=="win"&&game.state().phase==bj::Phase::RoundOver&&game.state().roundScore>=game.state().goal)) break;
                    if(game.state().phase==bj::Phase::RunLost) throw std::runtime_error("Preview run ended before next shop");
                    fast.reset(game.state());Commands command{false,false,&game,&fast};autoplayStep(game,command);
                }
                session.menu=false;session.shopReplacement=preview=="trade"?0:-1;timeline.reset(game.state());
            } else throw std::runtime_error("Unknown preview screen");
        }
        int equipmentClicks=0,equipmentIconClicks=0,abilityKeys=0,abilityActivations=0,choiceSelections=0;
        ImGuiKey smokeKey=ImGuiKey_None;
        bool smokeMouseDown=false;
        int frame = 0;
        int fullShopResizeFrame = -1;
        int relicHoverResizeFrame = -1;
        double previousTime = glfwGetTime();
        bool scenePaused=false;
        double sceneSeconds=0.0;
        gfx::BandView heldBand;
        double benchmarkSeconds=0.0;
        int benchmarkFrames=0;
        double pausedSmokeTime=0.0,pausedSmokeBeats=0.0;
        float pausedSmokeArrival=0.0f;
        SoundScope soundScope(sound);
        int carSoundCycle=-1;
        bool carSoundActive=false;
        // Beat grids and loudness curves of the soundtracks, in the same order the audio layer reports them.
        std::vector<presentation::SoundtrackSync> soundtrackSync;
        for(auto file:audio::discoverSoundtracks(assets/"audio")) soundtrackSync.push_back(presentation::SoundtrackSync::load(file.replace_extension(".sync")));
        // The ensemble plays the soundtrack that is sounding, in time with it. Without audio (hidden runs,
        // missing files) it plays a steady demonstration groove instead of standing still.
        const auto conduct=[&](gfx::Scene& scene,float sceneTime) {
            if(scenePaused) {scene.band=heldBand;return;}
            const auto clock=sound.musicClock();
            if(clock.track>=0&&static_cast<std::size_t>(clock.track)<soundtrackSync.size()&&!soundtrackSync[static_cast<std::size_t>(clock.track)].empty()) {
                const auto& sync=soundtrackSync[static_cast<std::size_t>(clock.track)];
                const auto energy=sync.energyAt(clock.position);
                scene.band.playing=true;
                scene.band.beats=sync.beatAt(clock.position);
                scene.band.bpm=static_cast<float>(sync.bpm());
                scene.band.low=energy.low;scene.band.mid=energy.mid;scene.band.high=energy.high;scene.band.hit=energy.hit;
            } else if(!sound.active()||clock.track>=0) {
                scene.band.playing=true;
                scene.band.beats=(sound.active()?clock.position:sceneTime)*110.0/60.0;
                scene.band.bpm=110.0f;
                scene.band.low=scene.band.mid=scene.band.high=scene.band.hit=.7f;
            } else {
                scene.band.playing=false;
            }
            heldBand=scene.band;
        };
        std::set<std::string> captured;
        std::set<std::pair<int,int>> checkedRelicLayouts;
        while (!glfwWindowShouldClose(window.handle)) {
            const double frameStarted=glfwGetTime();
            glfwPollEvents();
            if (options.smoke) {
                if (frame == 0 || frame == 34) glfwSetWindowSize(window.handle, 1100, 720);
                if (frame == 8 || frame == 300) glfwSetWindowSize(window.handle, 1440, 900);
                if (fullShopResizeFrame < 0 && game.state().phase == bj::Phase::Shop && game.state().owned.size() == bj::maxEquipmentSlots)
                    fullShopResizeFrame = frame;
                if (frame == fullShopResizeFrame && fullShopResizeFrame >= 0)
                    glfwSetWindowSize(window.handle, 1100, 720);
                if (fullShopResizeFrame >= 0 && frame == fullShopResizeFrame + 20)
                    glfwSetWindowSize(window.handle, 1440, 900);
                if(relicHoverResizeFrame<0&&!session.menu&&!timeline.busy()&&!game.state().owned.empty()&&
                    game.state().phase!=bj::Phase::Shop&&game.state().phase!=bj::Phase::RunLost)
                    relicHoverResizeFrame=frame;
                if(frame==relicHoverResizeFrame&&relicHoverResizeFrame>=0) glfwSetWindowSize(window.handle,1100,720);
                if(relicHoverResizeFrame>=0&&frame==relicHoverResizeFrame+45) glfwSetWindowSize(window.handle,1440,900);
            }
            const double now = glfwGetTime();
            const float dt = (options.hidden || options.benchmark) && options.frames > 0 ? 1.0f / 60.0f :
                static_cast<float>(std::min(now - previousTime, 0.1));
            previousTime = now;
            int width = 0, height = 0;
            glfwGetFramebufferSize(window.handle, &width, &height);
            if (width == 0 || height == 0) {
                glfwWaitEventsTimeout(0.05);
                continue;
            }
            if(options.entrancePreview && frame==1) session.start(game);
            const bool switched=session.switched;
            const float animationDt=scenePaused?0.0f:dt;
            session.update(animationDt, game);
            if(!switched && session.switched) {
                timeline.reset(game.state());
                playSound(audio::Sfx::ChipsStart); // Chips are handed to the player and the dealer.
            }
            if(!session.menu && !session.settings && !session.rules) timeline.update(animationDt,session.animationSpeed);
            session.history.observe(timeline.visible(),timeline.busy());
            session.updateGameOver(animationDt,!session.menu && !timeline.busy() && timeline.visible().phase==bj::Phase::RunLost);
            session.updateRelicReaction(!session.menu&&!session.settings&&!session.rules?animationDt:0,timeline);
            session.updateBustReaction(animationDt,timeline.visible());
            if(options.lightSmoke) {
                if(frame==8) session.settings=true;
                if(frame==45) session.settings=false;
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            if(options.audioSmoke) {
                if(frame==8) session.settings=true;
                auto& input=ImGui::GetIO();
                constexpr float fractions[]={0.0f,.70f,0.0f,.35f};
                for(int step=0;step<4;++step) {
                    const int down=12+step*10,channel=step/2;
                    if(frame==down||frame==down+1) {
                        const auto a=session.volumeMin[channel],b=session.volumeMax[channel];
                        const float grab=ImGui::GetStyle().GrabMinSize*.5f+2;
                        input.AddMousePosEvent(a.x+grab+(b.x-a.x-2*grab)*fractions[step],(a.y+b.y)*.5f);
                        input.AddMouseButtonEvent(0,frame==down);
                    }
                    if(frame==down+4) {
                        const float actual=channel==0?volumes.music:volumes.effects;
                        if(std::abs(actual-fractions[step])>.015f) throw std::runtime_error("Audio slider input failed");
                        std::cout<<"Live "<<(channel==0?"music":"effects")<<" volume: "<<actual<<'\n';
                    }
                }
            }
            if(options.lightSmoke) {
                auto& input=ImGui::GetIO();
                for(int phase=0;phase<3;++phase) {
                    const int down=12+phase*10;
                    if(frame==down||frame==down+1) {
                        const float fraction=phase*.5f;
                        input.AddMousePosEvent(session.lightMin.x+3+(session.lightMax.x-session.lightMin.x-6)*fraction,
                            (session.lightMin.y+session.lightMax.y)*.5f);
                        input.AddMouseButtonEvent(0,frame==down);
                    }
                    if(frame==down+4) {
                        const float expected=gfx::minLight+(gfx::maxLight-gfx::minLight)*phase*.5f;
                        if(std::abs(settings.lightIntensity-expected)>.035f)
                            throw std::runtime_error("Light slider input failed");
                        std::cout<<"Live light slider: "<<settings.lightIntensity<<'\n';
                    }
                }
            }
            if(options.smoke) {
                auto& io=ImGui::GetIO();
                // Smoke clicks are frame-scheduled; allow slow software-rendered CI frames between clicks.
                io.MouseDoubleClickTime=2.0f;
                const auto menu=menuLayout(io.DisplaySize);
                const auto click=[&](int down,ImVec2 point,int button=0) {
                    if(frame==down||frame==down+1) {
                        io.AddMousePosEvent(point.x,point.y);
                        io.AddMouseButtonEvent(button,frame==down);
                    }
                };
                if(!options.demo) {
                    if(frame==1||frame==6||frame==20) io.AddKeyEvent(ImGuiKey_F2,true);
                    if(frame==2||frame==7||frame==21) io.AddKeyEvent(ImGuiKey_F2,false);
                    click(10,{menu.origin.x+menu.width*.5f,menu.origin.y+2*menu.step+menu.height*.5f});
                    click(50,{menu.origin.x+menu.width*.5f,menu.origin.y+2*menu.step+menu.height*.5f});
                    const float collectionX=(io.DisplaySize.x-std::min(1240.0f,io.DisplaySize.x-48))*.5f;
                    click(12,{collectionX+20+3*172+80,120});
                    click(14,{collectionX+100,220});
                    click(52,{collectionX+20+3*172+80,120});
                    click(54,{collectionX+100,220});
                    if(frame==16||frame==24||frame==48||frame==56) io.AddKeyEvent(ImGuiKey_Escape,true);
                    if(frame==17||frame==25||frame==49||frame==57) io.AddKeyEvent(ImGuiKey_Escape,false);
                    click(18,{menu.origin.x+menu.width*.5f,menu.origin.y+menu.height*.5f});
                    click(58,{menu.origin.x+menu.width*.5f,menu.origin.y+menu.height*.5f});
                    const auto deck=deckLayout(io.DisplaySize);
                    const ImVec2 next{deck.origin.x+deck.next.x+28,deck.origin.y+deck.next.y+28};
                    // An outside-panel arrow click must stay blocked while settings are open.
                    click(22,{deck.origin.x+deck.previous.x+28,deck.origin.y+deck.previous.y+28});
                    for(const int step:{26,30,32,36,40,44}) click(step,next);
                    click(62,{deck.origin.x+476+(deck.width-512)*.5f,deck.origin.y+deck.height-63});
                }
                if(!options.demo && frame==65) session.entrance.skip();
                if(frame==124 || frame==126 || frame==128 || frame==130) {
                    const auto table=tableLayout(io.DisplaySize);
                    io.AddMousePosEvent(table.contentX+table.contentWidth*.65f+(frame>=126?40:0),table.boardY+120);
                    if(frame==124) io.AddMouseButtonEvent(1,true);
                    if(frame==128) io.AddMouseButtonEvent(1,false);
                    if(frame==130) io.AddMouseWheelEvent(0,1);
                }
                const auto table=tableLayout(io.DisplaySize);
                const ImVec2 freePoint{table.contentX+table.contentWidth*.65f+40,table.boardY+120};
                click(134,freePoint,1);click(136,freePoint,1);
                if(frame==142||frame==144||frame==146) {
                    io.AddMousePosEvent(freePoint.x+(frame>=144?40:0),freePoint.y);
                    if(frame==142) io.AddMouseButtonEvent(0,true);
                    if(frame==146) io.AddMouseButtonEvent(0,false);
                }
                if(frame>340&&!session.menu&&!game.state().owned.empty()&&game.state().phase!=bj::Phase::Shop&&game.state().phase!=bj::Phase::RunLost) {
                    io.AddMousePosEvent(table.contentX+table.contentWidth/16,equipmentLayout(table.contentWidth).centerY);
                }
                if(frame>=160 && frame%20==0 && !session.menu && !timeline.busy() &&
                    game.state().phase!=bj::Phase::Shop && game.state().phase!=bj::Phase::RunLost)
                    io.AddKeyEvent(ImGuiKey_Space,true);
                if(frame>=161 && frame%20==1) io.AddKeyEvent(ImGuiKey_Space,false);
            }
            const auto abilityCountBefore=game.state().usedHandAbilities.size()+game.state().usedRoundAbilities.size();
            const bool choiceBefore=!game.state().cardChoices.empty();
            if(options.abilitySmoke && !session.menu) {
                auto& io=ImGui::GetIO();
                if(smokeKey!=ImGuiKey_None) {io.AddKeyEvent(smokeKey,false);smokeKey=ImGuiKey_None;}
                if(smokeMouseDown) {io.AddMouseButtonEvent(0,false);smokeMouseDown=false;}
                if(!timeline.busy()&&frame>5&&frame%30==0) {
                    if(!game.state().cardChoices.empty()) smokeKey=ImGuiKey_Q;
                    else if(abilityActivations==0) {
                        for(std::size_t i=0;i<game.state().owned.size();++i)
                            if((options.abilityTarget<0||static_cast<int>(game.state().owned[i])==options.abilityTarget)&&bj::abilityStatus(game.state(),game.state().owned[i]).usable) {smokeKey=session.selectedEquipment==static_cast<int>(i)?ImGuiKey_A:static_cast<ImGuiKey>(ImGuiKey_1+static_cast<int>(i));break;}
                        if(smokeKey==ImGuiKey_None&&game.state().phase==bj::Phase::PlayerTurn) smokeKey=ImGuiKey_H;
                    }
                    if(options.abilityMouse&&smokeKey==ImGuiKey_A) {
                        const auto layout=tableLayout(io.DisplaySize);
                        io.AddMousePosEvent(layout.contentX+layout.contentWidth-103,equipmentLayout(layout.contentWidth).detailTop+25);
                        io.AddMouseButtonEvent(0,true);smokeMouseDown=true;smokeKey=ImGuiKey_None;
                    } else if(smokeKey!=ImGuiKey_None) io.AddKeyEvent(smokeKey,true);
                }
            }
            if(options.equipmentSmoke && !session.menu) {
                auto& io=ImGui::GetIO();
                if(frame==165||frame==166) {
                    const auto layout=tableLayout(io.DisplaySize);
                    io.AddMousePosEvent(layout.contentX+22,layout.boardY+120);
                    io.AddMouseButtonEvent(0,frame==165);
                }
                if(frame==168||frame==169) io.AddKeyEvent(ImGuiKey_8,frame==168);
                if(frame==170||frame==171) io.AddKeyEvent(ImGuiKey_Escape,frame==170);
                const auto projected=gfx::Renderer::projectEquipment(makeScene(timeline,frame/60.0f,session),settings,width,height);
                for(std::size_t i=0;i<projected.size();++i) {
                    const int clickFrame=10+static_cast<int>(i)*6;
                    if(frame==clickFrame||frame==clickFrame+1) {
                        io.AddMousePosEvent(projected[i].x*io.DisplaySize.x,projected[i].y*io.DisplaySize.y);
                        io.AddMouseButtonEvent(0,frame==clickFrame);
                    }
                    const int iconFrame=190+static_cast<int>(i)*6;
                    if(frame==iconFrame||frame==iconFrame+1) {
                        const auto layout=tableLayout(io.DisplaySize);
                        io.AddMousePosEvent(layout.contentX+(static_cast<float>(i)+.5f)*layout.contentWidth/8,equipmentLayout(layout.contentWidth).centerY);
                        io.AddMouseButtonEvent(0,frame==iconFrame);
                    }
                    const int keyFrame=90+static_cast<int>(i)*8;
                    if(frame==keyFrame||frame==keyFrame+1) io.AddKeyEvent(static_cast<ImGuiKey>(ImGuiKey_1+static_cast<int>(i)),frame==keyFrame);
                }
            }
            if(options.controlsSmoke) {
                auto& input=ImGui::GetIO();
                const auto press=[&](int at,ImGuiKey key) {
                    if(frame==at) input.AddKeyEvent(key,true);
                    if(frame==at+1) input.AddKeyEvent(key,false);
                };
                press(2,ImGuiKey_F1);press(5,ImGuiKey_F1);
                press(8,ImGuiKey_F3);press(11,ImGuiKey_F3);
                press(14,ImGuiKey_F4);press(17,ImGuiKey_F4);
                press(20,ImGuiKey_P);press(40,ImGuiKey_P);
                if(frame==23) input.AddKeyEvent(ImGuiKey_L,true);
                if(frame==30) input.AddKeyEvent(ImGuiKey_L,false);
                press(34,ImGuiKey_0);
            }
            ImGui::NewFrame();
            beginSoundFrame();
            hoverSoundsEnabled=!session.transitioning();
            if(!options.captures.empty()) {
                const auto display=ImGui::GetIO().DisplaySize;
                const auto size=std::make_pair(static_cast<int>(display.x),static_cast<int>(display.y));
                if(checkedRelicLayouts.insert(size).second) verifyRelicTextLayout(interface,tableLayout(display),display);
            }
            session.syncEquipmentContext(game.state());
            if(!ImGui::GetIO().WantTextInput) {
                if(ImGui::IsKeyPressed(ImGuiKey_P,false) && !session.transitioning()) scenePaused=!scenePaused;
                if(ImGui::IsKeyPressed(ImGuiKey_F3,false)) settings.lighting=!settings.lighting;
                if(ImGui::IsKeyPressed(ImGuiKey_F4,false)) settings.textures=!settings.textures;
                if(ImGui::IsKeyPressed(ImGuiKey_0,false)) settings.lightX=settings.lightY=settings.lightZ=0;
                const auto held=[](ImGuiKey key) {return ImGui::IsKeyDown(key)?1.0f:0.0f;};
                settings.lightX=std::clamp(settings.lightX+(held(ImGuiKey_L)-held(ImGuiKey_J))*dt*3.0f,-8.0f,8.0f);
                settings.lightY=std::clamp(settings.lightY+(held(ImGuiKey_U)-held(ImGuiKey_O))*dt*3.0f,-3.0f,5.0f);
                settings.lightZ=std::clamp(settings.lightZ+(held(ImGuiKey_K)-held(ImGuiKey_I))*dt*3.0f,-8.0f,8.0f);
            }
            const bool overlay=session.menu || session.settings || session.rules || session.collection || session.deckSelection || session.transitioning();
            const bool wasBusy=timeline.busy();
            Commands commands{overlay || wasBusy || scenePaused,false,&game,&timeline};
            if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {
                // Every Escape that steps back (closes a panel, goes up a level) plays the close sound.
                if(session.transitioning()) {session.entrance.skip();playSound(audio::Sfx::UiClose);}
                else if(session.rules) {session.rules=false;playSound(audio::Sfx::UiClose);}
                else if(session.settings) {session.settings=false;playSound(audio::Sfx::UiClose);}
                else if(session.collection) {session.collection=false;playSound(audio::Sfx::UiClose);}
                else if(session.deckSelection) {session.deckSelection=false;playSound(audio::Sfx::UiClose);}
                else if(!session.menu&&(session.selectedEquipment>=0||session.hoveredRelic>=0)) {session.clearEquipmentContext();playSound(audio::Sfx::UiClose);}
                else if(!session.transitioning()) {
                    if(!session.menu) playSound(audio::Sfx::UiClose);
                    session.returnToMenu();
                }
            }
            if(ImGui::IsKeyPressed(ImGuiKey_F1,false) && !session.transitioning()) {
                session.settings=false;session.rules=!session.rules;session.controlsTab=true;
                playSound(session.rules?audio::Sfx::UiOpen:audio::Sfx::UiClose);
            }
            if(ImGui::IsKeyPressed(ImGuiKey_F2,false) && !session.transitioning()) {
                session.rules=false; session.settings=!session.settings;
                playSound(session.settings?audio::Sfx::UiOpen:audio::Sfx::UiClose);
            }
            const bool space=ImGui::IsKeyPressed(ImGuiKey_Space,false);
            const bool gameSpace=ImGui::IsKeyDown(ImGuiKey_Space) && !session.settings && !session.rules &&
                !session.collection && !session.deckSelection;
            if(wasBusy && !overlay && !scenePaused && (space || ImGui::IsMouseClicked(0))) timeline.accelerate();
            commands.blocked=session.menu || session.settings || session.rules || session.collection || session.deckSelection || session.transitioning() || wasBusy || scenePaused;
            if(!scenePaused && !ImGui::GetIO().WantTextInput && !session.settings && !session.rules && !session.collection && !session.deckSelection && !session.transitioning() && !wasBusy) {
                const auto owned=game.state().owned;
                for(std::size_t i=0;i<owned.size();++i) if(ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1+static_cast<int>(i)),false)) {
                    session.selectedEquipment=static_cast<int>(i);session.inspectedRelic=owned[i];
                    session.suppressEquipmentHover=true;
                    playSound(audio::Sfx::UiOpen);
                }
                if(ImGui::IsKeyPressed(ImGuiKey_A,false)&&session.selectedEquipment>=0&&session.selectedEquipment<static_cast<int>(owned.size()))
                    commands.apply([&]{return game.activate(owned[static_cast<std::size_t>(session.selectedEquipment)]);},true);
                if(!game.state().cardChoices.empty()) {
                    if(ImGui::IsKeyPressed(ImGuiKey_Q,false)) commands.apply([&]{return game.chooseCard(0);},true);
                    if(ImGui::IsKeyPressed(ImGuiKey_E,false)) commands.apply([&]{return game.chooseCard(1);},true);
                }
                if(ImGui::IsKeyPressed(ImGuiKey_H,false)) commands.apply([&](){return game.hit();});
                if(ImGui::IsKeyPressed(ImGuiKey_S,false)) commands.apply([&](){return game.stand();});
                if(space && !commands.used) {
                    if(session.menu) {session.deckSelection=true;playSound(audio::Sfx::UiOpen);}
                    else if(game.state().phase==bj::Phase::RunLost) {
                        if(game.state().revivesLeft>0) commands.apply([&](){return game.revive();});
                        else {session.start(game);playSound(audio::Sfx::UiOpen);}
                    } else primaryAction(game,commands);
                }
            }
            if(!scenePaused && options.autoplay && frame>180 && frame%10==0 && !session.settings && !session.rules && !session.collection && !session.deckSelection && !session.transitioning()) {
                if(timeline.busy()) timeline.accelerate();
                else if(game.state().phase==bj::Phase::RunLost) {
                    if(session.gameOverElapsed>=1.1f) {
                        if(game.state().revivesLeft) commands.apply([&](){return game.revive();});
                        else session.start(game);
                    }
                } else if(session.menu && !options.smoke) session.start(game);
                else autoplayStep(game,commands);
            }
            feedback.observe(settledState(timeline),animationDt,!session.menu && !session.transitioning());
            session.syncEquipmentContext(game.state());
            const float sceneTime=static_cast<float>(sceneSeconds);
            effectTime=sceneTime;
            auto scene=makeScene(timeline,sceneTime,session);
            conduct(scene,sceneTime);
            drawPhysicalEquipment(scene,settings,width,height,game,commands,session,interface);
            // Space is handled above; prevent focused buttons from receiving it again.
            ImGui::BeginDisabled(gameSpace);
            if(session.menu) drawMenu(game,session,interface,window.handle);
            else drawTable(game,commands,session,feedback,interface,art,window.handle,timeline,settings.animate);
            drawCardChoices(game,commands,interface,session);
            drawDeckSelection(game,session,interface,art);
            if(session.transitioning()) scenePaused=false;
            drawCollection(session,interface,art);
            if(drawSettings(session,settings,interface,volumes,volumeSaveFailed)) {
                sound.setVolumes(volumes.music,volumes.effects);
                volumeDirty=true;
                volumeEditedAt=now;
                volumeSaveFailed=false;
            }
            drawRules(session,interface,settings,scenePaused);
            ImGui::EndDisabled();
            // Apply while dragging; save on release, close, or after keyboard edits settle.
            if(volumeDirty&&(!session.settings||ImGui::IsMouseReleased(0)||
                (!ImGui::IsMouseDown(0)&&now-volumeEditedAt>.4))) {
                if(!audioSettingsPath.empty()) volumeSaveFailed=!volumes.save(audioSettingsPath);
                volumeDirty=false;
            }
            // Drain after both keyboard and mouse actions so either input starts audio on this frame.
            for(const auto cue:timeline.takeCues()) playCue(cue);
            if(pendingUiSound!=audio::Sfx::Count) sound.play(pendingUiSound);
            pendingUiSound=audio::Sfx::Count;
            sound.setMusic(session.menu&&!session.transitioning()?audio::MusicMode::Menu:audio::MusicMode::Game);
            sound.setDucked(!session.menu&&session.gameOverElapsed>0.0f);
            // The same street phase drives rendering and the recording; its peak matches the midpoint.
            if(session.menu&&settings.animate&&!scenePaused&&!session.transitioning()&&presentation::CarPass::visible(sceneTime)) {
                const int cycle=static_cast<int>(sceneTime/presentation::CarPass::cycleSeconds);
                if(carSoundCycle!=cycle) {
                    sound.carPass(presentation::CarPass::recordingOffset(sceneTime));
                    carSoundCycle=cycle;
                    carSoundActive=true;
                }
            } else if(carSoundActive) {
                sound.carStop(1.2);
                carSoundActive=false;
                carSoundCycle=-1;
            }
            if(options.abilitySmoke) {
                const auto used=game.state().usedHandAbilities.size()+game.state().usedRoundAbilities.size();
                if(used>abilityCountBefore) ++abilityActivations;
                if(choiceBefore&&game.state().cardChoices.empty()) ++choiceSelections;
            }
            const auto& io=ImGui::GetIO();
            const auto layout=tableLayout(io.DisplaySize);
            if(options.equipmentSmoke) {
                if(game.state().usedHandAbilities.size()+game.state().usedRoundAbilities.size()!=abilityCountBefore)
                    throw std::runtime_error("Inspecting equipment consumed an ability");
                if(frame==167&&session.selectedEquipment!=-1) throw std::runtime_error("Click outside did not dismiss equipment context");
                if(frame==173&&(session.selectedEquipment!=-1||session.menu)) throw std::runtime_error("Escape did not dismiss equipment context");
                for(std::size_t i=0;i<scene.equipment.size();++i) {
                    if(frame==12+static_cast<int>(i)*6) {
                        if(session.selectedEquipment!=static_cast<int>(i)) throw std::runtime_error("Physical equipment click failed slot="+std::to_string(i)+" selected="+std::to_string(session.selectedEquipment)+" hover="+std::to_string(session.hoveredRelic));
                        ++equipmentClicks;
                    }
                    if(frame==192+static_cast<int>(i)*6) {
                        if(session.selectedEquipment!=static_cast<int>(i)) throw std::runtime_error("Illustrated equipment icon selected the wrong relic");
                        ++equipmentIconClicks;
                    }
                    if(frame==92+static_cast<int>(i)*8 && !wasBusy) {
                        if(session.selectedEquipment!=static_cast<int>(i)) throw std::runtime_error("Equipment key selection failed");
                        ++abilityKeys;
                    }
                }
            }
            const bool freeTable=!session.menu && !session.settings && !session.rules && !session.transitioning() &&
                timeline.visible().phase!=bj::Phase::Shop && timeline.visible().phase!=bj::Phase::RunLost &&
                io.MousePos.x>layout.contentX && io.MousePos.y>layout.boardY && io.MousePos.y<layout.actionY &&
                !session.physicalChipHovered && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
            if(!ImGui::IsMouseDown(1) || !freeTable) cameraDragging=false;
            if(freeTable && ImGui::IsMouseDoubleClicked(1)) {resetCamera(settings);cameraDragging=false;}
            else if(freeTable && ImGui::IsMouseClicked(1)) cameraDragging=true;
            if(cameraDragging && freeTable && ImGui::IsMouseDragging(1,3)) {
                settings.yaw=std::clamp(settings.yaw-io.MouseDelta.x*.20f,-35.0f,35.0f);
                settings.pitch=std::clamp(settings.pitch+io.MouseDelta.y*.16f,32.0f,68.0f);
            }
            if(freeTable && io.MouseWheel!=0) settings.distance=std::clamp(settings.distance-io.MouseWheel*.5f,gfx::minZoom,gfx::maxZoom);
            if(options.smoke) {
                smokeCollection=smokeCollection||session.collection;
                smokeDeck=smokeDeck||session.deckSelection;
                const int sizeIndex=io.DisplaySize.y<800?0:1;
                if(session.deckSelection) smokeDeckMasks[sizeIndex]|=1<<deckIndex(session.selectedDeck);
                if(session.collection&&session.collectionFilter==3&&session.selectedRelic==bj::Relic::SecondChance)
                    smokeSelectedCollection[sizeIndex]=true;
                smokeDealt=smokeDealt||game.state().handsLeft<game.state().maxHands;
                smokeStood=smokeStood||game.state().outcome!=bj::Outcome::None;
                smokeRevived=smokeRevived||game.state().revivalsUsed>0;
                maxOwned=std::max(maxOwned,static_cast<int>(game.state().owned.size()));
                maxRound=std::max(maxRound,game.state().round);
                if(!options.demo&&((frame==2&&!session.settings)||(frame==8&&session.settings))) throw std::runtime_error("Settings input failed");
                if(!options.demo&&frame==24&&(session.selectedDeck!=options.deck||session.transitioning()))
                    throw std::runtime_error("Deck controls accepted input behind settings");
                if(!options.demo&&frame==24&&(!session.deckSelection||session.settings))
                    throw std::runtime_error("Escape closed the deck carousel instead of settings");
                if(frame==129&&std::abs(settings.yaw-defaultView.yaw)<1) throw std::runtime_error("Right drag did not rotate camera");
                if(frame==131&&settings.distance>=defaultView.distance) throw std::runtime_error("Mouse wheel did not zoom camera");
                if(frame==140&&(std::abs(settings.yaw-defaultView.yaw)>.01f||std::abs(settings.pitch-defaultView.pitch)>.01f||
                    std::abs(settings.distance-defaultView.distance)>.01f))
                    throw std::runtime_error("Double right-click did not reset camera");
                if(frame==148&&(std::abs(settings.yaw-defaultView.yaw)>.01f||std::abs(settings.pitch-defaultView.pitch)>.01f))
                    throw std::runtime_error("Left drag changed camera");
            }
            if(options.smoke) {
                if(frame==150) {settings.yaw=-35;settings.pitch=32;settings.distance=8;}
                if(frame==154) {settings.yaw=35;settings.pitch=68;settings.distance=14;}
                if(frame==158) resetCamera(settings);
            }
            session.updateBustReaction(0,timeline.visible());
            drawGameplayShading(session,timeline);
            scene=makeScene(timeline,sceneTime,session);
            conduct(scene,sceneTime);
            if(options.controlsSmoke) {
                if(frame==3&&!session.rules) throw std::runtime_error("F1 did not open help");
                if(frame==6&&session.rules) throw std::runtime_error("F1 did not close help");
                if(frame==9&&settings.lighting) throw std::runtime_error("F3 did not disable lighting");
                if(frame==12&&!settings.lighting) throw std::runtime_error("F3 did not restore lighting");
                if(frame==15&&settings.textures) throw std::runtime_error("F4 did not disable textures");
                if(frame==18&&!settings.textures) throw std::runtime_error("F4 did not restore textures");
                if(frame==21) {
                    if(!scenePaused) throw std::runtime_error("P did not pause the scene");
                    pausedSmokeTime=sceneSeconds;pausedSmokeBeats=scene.band.beats;pausedSmokeArrival=timeline.playerArrival();
                }
                if(frame>21&&frame<40&&(sceneSeconds!=pausedSmokeTime||scene.band.beats!=pausedSmokeBeats||timeline.playerArrival()!=pausedSmokeArrival))
                    throw std::runtime_error("Scene, band or card animation moved while paused");
                if(frame==31&&settings.lightX<.2f) throw std::runtime_error("Light movement did not work while paused");
                if(frame==35&&settings.lightX!=0) throw std::runtime_error("Light reset failed");
                if(frame==43&&(scenePaused||sceneSeconds<=pausedSmokeTime)) throw std::runtime_error("P did not resume the scene");
            }
            renderer.render(scene,settings,width,height);
            if(!session.transitioning()) {
                std::ostringstream status;
                status<<"F1 Help  |  F3 Lighting "<<(settings.lighting?"ON":"OFF")
                    <<"  |  F4 Textures "<<(settings.textures?"ON":"OFF")
                    <<"  |  "<<(scenePaused?"PAUSED - P to resume":"P Pause");
                auto* foreground=ImGui::GetForegroundDrawList();
                const auto color=scenePaused?IM_COL32(240,199,124,245):IM_COL32(196,189,176,210);
                foreground->AddText(interface.small,16.0f,{16,2},IM_COL32(0,0,0,220),status.str().c_str());
                foreground->AddText(interface.small,16.0f,{15,1},color,status.str().c_str());
            }
            if(session.transitioning() && session.menu) ImGui::GetForegroundDrawList()->AddText({24,24},IM_COL32(242,218,175,160),"ESC / SKIP ENTRANCE");
            if (session.transitioning()) ImGui::GetForegroundDrawList()->AddRectFilled(
                {0, 0}, ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 0, static_cast<int>(255 * session.darkness())));
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) throw std::runtime_error("OpenGL error: " + std::to_string(error));
            if (!options.captures.empty() && !session.transitioning() && !commands.used) {
                const std::string name = session.collection ? "collection" : session.deckSelection ? "decks" : timeline.busy() ? (timeline.rewardStep()>=0 ? "reward" : "dealing") : session.rules ? "rules" : session.settings ? "settings" : session.menu ? "menu" :
                    game.state().phase == bj::Phase::Shop ? (game.state().owned.size() == bj::maxEquipmentSlots ? "shop-full" : "shop") :
                    game.state().phase == bj::Phase::RunLost ? "end" :
                    game.state().phase == bj::Phase::RoundOver && game.state().roundScore >= game.state().goal ? "round-clear" :
                    game.state().phase == bj::Phase::RoundOver && game.state().outcome == bj::Outcome::Loss &&
                        game.state().handsLeft > 0 ? "hand-loss" : "table";
                const auto capture = [&](const std::string& tag) {
                    if (captured.insert(tag).second) saveScreenshot(options.captures / (tag + ".png"), width, height);
                };
                const std::string suffix=height<800?"-small":"-large";
                if(name!="end" || session.gameOverElapsed>=.8f) capture(name);
                if(session.selectedEquipment>=0) capture("equipment-selected-"+std::to_string(session.selectedEquipment)+suffix);
                if(session.hoveredRelic>=0) capture("equipment-hover-"+std::to_string(session.hoveredRelic)+suffix);
                if(!game.state().cardChoices.empty()) capture("ability-card-choice"+suffix);
                if(game.state().pendingRescue&&!timeline.busy()) capture("ability-rescue-pending"+suffix);
                if(options.abilitySmoke&&abilityActivations>0&&!timeline.busy()) capture("ability-used"+suffix);
                if(name=="end") {
                    if(session.gameOverElapsed>=.20f && session.gameOverElapsed<.40f) capture("end-fade-early"+suffix);
                    if(session.gameOverElapsed>=.50f && session.gameOverElapsed<.70f) capture("end-fade-late"+suffix);
                    if(session.gameOverElapsed>=.8f) capture("end"+suffix);
                }
                if(session.deckSelection) {
                    const char* names[]={"hands","life","coins"};
                    capture(std::string("deck-")+names[deckIndex(session.selectedDeck)]+suffix);
                    if(session.deckMotion>.8f) capture("carousel-motion"+suffix);
                }
                if(session.collection&&session.collectionFilter==3&&session.selectedRelic==bj::Relic::SecondChance)
                    capture("collection-selected"+suffix);
                if(!session.menu&&!session.settings&&!session.rules&&session.hoveredRelic>=0&&
                    session.relicHover[session.hoveredRelic]>.7f&&game.state().phase!=bj::Phase::Shop&&game.state().phase!=bj::Phase::RunLost)
                    capture("relic-hover"+suffix);
                if(name=="shop-full"||name=="table"||name=="collection"||name=="menu") capture(name+suffix);
                if(game.state().phase==bj::Phase::Shop) {
                    for(auto relic:game.state().offers) {
                        if(relicInfo(relic).rarity==bj::Rarity::Legendary) capture("shop-legendary");
                        if(relicInfo(relic).rarity==bj::Rarity::Rare) capture("shop-rare");
                    }
                }
                if(session.collection && session.collectionFilter) capture("collection-filter");
                if(!session.menu && game.state().phase!=bj::Phase::Shop && !session.history.records().empty())
                    capture("journal-filled");
                if(!session.menu && game.state().phase!=bj::Phase::Shop && session.history.records().size()>=5)
                    capture("journal-history");
                const auto& visible=timeline.visible();
                const auto hand = bj::evaluateHand(visible.player);
                if(timeline.rewardStep()>0 && !visible.score.appliedEffects.empty())
                    capture("applied-relics-"+std::to_string(timeline.rewardStep()));
                const float effectDuration=.5f+(.07f-.5f)*std::clamp(session.animationSpeed,0.0f,1.0f);
                if(!session.menu && !session.settings && !session.rules && timeline.busy() &&
                    session.relicReactionStep>0 && session.relicReactionElapsed>=effectDuration*.25f)
                    capture("relic-reaction-"+std::to_string(session.relicReactionStep)+suffix);
                if(visible.owned.size()==bj::maxEquipmentSlots && visible.phase!=bj::Phase::Shop) {
                    capture("relics-full");
                    capture("relics-full"+suffix);
                }
                if (feedback.remaining > 0 && !session.menu && !session.settings && !session.rules &&
                    game.state().phase != bj::Phase::RunLost) {
                    capture("card-gain");
                    if (feedback.delta <= 0) capture("ace-recount");
                }
                if (hand.bust && visible.phase == bj::Phase::RoundOver) capture("bust");
                if (!timeline.busy() && hand.blackjack && visible.outcome == bj::Outcome::Win &&
                    visible.phase == bj::Phase::RoundOver) {
                    capture("blackjack");
                    if (game.hasRelic(bj::Relic::BlackjackBonus)) capture("blackjack-bonus");
                }
            }
            if (options.smoke && !options.captures.empty() && (frame == 65 || frame == 78 || frame == 91))
                saveScreenshot(options.captures / ("fade-" + std::to_string(frame) + ".png"), width, height);
            if(options.smoke && !options.captures.empty() && frame==275)
                saveScreenshot(options.captures/"table-small.png",width,height);
            if(options.smoke && !options.captures.empty() && (frame==152||frame==156))
                saveScreenshot(options.captures/(frame==152?"camera-low-close.png":"camera-high-far.png"),width,height);
            if(!options.animationFrames.empty()) {
                std::ostringstream name;name<<std::setw(5)<<std::setfill('0')<<frame<<".png";
                saveScreenshot(options.animationFrames/name.str(),width,height);
            }
            if(!options.captures.empty()&&session.transitioning()) {
                const float elapsed=session.entrance.elapsed();
                const auto captureEntrance=[&](const char* name,float start,float end) {
                    if(elapsed>=start&&elapsed<end&&captured.insert(name).second) saveScreenshot(options.captures/(std::string(name)+".png"),width,height);
                };
                captureEntrance("entrance-buttons-fade",.2f,.3f);
                captureEntrance("entrance-approach",1.3f,1.4f);
                captureEntrance("entrance-door-opening",2.65f,2.75f);
                captureEntrance("entrance-push",3.2f,3.3f);
                captureEntrance("entrance-black",3.85f,3.86f);
            }
            ++frame;
            const bool finished = options.frames > 0 && frame >= options.frames;
            if (finished && !options.screenshot.empty()) saveScreenshot(options.screenshot, width, height);
            glfwSwapBuffers(window.handle);
            if(options.benchmark) {
                glFinish();
                if(frame>60) {benchmarkSeconds+=glfwGetTime()-frameStarted;++benchmarkFrames;}
            }
            if(!scenePaused) sceneSeconds+=dt;
            if (finished) {
                if(options.smoke && !options.demo && frame>=400 && (!smokeCollection || !smokeDeck || !smokeDealt || !smokeStood))
                    throw std::runtime_error("Smoke traversal incomplete");
                if(options.smoke&&!options.demo&&frame>=400&&
                    (smokeDeckMasks[0]!=7||smokeDeckMasks[1]!=7||!smokeSelectedCollection[0]||!smokeSelectedCollection[1]))
                    throw std::runtime_error("Art screen traversal incomplete");
                if(options.smoke && !options.demo && options.deck==bj::StartDeck::SecondLife && frame>=2000 && !smokeRevived)
                    throw std::runtime_error("Revival traversal incomplete");
                break;
            }
        }
        if(volumeDirty&&!audioSettingsPath.empty()) volumes.save(audioSettingsPath);
        std::cout << "Rendered frames: " << frame << '\n';
        if(options.controlsSmoke) {
            if(frame<44) throw std::runtime_error("Scene controls check ended before all assertions ran");
            std::cout<<"Scene controls: help, lighting, textures, pause/resume and movable light passed\n";
        }
        if(options.benchmark&&benchmarkFrames>0) {
            std::cout<<"GPU: "<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<'\n'
                <<"OpenGL: "<<reinterpret_cast<const char*>(glGetString(GL_VERSION))<<'\n'
                <<"Benchmark 1280x720, lighting="<<(settings.lighting?"on":"off")
                <<", frames="<<benchmarkFrames<<", mean ms="<<std::fixed<<std::setprecision(2)
                <<benchmarkSeconds*1000.0/benchmarkFrames<<", FPS="<<benchmarkFrames/benchmarkSeconds<<'\n';
        }
        if(options.smoke) std::cout << "Smoke: collection="<<smokeCollection<<" decks="<<smokeDeck
            <<" dealt="<<smokeDealt<<" resolved="<<smokeStood<<" revived="<<smokeRevived
            <<" max relics="<<maxOwned<<" max round="<<maxRound
            <<" deck masks="<<smokeDeckMasks[0]<<'/'<<smokeDeckMasks[1]
            <<" selected collection="<<smokeSelectedCollection[0]<<'/'<<smokeSelectedCollection[1]<<'\n';
        if(options.abilitySmoke) {
            if(abilityActivations==0) throw std::runtime_error("No ability activated through actual key input");
            std::cout<<"Ability input: activations="<<abilityActivations<<" choices="<<choiceSelections<<'\n';
        }
        if(options.equipmentSmoke) std::cout<<"Equipment input: projected clicks="<<equipmentClicks<<" icon clicks="<<equipmentIconClicks<<" key selections="<<abilityKeys<<'\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Jester Jack: " << error.what() << '\n';
        return 1;
    }
}
