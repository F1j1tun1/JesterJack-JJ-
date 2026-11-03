# Jester Jack

A single-player blackjack game with endless rounds, relics and a theatrical casino. Built with C++17 and OpenGL 3.3.

## Play on Windows

Download this repository using **Code → Download ZIP**, extract the entire archive, then open **JesterJack.exe**. Keep `assets/` and `licenses/` beside it. An OpenGL 3.3 driver is required; music and sound are included.

- **H / HIT:** take a card. **S / STAND:** finish the hand.
- **Space:** main action or speed up an animation.
- **1–8:** inspect a relic. **A:** activate its available ability.
- **Right-drag / wheel:** rotate / zoom the camera. Double right-click resets it.
- **F1:** options. **Escape:** close the current panel or return to the menu.

Reach each round's target within the available hands. Wins earn coins for relics; failing a round ends the run. Full rules are in HOW TO PLAY.

## Build

Install CMake 3.24+ and Visual Studio 2022/2026 or Build Tools with MSVC, Windows SDK and the Desktop development with C++ component. Dependencies are included in `third_party/`.

From the repository root:

```powershell
cmake --preset desktop
cmake --build build/desktop --config Release --parallel 2
ctest --test-dir build/desktop -C Release --output-on-failure
.\build\desktop\Release\blackjack_forge.exe
```

`scripts/run.ps1 -Release` also builds, tests and starts the game. The source code is in `src/`; resources are in `assets/`. Preserve the included library, font and asset notices in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This version matches the verified desktop package from 3 November 2026, based on [Final-Project-CGF commit 7cf4bde](https://github.com/F1j1tun1/Final-Project-CGF/commit/7cf4bde215c5310d6b04d83fe90f752bfd2037be). The ready Windows executable and matching source are included together.
