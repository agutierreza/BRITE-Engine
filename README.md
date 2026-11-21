# BRITE Engine

**BRITE** is a C++20 game-engine framework, built as a set of static libraries. Its core is an ECS orchestrator: an
[EnTT](https://github.com/skypjack/entt) registry as the one source of truth, a fixed-timestep loop, a stack of scenes,
and a data-driven render pass. Everything that touches an operating system or a third-party library -- the window, the
input, the drawing, the physics solver, the audio, the saved game -- sits behind an interface in an optional **module**,
so a project builds only what it uses. Today's modules are a raylib backend (window, input, rendering, glTF models and
PBR lighting), Box2D and Box3D physics, SoLoud audio, PhysicsFS persistence, a content/manifest layer and a debug-UI
binding.

The name is the stack it started on: **B**ox2D/Box3D, **R**aylib, **I**mGui and **I**nput, **T**racy and **T**weeny,
**E**nTT. It is an experimental engine, written to learn engine architecture by building it, and it is early: expect
interfaces to change.

This README says only what has been run. A statement that has not been tried says **unverified**.

---

## Install (Windows)

What a build was last verified on: **Windows 11, MSYS2 `clang64`** -- clang 22.1.8, CMake 4.4.0, Ninja 1.13.2 -- from a
cold clone, with nothing else installed (no vcpkg, no Visual Studio, no Python).

