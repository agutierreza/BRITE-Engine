cmake_minimum_required(VERSION 3.20)
project(BRITEEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configure release asset packing
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(USE_PACKED_ASSETS)
endif()

include(FetchContent)

# ---------------------------------------------------------
# Dependencies
# ---------------------------------------------------------

# Raylib
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/master.tar.gz
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# EnTT
FetchContent_Declare(
    entt
    URL https://github.com/skypjack/entt/archive/refs/tags/v3.13.1.tar.gz
)
FetchContent_MakeAvailable(entt)

# Box2D
FetchContent_Declare(
    box2d
    URL https://github.com/erincatto/box2d/archive/refs/tags/v3.0.0.tar.gz
)
set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_VALIDATION OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box2d)

# nlohmann_json
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
)
FetchContent_MakeAvailable(nlohmann_json)

# PhysicsFS
FetchContent_Declare(
    physfs
    URL https://github.com/icculus/physfs/archive/refs/heads/main.zip
)
set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(physfs)

# rlImGui (Requires raylib and imgui)
FetchContent_Declare(
    rlImGui
    URL https://github.com/raylib-extras/rlImGui/archive/refs/heads/main.zip
)
FetchContent_MakeAvailable(rlImGui)

# spdlog
FetchContent_Declare(
    spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.13.0.tar.gz
)
FetchContent_MakeAvailable(spdlog)

# Tracy Profiler
FetchContent_Declare(
    tracy
    URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.10.tar.gz
)
set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tracy)

# SoLoud (Compiling directly from source)
FetchContent_Declare(
    soloud
    URL https://github.com/jarikomppa/soloud/archive/refs/tags/RELEASE_20200207.tar.gz
)
FetchContent_MakeAvailable(soloud)
add_library(soloud STATIC
    ${soloud_SOURCE_DIR}/src/core/soloud.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_audiosource.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_bus.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_3d.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_basicops.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_faderops.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_filterops.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_getters.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_setters.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_voicegroup.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_core_voiceops.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_fader.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_fft.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_fft_lut.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_file.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_filter.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_misc.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_queue.cpp
    ${soloud_SOURCE_DIR}/src/core/soloud_thread.cpp
    ${soloud_SOURCE_DIR}/src/backend/miniaudio/soloud_miniaudio.cpp
)
target_compile_definitions(soloud PRIVATE WITH_MINIAUDIO)
target_include_directories(soloud PUBLIC ${soloud_SOURCE_DIR}/include)

# ---------------------------------------------------------
# Core Engine Library
# ---------------------------------------------------------

add_library(${PROJECT_NAME} STATIC
    src/Core/InputManager.cpp
    src/Systems/RenderSystem.cpp
    src/Systems/PhysicsSystem.cpp
    src/Framework/Application.cpp
    src/Framework/Scene.cpp
)

# Include external header-only libraries and our own src
target_include_directories(${PROJECT_NAME} PUBLIC 
    include
    src
    external/tweeny/include
    ${physfs_SOURCE_DIR}/src
    ${rlimgui_SOURCE_DIR}
)

# Link Libraries
target_link_libraries(${PROJECT_NAME} PUBLIC 
    raylib 
    EnTT::EnTT 
    box2d 
    nlohmann_json::nlohmann_json 
    physfs-static
    spdlog::spdlog
    TracyClient
    soloud
)
