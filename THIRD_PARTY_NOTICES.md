# Third-party libraries

Dependencies are included as source in `third_party/`; no downloads happen during CMake configuration. Keep the upstream licenses when sharing source or binaries.

| Component | Pinned version | License location |
| --- | --- | --- |
| [GLFW](https://github.com/glfw/glfw/tree/3.4) | 3.4 | `third_party/glfw/LICENSE.md` (zlib/libpng) |
| [GLM](https://github.com/g-truc/glm/tree/1.0.1) | 1.0.1 | `third_party/glm/copying.txt` (MIT or modified MIT) |
| [Dear ImGui](https://github.com/ocornut/imgui/tree/v1.91.9b) | v1.91.9b | `third_party/imgui/LICENSE.txt` (MIT) |
| GLAD loader | Generated header supplied with GLFW 3.4 | `third_party/glfw/deps/glad/gl.h` (generated source) |
| stb_image_write | Header supplied with GLFW 3.4 | License text at the end of `third_party/glfw/deps/stb_image_write.h` (MIT or public domain) |

Downloaded release archive SHA-256 hashes:

```text
glfw  3.4      a133ddc3d3c66143eba9035621db8e0bcf34dba1ee9514a9e23e96afd39fd57a
glm   1.0.1    09c5716296787e1f7fcb87b1cbdbf26814ec1288ed6259ccd30d5d9795809fa5
imgui 1.91.9b  fd37507c8476a6d14cc7c4b352401f31bcbd0f0d995d35390811e968c466f46e
```

The game uses its own procedural meshes and textures; no Balatro artwork, audio or source code is included. Select the license for your team's own code before publicly publishing the repository.

## Jester Jack fonts

Jester Jack includes unmodified Cormorant Garamond Regular and SemiBold TrueType fonts by Christian Thalmann and the Cormorant Project Authors. They are licensed under the **SIL Open Font License 1.1**, preserved in `assets/fonts/OFL-CormorantGaramond.txt` and copied with the assets into builds and installed packages.

Source: [google-fonts-bower/cormorantgaramond-bower](https://github.com/google-fonts-bower/cormorantgaramond-bower/tree/bcf5515652e4d99eb3e42e41ed5f4285a315a175), commit `bcf5515652e4d99eb3e42e41ed5f4285a315a175`, acquired 16 October 2026. Exact source links and SHA-256 hashes are recorded in [assets/fonts/README.md](assets/fonts/README.md). The game does not depend on proprietary system fonts.

Readable body text, labels and buttons use unmodified **Source Sans 3 Regular and Semibold** static TrueType fonts, Copyright 2010-2024 Adobe, with Reserved Font Name `Source`. They are licensed under the **SIL Open Font License 1.1**, preserved in `assets/fonts/OFL-SourceSans3.txt` and distributed with the assets.

Source: official [adobe-fonts/source-sans](https://github.com/adobe-fonts/source-sans/tree/87b37a2daaed80fcb8e8ccb0085c4d72ddade12e), release commit `87b37a2daaed80fcb8e8ccb0085c4d72ddade12e`, acquired 16 October 2026. Exact source links and SHA-256 hashes are recorded in [assets/fonts/README.md](assets/fonts/README.md). Cormorant Garamond remains the display font for the main title.

## Jester Jack audio

`assets/audio/` contains the game's music and sound effects as supplied by the project team (file-by-file mapping in `assets/audio/README.md`), including the car recording used in the main menu. The authors, sources and licenses of these recordings are not recorded in this repository: the project owner must add them there and confirm that none of the files comes from a third-party game or sample library before the repository is published or the game is distributed.

MP3 playback on Windows uses the operating system's built-in MCI (`winmm`) decoder; no audio library is bundled.
