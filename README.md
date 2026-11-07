# Jester Jack

A single-player blackjack game in a 3D casino, built with C++17 and OpenGL 3.3. Reach each round's target within the available hands and spend coins on relics. Full rules are in HOW TO PLAY.

**JesterJack · IT-2502:** Yerkingali Saruar, Baibatyr Miras, Yesmagzam Sultan.

## Play on Windows

On Windows 10 or later, extract the complete package and open **JesterJack.exe**. Keep `assets/`, `licenses/` and `THIRD_PARTY_NOTICES.md` beside it. An OpenGL 3.3 capable graphics driver is required. Music and sound are included.

## Controls

- **H / HIT:** take a card. **S / STAND:** finish the hand.
- **Space:** main action or speed up an animation.
- **1–8:** inspect a relic. **A:** activate its available ability.
- **Q / E:** choose the left / right card when an ability offers a choice.
- **Right-drag / wheel:** rotate / zoom the camera. Double right-click resets it.
- **F1:** controls and help. **F2:** options. **P:** pause / resume scene animation and actions.
- **F3:** lighting on / off. **F4:** textures on / off.
- **I / J / K / L:** move the light horizontally along X/Z. **U / O:** move it up / down. **0:** reset the light.
- **Escape:** close the current panel or return to the menu.

Options has separate **Music** and **Effects** volume sliders. Changes apply immediately and are remembered between launches; 0% mutes that channel.

## Build

Install **Visual Studio 2022 or Build Tools**, the **Desktop development with C++** workload (MSVC and Windows SDK), and **CMake 3.24+**. Visual Studio 2026 requires **CMake 4.2+**. Dependencies are included in `third_party/`; configuration needs no downloads.

In the desktop package, extract `JesterJack-Source.zip` first. From the source folder in PowerShell, using Visual Studio 2022:

```powershell
cmake --preset desktop -G "Visual Studio 17 2022" -A x64
cmake --build build/desktop --config Release --parallel 2
ctest --test-dir build/desktop -C Release --output-on-failure
.\build\desktop\Release\blackjack_forge.exe
```

For Visual Studio 2026, replace `Visual Studio 17 2022` with `Visual Studio 18 2026` in the first command. Use a fresh build folder when changing compilers. After configuration, `scripts/run.ps1 -Release` builds, tests and starts the game.

## Performance comparison

Run these commands from the package folder:

```powershell
.\JesterJack.exe --benchmark
.\JesterJack.exe --benchmark --lighting-off
```

Each run measures actual full-frame FPS in the default scene at 1280 × 720 and logs the result. The second run disables lighting for comparison. Use the same computer and graphics settings for both runs. With a source build, substitute `.\build\desktop\Release\blackjack_forge.exe` for `.\JesterJack.exe`.

The source is in `src/`, resources in `assets/`, and tests in `tests/`. Preserve the library, font and asset notices in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