1. Install [MSYS2](https://www.msys2.org/) and open its **CLANG64** shell (not MSYS, not MINGW64).
2. In it, install the toolchain:

```bash
pacman -S mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja
```

3. **Network access to github.com at configure time.** CMake downloads the dependencies as archives from GitHub (11 of
   them with every module on; fewer with modules off -- see below). `git` is not needed to build, only to clone.
4. **A graphics driver with OpenGL 3.3** to open a window. Some of BRITE's tests open one too.

**Unverified:** GCC, MSVC and Visual Studio generators; Linux and macOS (the CMake links Windows system libraries under
`if(WIN32)` and nothing else, so a port may be small, but nobody has tried); CMake older than 4.4 (the file asks for
3.20).

## Build and test BRITE

```bash
git clone https://github.com/agutierreza/BRITE-Engine.git
cd BRITE-Engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

* **State the build type.** Left empty, nothing is optimised. **Debug turns on AddressSanitizer and UBSan** for the
  engine and everything linked to it (`BRITE_ENABLE_SANITIZERS`, on by default only for Debug): slower, but a memory error
  is named where it happens. Release switches them off.
* On the verified machine a cold configure took about 45 s (it downloads every dependency), the build about 50 s, and
  BRITE's own suite, about 200 cases, under a minute.
* `ctest` runs **BRITETests**, which is built only when BRITE is the top-level project. **Several tests open a window**
  (a hidden one, but it needs the OpenGL driver). Build every target before running ctest: one test reads a manifest the
  build writes.
* The same build compiles `examples/hello` (below) into `build/examples/hello/hello.exe`.

## Use BRITE from your own project

BRITE is consumed as a subdirectory: put the folder beside or inside your project (a clone, a git submodule) and
`add_subdirectory` it. There is no `install()` or `find_package`. `examples/hello` is a complete project of exactly
this shape and is built with every engine build, so what follows cannot drift from code that compiles.

`examples/hello/CMakeLists.txt`:

```cmake
# hello: a BRITE application, and the pattern for your own.
#
# This file is two things at once. Built from BRITE's own tree (cmake -S . -B build, the default when BRITE is the
# top-level project) it just adds the `hello` target. Used as a project of its own -- copy this folder anywhere, set
# BRITE_DIR to where BRITE-Engine is -- it adds BRITE as a subdirectory first, which is how your project uses BRITE.
cmake_minimum_required(VERSION 3.20)
project(Hello CXX)

if(NOT TARGET BRITE::Raylib)
    # Where BRITE-Engine is: a clone beside your project, a git submodule, wherever. The default is this tree.
    set(BRITE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "The BRITE-Engine folder")

    # BRITE builds everything by default and downloads what it builds. A window needs only the raylib module:
    # switch off the rest, which also skips their downloads.
    set(BRITE_BUILD_BOX2D OFF)
    set(BRITE_BUILD_BOX3D OFF)
    set(BRITE_BUILD_AUDIO OFF)
    set(BRITE_BUILD_PERSISTENCE OFF)
    set(BRITE_BUILD_CONTENT OFF)
    set(BRITE_BUILD_DEBUG_UI OFF)
    # The profiler listens on the network, and Windows asks before letting a new program do that.
    set(BRITE_ENABLE_PROFILER OFF)

    add_subdirectory("${BRITE_DIR}" brite)
endif()

add_executable(hello main.cpp)
target_link_libraries(hello PRIVATE BRITE::Raylib)
```

To start your own project: copy `examples/hello` out of the BRITE folder and give it BRITE's location,

```bash
cmake -S hello -B hello/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBRITE_DIR=C:/path/to/BRITE-Engine
cmake --build hello/build
hello/build/hello.exe
```

(That is the command line this README's author ran from an empty folder. A configure downloads the dependencies into
your project's build folder again; set `FETCHCONTENT_BASE_DIR` to share one download between projects.)

The things to know:

* **Link `BRITE::Raylib`** for a window; it brings the core (`BRITE::Core`) with it. `BRITEEngine` links the core and
  every module you left on. Each module has a target of the same shape (`BRITE::Box2D`, `BRITE::Audio`, ...).
* **Turn off the modules you do not use**, as above: each `BRITE_BUILD_*` option that is off is a library not built and
  archives not downloaded. They default to ON.
* Set the options **before** `add_subdirectory`.
* When you add BRITE this way it builds neither its own tests nor `examples/hello`.

`examples/hello/main.cpp`, the application:

```cpp
// hello: the smallest BRITE application that draws something that moves.
//
// A window, one scene, one entity with a 3D cube, and a camera. Everything here is the engine's real API: the README
// quotes this file, and it is built with every engine build, so it cannot go stale.
//
//   hello            runs until the window is closed
//   hello --frames N runs N fixed ticks (60 a second) and exits, for a script to start it and see it end

#include <Backends/Raylib/RaylibApplicationBackend.hpp>
#include <Backends/Raylib/RaylibInputBackend.hpp>
#include <Backends/Raylib/RaylibRenderBackend.hpp>
#include <ECS/Components.hpp>
#include <Framework/Application.hpp>
#include <Framework/Scene.hpp>
#include <Systems/RenderSystem.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace brite::framework;

class HelloScene : public Scene {
  public:
    HelloScene(Application* app, int ticksToRun) : Scene(app), m_ticksToRun(ticksToRun) {}

    // Spawn the entities and aim the camera. Called once, when the scene is pushed.
    void OnStart() override {
        m_cube = m_registry.create();
        m_registry.emplace<BRITE::TransformComponent>(m_cube);
        auto& cube = m_registry.emplace<BRITE::Primitive3DComponent>(m_cube);
        cube.Type = BRITE::Primitive3DType::Cube;
        cube.Size = {2.0f, 2.0f, 2.0f};
        cube.Tint = BRITE::Color{230, 90, 40, 255};

        m_camera.position = {6.0f, 5.0f, 6.0f};
        m_camera.target = {0.0f, 0.0f, 0.0f};
        m_camera.up = {0.0f, 1.0f, 0.0f};
        m_camera.fovy = 45.0f;
        m_camera.projection = BRITE::CameraProjection::Perspective;
    }

    // Game logic, 60 times a second whatever the frame rate. `dt` is the fixed step, 1/60 s.
    void OnLogicStep(double dt) override {
        m_seconds += dt;
        auto& transform = m_registry.get<BRITE::TransformComponent>(m_cube);
        const float halfTurn = static_cast<float>(m_seconds) * 0.5f;
        transform.Rotation = {0.0f, std::sin(halfTurn), 0.0f, std::cos(halfTurn)}; // a quaternion: spin about +y
        transform.Position.x = 2.0f * std::sin(static_cast<float>(m_seconds));

        if (m_ticksToRun > 0 && ++m_ticksRun >= m_ticksToRun)
            m_app->Quit();
    }

    // Describe what to draw. RenderSystem turns every entity with a TransformComponent and a
    // Primitive3DComponent (or a SpriteComponent) into draw commands in `pass`; the backend draws the pass.
    void OnRender(BRITE::RenderPass& pass) override {
        BRITE::RenderSystem::Update(m_registry, pass, nullptr, &m_camera);
    }

  private:
    entt::entity m_cube{entt::null};
    BRITE::Camera3D m_camera{};
    double m_seconds = 0.0;
    int m_ticksToRun;
    int m_ticksRun = 0;
};

class HelloApp : public Application {
  public:
    HelloApp(int ticksToRun)
        : Application(std::make_unique<BRITE::Backends::Raylib::RaylibApplicationBackend>(),
                      std::make_unique<BRITE::Backends::Raylib::RaylibInputBackend>(),
                      std::make_unique<BRITE::Backends::Raylib::RaylibRenderBackend>(), "Hello BRITE", "hello", "hello",
                      800, 600),
          m_ticksToRun(ticksToRun) {}

  protected:
    void OnStart() override {
        PushScene(std::make_shared<HelloScene>(this, m_ticksToRun));
    }

  private:
    int m_ticksToRun;
};

int main(int argc, char** argv) {
    int ticksToRun = 0; // 0: until the window is closed
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0)
            ticksToRun = std::atoi(argv[i + 1]);
    }
    HelloApp app(ticksToRun);
    app.Run();
    return 0;
}
```

The three pieces to construct are the backends -- `RaylibApplicationBackend` (window and clock), `RaylibInputBackend`,
`RaylibRenderBackend` -- passed to `Application`. Swapping a backend means passing a different class that implements
`IApplicationBackend`, `IInputBackend` or `IRenderBackend`.

## The modules and their options

All are ON by default. A module's dependencies are fetched only when it is on.

| Option | Library target | What it is | What it fetches |
|---|---|---|---|
| *(always)* | `BRITE::Core` | the application, scenes, input layer, ECS components, render pass, the interfaces | EnTT v3.13.1, spdlog v1.17.0 |
| `BRITE_BUILD_RAYLIB` | `BRITE::Raylib` | window, input, rendering, glTF models, PBR shader | raylib (the `master` archive), nlohmann_json v3.11.3 |
| `BRITE_BUILD_BOX2D` | `BRITE::Box2D` | the 2D physics backend and the area-of-interest system | Box2D v3.0.0 |
| `BRITE_BUILD_BOX3D` | `BRITE::Box3D` | the 3D physics backend | Box3D (the `main` archive) |
| `BRITE_BUILD_AUDIO` | `BRITE::Audio` | the sounds a scene keeps | SoLoud RELEASE_20200207 |
| `BRITE_BUILD_PERSISTENCE` | `BRITE::Persistence` | packed assets and a saved game state | PhysicsFS (one pinned commit), nlohmann_json |
| `BRITE_BUILD_CONTENT` | `BRITE::Content` | shipped files read through one root, and a manifest | nlohmann_json |
| `BRITE_BUILD_DEBUG_UI` | `BRITE::DebugUI` | rlImGui on the include path (nothing calls it yet; needs raylib) | rlImGui (the `main` archive) |
| `BRITE_ENABLE_PROFILER` | *(in the core)* | the Tracy profiler (below) | Tracy v0.10 |
| `BRITE_ENABLE_SANITIZERS` | | ASan and UBSan; default ON for Debug builds only | |
| `BRITE_BUILD_TESTS` | `BRITETests` | BRITE's own suite; default ON only at the top level | GoogleTest v1.14.0 |
| `BRITE_BUILD_EXAMPLES` | `hello` | `examples/hello`; default ON only at the top level; needs raylib | |

raylib, Box3D and rlImGui are fetched from a moving branch, and not pinned: a clean configure next month can build
different code than a build today.

## The frame

`Application::Run` runs a fixed-timestep loop, 60 ticks a second by default (`SetFixedTimeStep`), with rendering
decoupled from it (target 144 frames a second; `SetTargetFPS`). Time carried over from one frame to the next accumulates;
each whole tick runs the scenes, and a frame that finishes no tick draws the last state again.

Each tick walks the scene stack top down until a scene whose `BlocksUpdate()` is true, and runs these on each. This is the
real order, from `Application.cpp`:

1. `Scene::OnInstantiation()` -- spawn and destroy entities;
2. `PhysicsSystem::PreStep` -- bodies, shapes and joints are created from the components (a scene has no physics until it
   is given a backend with `GetPhysicsSystem().Init(registry, backend)`);
3. `Scene::OnLogicStep(dt)` -- gameplay and input;
4. `PhysicsSystem::Step` -- the solver steps, and the result is copied back into `TransformComponent`;
5. `Scene::OnRenderPrepStep(dt)` -- camera work, interpolation.

Before the five, once per tick, `Application::OnFixedTick(dt)` runs. Rendering follows, bottom up through the scenes that
are visible (`BlocksRender()`): each scene's `OnRender(BRITE::RenderPass&)`. Changing the scene stack (`PushScene`,
`PopScene`, `ChangeScene`) is deferred to the start of the next frame, so a scene may call them from its own hooks.

The hooks of `Scene`, from `include/Framework/Scene.hpp`:

```cpp
virtual void OnStart() {}
virtual void OnInstantiation() {}
virtual void OnLogicStep(double dt) {}
virtual void OnRenderPrepStep(double dt) {}
virtual void OnRender(BRITE::RenderPass& pass) {}
virtual void OnShutdown() {}
```

**What gets drawn.** `OnRender` fills a `RenderPass` (`include/Backends/RenderPass.hpp`), and the render backend draws
that and nothing else. `RenderSystem::Update(registry, pass, camera2d, camera3d)` fills it from the registry: an entity
with a `TransformComponent` and a `SpriteComponent` is a textured 2D sprite, and one with a `TransformComponent` and a
`Primitive3DComponent` is a 3D primitive (cube, sphere, wires, grid) -- which needs the `Camera3D` you pass. **A
physics collider (`RigidBodyComponent`, `BoxColliderComponent`) draws nothing**: an entity with only those is invisible.
Models, lights, lines and rectangles are added to the pass directly (`ModelCommands`, `Lights`, `Line3DCommands`, ...).

**The order a pass is drawn in** is stated on `RenderPass` and does not depend on the order of its lists: the opaque 3D
draws first (models as they are drawn, then the batched primitives and lines; at exactly equal depth the later one wins,
so a line beats a primitive beats a model); then the translucent 3D draws -- a tint or a material with alpha, a glTF
`BLEND` material -- depth-tested and writing no depth, in two groups: *layers* (a model draw with `DepthWrite` off, in the
order submitted), then the rest, farthest first; then the 2D draws. Order-independent transparency is not done: one sort
key per draw (per mesh, for a model).

**`TickFraction()`.** A frame that falls between two ticks draws the same state as the last, so what moves at one tick a
step moves in steps. `Application::TickFraction()` is how far the clock has advanced into the next tick, in `[0, 1)`,
valid during `OnRender`; keep the previous tick's state and draw `previous + (current - previous) * TickFraction()` to
move on every frame. `hello` does not use it: its cube moves at the tick rate.

## The profiler, and the firewall prompt

**The Tracy profiler is on by default** (`BRITE_ENABLE_PROFILER`), the `Application` starts it, and **it listens on the
network**. On Windows the first run of any program built with it raises a firewall prompt ("allow public and private
networks"), and every program built from a clone of BRITE gets its own. If you do not mean to profile, turn it off:

```
-DBRITE_ENABLE_PROFILER=OFF
```

(`examples/hello` does, as above.) It is on by default for the projects that already depend on it, so the default is the
engine owner's decision and has not been changed here.

## Licence

BRITE Engine is released under the MIT License. (A `LICENSE` file is not in the repository yet.)
