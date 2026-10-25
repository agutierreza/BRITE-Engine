cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
project(BRITEEngine C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)



# Configure release asset packing
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(USE_PACKED_ASSETS)
endif()

include(FetchContent)

# ---------------------------------------------------------
# What is built: the core, and one optional module per backend
# ---------------------------------------------------------
# The CORE is the ECS orchestrator: the Application and its fixed-timestep loop, the scene stack, the input layer,
# the ECS components and events, the render pass and the render system, and the interfaces every backend implements
# (application, input, render, physics). Its only API dependency is EnTT. It is always built.
#
# Each backend, and each optional service, is a MODULE: its own static library, its own include root, and its own
# third-party dependencies, fetched only when the module is on. Every module is ON by default, so a consumer that
# says nothing builds everything, as before the split; a consumer turns off what it does not use.
option(BRITE_BUILD_RAYLIB "Build the raylib application, input and render backends (and the raylib camera helpers)" ON)
option(BRITE_BUILD_BOX2D "Build the Box2D physics backend and the area-of-interest system" ON)
option(BRITE_BUILD_BOX3D "Build the Box3D physics backend" ON)
option(BRITE_BUILD_AUDIO "Build the audio module: SoLoud, and the sounds a scene keeps" ON)
option(BRITE_BUILD_PERSISTENCE "Build the persistence module: PhysicsFS, packed assets and a saved game state" ON)
option(BRITE_BUILD_DEBUG_UI "Fetch rlImGui and put it on the include path (a debug UI's raylib binding; needs raylib)" ON)
option(BRITE_ENABLE_PROFILER "Build the Tracy profiler into the core (started and stopped by the Application)" ON)

if(BRITE_BUILD_DEBUG_UI AND NOT BRITE_BUILD_RAYLIB)
    message(FATAL_ERROR "BRITE_BUILD_DEBUG_UI binds a debug UI to raylib: it needs BRITE_BUILD_RAYLIB")
endif()

# ---------------------------------------------------------
# Dependencies -- each fetched only by what needs it
# ---------------------------------------------------------

if(BRITE_BUILD_RAYLIB)
    # Raylib
    FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/master.tar.gz
    )
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    # Disable Raylib's internal audio module to prevent dr_wav symbol collisions with SoLoud
    set(CUSTOMIZE_BUILD ON CACHE BOOL "Allow customizing raylib" FORCE)
    set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "Disable raylib audio" FORCE)
    # JPEG textures on. raylib leaves JPEG out by default -- its config.h says so,
    # and with CUSTOMIZE_BUILD the options above start from those same defaults --
    # so a model whose file carried a JPEG texture drew without it, with only a
    # raylib log line to say why. PNG is raylib's default and stays on. HDR stays
    # off: nothing here needs it yet.
    set(SUPPORT_FILEFORMAT_JPG ON CACHE BOOL "Load JPEG images and textures" FORCE)

    FetchContent_MakeAvailable(raylib)
endif()

# EnTT -- the core's one API dependency.
FetchContent_Declare(
    entt
    URL https://github.com/skypjack/entt/archive/refs/tags/v3.13.1.tar.gz
)
FetchContent_MakeAvailable(entt)

