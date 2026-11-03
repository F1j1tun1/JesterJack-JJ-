cmake_minimum_required(VERSION 3.24)
project(JesterJack VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
option(BJ_BUILD_APP "Build the OpenGL desktop game" ON)
include(CTest)

add_library(blackjack_core src/game/Game.cpp src/presentation/Presentation.cpp src/presentation/BandSync.cpp)
target_include_directories(blackjack_core PUBLIC src)

# Sound: a platform-independent mixer plus a playback thread. Windows plays MP3 through the
# built-in winmm/MCI decoder (no extra dependency); other platforms are silent no-ops.
find_package(Threads REQUIRED)
add_library(blackjack_audio src/audio/Mixer.cpp src/audio/Engine.cpp)
target_include_directories(blackjack_audio PUBLIC src)
target_link_libraries(blackjack_audio PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(blackjack_audio PUBLIC winmm)
endif()

function(bj_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
bj_warnings(blackjack_core)
bj_warnings(blackjack_audio)

# The on-stage jazz band: pure geometry and pose maths (glm only, no OpenGL), so it is testable headless.
add_library(blackjack_band src/graphics/BandModel.cpp)
target_include_directories(blackjack_band PUBLIC src)
target_include_directories(blackjack_band SYSTEM PUBLIC third_party/glm)
bj_warnings(blackjack_band)

if(BUILD_TESTING)
    add_executable(blackjack_tests tests/game_tests.cpp)
    target_link_libraries(blackjack_tests PRIVATE blackjack_core)
    bj_warnings(blackjack_tests)
    add_test(NAME blackjack_rules COMMAND blackjack_tests)
    add_executable(presentation_tests tests/presentation_tests.cpp)
    target_link_libraries(presentation_tests PRIVATE blackjack_core)
    bj_warnings(presentation_tests)
    add_test(NAME card_presentation COMMAND presentation_tests)
    add_executable(score_history_tests tests/score_history_tests.cpp)
    target_link_libraries(score_history_tests PRIVATE blackjack_core)
    bj_warnings(score_history_tests)
    add_test(NAME score_history COMMAND score_history_tests)
    add_executable(entrance_tests tests/entrance_tests.cpp)
    target_include_directories(entrance_tests PRIVATE src)
    bj_warnings(entrance_tests)
    add_test(NAME cinematic_entrance COMMAND entrance_tests)
    add_executable(band_tests tests/band_tests.cpp)
    target_link_libraries(band_tests PRIVATE blackjack_core blackjack_band)
    bj_warnings(band_tests)
    add_test(NAME jazz_band COMMAND band_tests)
    add_executable(audio_tests tests/audio_tests.cpp)
    target_link_libraries(audio_tests PRIVATE blackjack_audio)
    bj_warnings(audio_tests)
    add_test(NAME sound_mixer COMMAND audio_tests)
    add_executable(balance_sim scripts/balance_sim.cpp)
    target_link_libraries(balance_sim PRIVATE blackjack_core)
    target_compile_definitions(balance_sim PRIVATE BJ_BALANCE_ACTIVE=1)
    bj_warnings(balance_sim)
endif()

if(BJ_BUILD_APP)
    # Sources are vendored: the TA can configure/build without internet access.
    foreach(dependency glfw glm imgui)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${dependency}")
            message(FATAL_ERROR "Missing third_party/${dependency}; use the complete source archive.")
        endif()
    endforeach()
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
    find_package(OpenGL REQUIRED)

    add_library(bj_glm INTERFACE)
    target_include_directories(bj_glm SYSTEM INTERFACE third_party/glm)
    add_library(bj_glad STATIC src/graphics/glad.cpp)
    # GLFW 3.4 ships this generated GLAD header; keep GLFW pinned when updating.
    target_include_directories(bj_glad SYSTEM PUBLIC third_party/glfw/deps)
    target_link_libraries(bj_glad PUBLIC OpenGL::GL)

    add_library(bj_imgui STATIC
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_glfw.cpp
        third_party/imgui/backends/imgui_impl_opengl3.cpp)
    target_include_directories(bj_imgui SYSTEM PUBLIC third_party/imgui third_party/imgui/backends)
    target_compile_definitions(bj_imgui PRIVATE GLFW_INCLUDE_NONE)
    target_link_libraries(bj_imgui PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS})

    add_executable(blackjack_forge src/main.cpp src/graphics/Renderer.cpp)
    if(WIN32)
        enable_language(RC)
        configure_file(src/windows.rc.in JesterJack.rc @ONLY)
        target_sources(blackjack_forge PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/JesterJack.rc")
    endif()
    target_compile_definitions(blackjack_forge PRIVATE GLFW_INCLUDE_NONE)
    target_link_libraries(blackjack_forge PRIVATE blackjack_core blackjack_audio blackjack_band bj_glad bj_glm bj_imgui)
    bj_warnings(blackjack_forge)
    # Keep shaders beside the executable, independent of the working directory.
    add_custom_target(blackjack_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:blackjack_forge>/assets"
        COMMENT "Copying game assets")
    add_dependencies(blackjack_forge blackjack_assets)
    install(TARGETS blackjack_forge RUNTIME DESTINATION .)
    install(DIRECTORY assets DESTINATION .)
    install(FILES THIRD_PARTY_NOTICES.md DESTINATION .)
    install(FILES third_party/glfw/LICENSE.md DESTINATION licenses RENAME GLFW.txt)
    install(FILES third_party/glm/copying.txt DESTINATION licenses RENAME GLM.txt)
    install(FILES third_party/imgui/LICENSE.txt DESTINATION licenses RENAME Dear-ImGui.txt)
    install(FILES third_party/glfw/deps/stb_image_write.h DESTINATION licenses)
    install(FILES third_party/glfw/deps/glad/gl.h DESTINATION licenses RENAME glad-generated-header.h)
endif()