if(BRITE_BUILD_BOX2D)
    # Box2D
    FetchContent_Declare(
        box2d
        URL https://github.com/erincatto/box2d/archive/refs/tags/v3.0.0.tar.gz
    )
    set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
    set(BOX2D_VALIDATION OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(box2d)
endif()

if(BRITE_BUILD_BOX3D)
    # Box3D
    FetchContent_Declare(
        box3d
        URL https://github.com/erincatto/box3d/archive/refs/heads/main.zip
    )
    set(BOX3D_SAMPLES OFF CACHE BOOL "" FORCE)
    set(BOX3D_VALIDATION OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(box3d)
endif()

if(BRITE_BUILD_RAYLIB OR BRITE_BUILD_PERSISTENCE)
    # nlohmann_json: the raylib module reads glTF materials with it, the persistence module writes the game state.
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if(BRITE_BUILD_PERSISTENCE)
    # PhysicsFS
    FetchContent_Declare(
        physfs
        URL https://github.com/icculus/physfs/archive/refs/heads/main.zip
    )
    set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(physfs)
endif()

if(BRITE_BUILD_DEBUG_UI)
    # rlImGui (Requires raylib and imgui)
    FetchContent_Declare(
        rlImGui
        URL https://github.com/raylib-extras/rlImGui/archive/refs/heads/main.zip
    )
    FetchContent_MakeAvailable(rlImGui)
endif()

# spdlog -- the core's log.
FetchContent_Declare(
    spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
)
FetchContent_MakeAvailable(spdlog)

if(BRITE_ENABLE_PROFILER)
    # Tracy Profiler
    FetchContent_Declare(
        tracy
        URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.10.tar.gz
    )
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    # The profiler's lifetime is the framework's: it starts and stops with the Application
    # (include/Core/Profiler.hpp), so its threads end inside main() rather than during static
    # destruction, where a runtime that skips thread_local destructors for threads ending in
    # exit() leaves Tracy's queue writing into a dead thread's storage.
    set(TRACY_DELAYED_INIT ON CACHE BOOL "" FORCE)
    set(TRACY_MANUAL_LIFETIME ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tracy)
endif()

if(BRITE_BUILD_AUDIO)
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
        ${soloud_SOURCE_DIR}/src/audiosource/wav/soloud_wav.cpp
        ${soloud_SOURCE_DIR}/src/audiosource/wav/soloud_wavstream.cpp
        ${soloud_SOURCE_DIR}/src/audiosource/wav/dr_impl.cpp
        ${soloud_SOURCE_DIR}/src/audiosource/wav/stb_vorbis.c
    )
    target_compile_definitions(soloud PRIVATE WITH_MINIAUDIO)
    target_include_directories(soloud PUBLIC ${soloud_SOURCE_DIR}/include)
endif()

# ---------------------------------------------------------
# CMake Macros
# ---------------------------------------------------------

macro(brite_setup_assets TARGET_NAME)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_custom_command(
            TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../BRITE-Engine/assets" "$<TARGET_FILE_DIR:${TARGET_NAME}>/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${TARGET_NAME}>/assets"
            COMMAND ${CMAKE_COMMAND} -E tar "cfv" "$<TARGET_FILE_DIR:${TARGET_NAME}>/assets.pak" --format=zip "$<TARGET_FILE_DIR:${TARGET_NAME}>/assets"
            COMMENT "Packaging engine and client assets into assets.pak..."
        )
    else()
        add_custom_command(
            TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../BRITE-Engine/assets" "$<TARGET_FILE_DIR:${TARGET_NAME}>/assets"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${TARGET_NAME}>/assets"
            COMMENT "Merging engine and client assets to build directory..."
        )
    endif()
endmacro()

# ---------------------------------------------------------
# The core
# ---------------------------------------------------------

add_library(BRITECore STATIC
    src/Core/InputManager.cpp
    src/Core/Profiler.cpp
    src/Systems/RenderSystem.cpp
    src/Systems/PhysicsSystem.cpp
    src/Framework/Application.cpp
    src/Framework/Scene.cpp
    src/Backends/MeshData.cpp
)
add_library(BRITE::Core ALIAS BRITECore)
target_include_directories(BRITECore PUBLIC include)
target_link_libraries(BRITECore PUBLIC EnTT::EnTT spdlog::spdlog)
if(BRITE_ENABLE_PROFILER)
    target_link_libraries(BRITECore PUBLIC TracyClient)
endif()
if(WIN32)
    target_link_libraries(BRITECore PUBLIC ws2_32 dbghelp)
endif()

# The math alone: header-only, no dependency at all, for code that wants BRITE's vectors and nothing else.
add_library(BRITEMath INTERFACE)
add_library(BRITE::Math ALIAS BRITEMath)
target_include_directories(BRITEMath INTERFACE include)

# ---------------------------------------------------------
# The modules
# ---------------------------------------------------------
# Every module links the core: it builds on the core's interfaces and log, and it takes the core's sanitizer flags
# (below) with them, so a module is instrumented exactly as the core is.

# Every module this configure builds, for the aggregate below.
set(BRITE_MODULES "")

if(BRITE_BUILD_RAYLIB)
    add_library(BRITERaylib STATIC
        modules/raylib/src/Backends/GltfMaterials.cpp
        modules/raylib/src/Backends/Raylib/RaylibApplicationBackend.cpp
        modules/raylib/src/Backends/Raylib/RaylibInputBackend.cpp
        modules/raylib/src/Backends/Raylib/RaylibRenderBackend.cpp
        modules/raylib/src/Backends/Raylib/RaylibSceneAssets.cpp
    )
    add_library(BRITE::Raylib ALIAS BRITERaylib)
    target_include_directories(BRITERaylib PUBLIC modules/raylib/include)
    # The PBR shader is compiled INTO the engine, so a client draws lit models
    # without staging any asset files. The GLSL stays in assets/shaders/ as the
    # editable source; it is read here at configure time into a generated header of
    # string literals, and listing the two files in CMAKE_CONFIGURE_DEPENDS makes a
    # build after editing them reconfigure on its own rather than link a stale copy.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/pbr.vs" BRITE_PBR_VS)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/pbr.fs" BRITE_PBR_FS)
    configure_file(modules/raylib/src/Backends/Raylib/PbrShaderSource.hpp.in
                   "${CMAKE_CURRENT_BINARY_DIR}/generated/Backends/Raylib/PbrShaderSource.hpp" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/pbr.vs"
                 "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/pbr.fs")
    target_include_directories(BRITERaylib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(BRITERaylib PUBLIC BRITECore raylib nlohmann_json::nlohmann_json)
    list(APPEND BRITE_MODULES BRITERaylib)
endif()

if(BRITE_BUILD_BOX2D)
    add_library(BRITEBox2D STATIC
        modules/box2d/src/Systems/Box2DBackend.cpp
        modules/box2d/src/Systems/AoISystem.cpp
    )
    add_library(BRITE::Box2D ALIAS BRITEBox2D)
    target_include_directories(BRITEBox2D PUBLIC modules/box2d/include)
    target_link_libraries(BRITEBox2D PUBLIC BRITECore box2d)
    list(APPEND BRITE_MODULES BRITEBox2D)
endif()

if(BRITE_BUILD_BOX3D)
    add_library(BRITEBox3D STATIC modules/box3d/src/Systems/Box3DBackend.cpp)
    add_library(BRITE::Box3D ALIAS BRITEBox3D)
    target_include_directories(BRITEBox3D PUBLIC modules/box3d/include)
    target_link_libraries(BRITEBox3D PUBLIC BRITECore box3d)
    list(APPEND BRITE_MODULES BRITEBox3D)
endif()

if(BRITE_BUILD_AUDIO)
    add_library(BRITEAudio STATIC modules/audio/src/Audio/SceneSounds.cpp)
    add_library(BRITE::Audio ALIAS BRITEAudio)
    target_include_directories(BRITEAudio PUBLIC modules/audio/include)
    target_link_libraries(BRITEAudio PUBLIC BRITECore soloud)
    list(APPEND BRITE_MODULES BRITEAudio)
endif()

if(BRITE_BUILD_PERSISTENCE)
    add_library(BRITEPersistence STATIC modules/persistence/src/Persistence/SaveStore.cpp)
    add_library(BRITE::Persistence ALIAS BRITEPersistence)
    target_include_directories(BRITEPersistence PUBLIC modules/persistence/include ${physfs_SOURCE_DIR}/src)
    target_link_libraries(BRITEPersistence PUBLIC BRITECore nlohmann_json::nlohmann_json physfs-static)
    list(APPEND BRITE_MODULES BRITEPersistence)
endif()

if(BRITE_BUILD_DEBUG_UI)
    # Today the module is rlImGui's sources on the include path and nothing more: ImGui itself is not fetched and
    # nothing is compiled, so a consumer cannot call it yet. It is kept so that a debug UI can be built on it.
    add_library(BRITEDebugUI INTERFACE)
    add_library(BRITE::DebugUI ALIAS BRITEDebugUI)
    target_include_directories(BRITEDebugUI INTERFACE ${rlimgui_SOURCE_DIR})
    target_link_libraries(BRITEDebugUI INTERFACE BRITERaylib)
    list(APPEND BRITE_MODULES BRITEDebugUI)
endif()

# ---------------------------------------------------------
# BRITEEngine: the core and every module this configure builds
# ---------------------------------------------------------
# What a consumer linked before the split, and still may: with every module on, it carries what the one engine
# library did.
add_library(BRITEEngine INTERFACE)
target_link_libraries(BRITEEngine INTERFACE BRITECore ${BRITE_MODULES})

# ---------------------------------------------------------
# Sanitizers (ASan + UBSan)
# ---------------------------------------------------------
# These MUST be usage requirements on the core target, not directory-level
# add_compile_options(). Directory properties are snapshotted by a target when it
# is created, so a bare add_compile_options() placed after add_library() misses
# the engine entirely, and never reaches clients at all (clients add_subdirectory()
# this file before declaring their own targets, and directory properties only
# flow parent -> child). PUBLIC on the core propagates to every module (each links
# the core), to BRITETests and to every client executable that links BRITEEngine or
# any module, so the whole program is instrumented consistently.
if(CMAKE_BUILD_TYPE MATCHES Debug)
    set(BRITE_SANITIZERS_DEFAULT ON)
else()
    set(BRITE_SANITIZERS_DEFAULT OFF)
endif()
option(BRITE_ENABLE_SANITIZERS "Build BRITE and its clients with AddressSanitizer and UBSan"
       ${BRITE_SANITIZERS_DEFAULT})

if(BRITE_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(BRITECore PUBLIC
            -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(BRITECore PUBLIC
            -fsanitize=address -fsanitize=undefined)
        message(STATUS "BRITE: ASan + UBSan enabled (engine, tests, and clients)")
        # Tracy's crash handler goes first in the vectored exception chain and, while a profiler
        # is connected, takes any access violation for a crash -- including the first-chance faults
        # AddressSanitizer raises on purpose to commit its shadow memory -- and ends the process.
        # With the sanitizers on, crashes are AddressSanitizer's to report.
        if(BRITE_ENABLE_PROFILER)
            target_compile_definitions(TracyClient PUBLIC TRACY_NO_CRASH_HANDLER)
        endif()
    else()
        message(WARNING "BRITE_ENABLE_SANITIZERS is ON but compiler '${CMAKE_CXX_COMPILER_ID}' is unsupported; ignoring")
    endif()
endif()

enable_testing()

# BRITE's own suite is built and registered with ctest when BRITE is the project being configured, and not when another
# project adds BRITE with add_subdirectory: that project's ctest is its own suite, and the engine's would otherwise
# run inside it -- and be linked by its every build -- whatever the project was testing. BRITE_BUILD_TESTS turns the
# suite on there too. Top level is read as CMAKE_SOURCE_DIR being this directory, which every CMake this file accepts
# can say; PROJECT_IS_TOP_LEVEL says the same from 3.21 on.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(BRITE_BUILD_TESTS_DEFAULT ON)
else()
    set(BRITE_BUILD_TESTS_DEFAULT OFF)
endif()
option(BRITE_BUILD_TESTS "Build BRITETests and register it with ctest (default: only when BRITE is the top-level project)"
       ${BRITE_BUILD_TESTS_DEFAULT})

if(BRITE_BUILD_TESTS)
    # GoogleTest: fetched for BRITE's own suite and nothing else. A project that adds BRITE and has tests of its own
    # declares GoogleTest itself; were it to declare the same before adding BRITE, FetchContent's first declaration
    # would be the one both use.
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # The core's cases always, and each module's only when the module is built.
    set(BRITE_TEST_SOURCES
        tests/InputManagerTests.cpp
        tests/TickFractionTests.cpp
        tests/ApplicationLifetimeTests.cpp
        tests/SceneStackTests.cpp
        tests/FixedTickHookTests.cpp
        tests/ScenePhysicsTests.cpp
        tests/PhysicsSystemTests.cpp
        tests/RenderSystemTests.cpp
        tests/ModuleBoundaryTests.cpp
    )
    if(BRITE_ENABLE_PROFILER)
        list(APPEND BRITE_TEST_SOURCES tests/ProfilerLifetimeTests.cpp)
    endif()
    if(BRITE_BUILD_RAYLIB)
        list(APPEND BRITE_TEST_SOURCES
            tests/MeshDataTests.cpp
            tests/ModelMaterialTests.cpp
            tests/ModelReadbackTests.cpp
            tests/MultisampleTests.cpp
            tests/GltfMaterialsTests.cpp
            tests/WindowModeTests.cpp
            tests/DepthWriteTests.cpp
            tests/RaylibSceneAssetsTests.cpp
        )
    endif()
    if(BRITE_BUILD_BOX2D)
        # The backend and the area of interest, each driven on a registry with no window or application.
        list(APPEND BRITE_TEST_SOURCES tests/Box2DBackendTests.cpp tests/AoISystemTests.cpp)
    endif()
    if(BRITE_BUILD_BOX3D)
        list(APPEND BRITE_TEST_SOURCES tests/Box3DBackendTests.cpp)
    endif()
    if(BRITE_BUILD_RAYLIB AND BRITE_BUILD_BOX2D)
        # Scenes on raylib's window, swapped as fast as they tick, each stepping a Box2D world.
        list(APPEND BRITE_TEST_SOURCES tests/SceneFuzzTest.cpp)
    endif()
    if(BRITE_BUILD_PERSISTENCE)
        list(APPEND BRITE_TEST_SOURCES tests/SaveStoreTests.cpp)
    endif()
    if(BRITE_BUILD_AUDIO)
        list(APPEND BRITE_TEST_SOURCES tests/SceneSoundsTests.cpp)
    endif()
    add_executable(BRITETests ${BRITE_TEST_SOURCES})
    target_link_libraries(BRITETests PRIVATE BRITEEngine gtest_main)
    # Which modules this configure builds, for ModuleBoundaryTests: each one's headers must be reachable exactly then.
    set_source_files_properties(tests/ModuleBoundaryTests.cpp PROPERTIES COMPILE_DEFINITIONS
        "BRITE_TEST_MODULE_RAYLIB=$<BOOL:${BRITE_BUILD_RAYLIB}>;BRITE_TEST_MODULE_BOX2D=$<BOOL:${BRITE_BUILD_BOX2D}>;BRITE_TEST_MODULE_BOX3D=$<BOOL:${BRITE_BUILD_BOX3D}>;BRITE_TEST_MODULE_AUDIO=$<BOOL:${BRITE_BUILD_AUDIO}>;BRITE_TEST_MODULE_PERSISTENCE=$<BOOL:${BRITE_BUILD_PERSISTENCE}>;BRITE_TEST_MODULE_DEBUG_UI=$<BOOL:${BRITE_BUILD_DEBUG_UI}>")
    if(WIN32)
        # ProfilerLifetimeTests finds its own profiler's port in the table of listening sockets.
        target_link_libraries(BRITETests PRIVATE iphlpapi)
    endif()

    include(GoogleTest)
    gtest_discover_tests(BRITETests)
endif()
