# BRITE Engine

**BRITE** is a lightweight, modern 2D C++20 game engine framework built as a static library.

## Overview & Acronym Breakdown

The name **BRITE** is an acronym representing the core tech stack integrated into the library:

* **B** — **B**ox2D (2D Rigid Body Physics Engine)
* **R** — **R**aylib (2D Rendering, Windowing, & Maths Utilities)
* **I** — **I**mGui / **I**nput Management (rlImGui Debug UI & Input Abstraction)
* **T** — **T**racy Profiler / **T**weeny (Real-time Instrumentation & Animation)
* **E** — **E**nT T (**E**ntity Component System / Engine Architecture)

### Origin & Purpose

I initially named this project **Sprite-Box**, where **SPRITE** was an acronym formed by the engine's core middleware stack (**S**oLoud, **P**hysicsFS, **R**aylib, **I**mGui, **T**weeny/Tracy, **E**nT T) paired with **Box**2D physics! 

However, I soon realised that *SpriteBox* was already an existing coding framework designed for teaching children how to code. To avoid confusion, and a bit disappointed, I rebranded the project to **BRITE**.

This engine is primarily a personal sandbox for me to learn video game development and game engine architecture from first principles. The main purpose of this packed static library is to serve as an experimental workbench where I can explore ECS patterns, integrate core C++ middleware, and build my own games.

While it is mainly built for my own personal learning and projects, I am leaving the repository public in case anyone else finds it useful or interesting!

---

## Core Third-Party Frameworks & Dependencies

The BRITE core static library (`BRITEEngine`) integrates several industry-standard C++ libraries:

* **[Raylib](https://www.raylib.com/)** (v4.5 / master): High-performance, lightweight 2D graphics rendering, window management, input polling, texture handling, and 2D camera utilities (`Camera2D`).
* **[EnTT](https://github.com/skypjack/entt)** (v3.13.1): Fast, header-only Entity Component System (ECS) framework powering game state and object management (`entt::registry`).
* **[Box2D](https://box2d.org/)** (v3.0.0): Redesigned C-based 2D rigid body physics simulation engine (`b2WorldId`, `b2BodyId`, `b2ShapeId`).
* **[SoLoud](https://soloud-doc.mue.org/)**: Multi-channel audio engine built with a Miniaudio backend, handling sound effects, background music, audio buses, and 3D sound positioning (`SoLoud::Soloud`).
* **[PhysicsFS (PhysFS)](https://icculus.org/physfs/)**: Portable virtual file system (VFS) abstraction layer allowing seamless loading of loose assets or packed archives (`assets.pak`) by overriding Raylib file callbacks.
* **[spdlog](https://github.com/gabime/spdlog)** (v1.13.0): Fast, header-only C++ logging library for structured console and file logging.
* **[Tracy Profiler](https://github.com/wolfpld/tracy)** (v0.10): Real-time nanosecond-level frame profiling (`ZoneScoped` instrumentation).
* **[nlohmann_json](https://github.com/nlohmann/json)** (v3.11.3): Intuitive header-only JSON serialisation and configuration library.
* **[rlImGui](https://github.com/raylib-extras/rlImGui)**: Integration bridge linking Raylib with Dear ImGui for immediate-mode debug UI tooling.
* **[Tweeny](https://github.com/mobiust/tweeny)**: Header-only C++ animation and parameter interpolation library.

---

## Lightweight Engine Framework

BRITE provides a high-level application framework that abstracts game loop boilerplate, scene lifecycle, input management, and camera behaviours.

### 1. Application & Fixed Timestep Loop (`brite::framework::Application`)
* Manages subsystem initialisation and clean shutdown (spdlog, PhysFS archive mounting, Raylib window, and SoLoud audio).
* Implements a deterministic **Fixed Timestep Game Loop** (defaulting to 60 Hz physics updates) with variable rendering framerates (up to 144 FPS target) and spiral-of-death time accumulation clamping.
* Loop execution pipeline per frame:
  $$\text{Scene Transition} \rightarrow \text{OnPreStep} \rightarrow \text{PhysicsSystem::PreStep} \rightarrow \text{b2World\_Step} \rightarrow \text{PhysicsSystem::PostStep} \rightarrow \text{OnPostStep} \rightarrow \text{OnRender}$$

### 2. Scene Lifecycle Management (`brite::framework::Scene`)
* Encapsulates an `entt::registry` for entity management and a dedicated `b2WorldId` Box2D physics world instance (default gravity: $y = -10.0\text{ m/s}^2$).
* Provides overridable lifecycle hooks:
  * `OnStart()`: Resource allocation and entity spawning.
  * `OnPreStep(double dt)`: Pre-physics game logic and input handling.
  * `OnPostStep(double dt)`: Post-physics state synchronisation.
  * `OnRender()`: Sprite drawing, debug visualisations, and UI rendering.
  * `OnShutdown()`: Scene cleanup.

### 3. Input Abstraction (`BRITE::InputManager`)
* Static input query wrapper over Raylib keyboard and mouse state.
* Standardised enums (`BRITE::KeyCode`, `BRITE::MouseButtonCode`) mapping to engine key states (`IsKeyPressed`, `IsKeyDown`, `IsKeyReleased`, `GetMouseX`, `GetMouseY`).

### 4. Modular Camera System (`brite::framework::CameraSystem`)
* 2D camera control logic interfacing directly with Raylib's `Camera2D` and `CameraFollowComponent`.
* Supports multiple target follow modes:
  * **HardLock**: Immediate snapping to target position.
  * **SmoothLerp**: Frame-rate independent linear interpolation (`lerpSpeed`).
  * **Deadzone**: Rectangular boundary threshold delaying camera movements until target exits deadzone bounds.
  * **LeadVelocity**: Dynamic target leading based on entity movement velocity vectors (`leadDistance`, `leadSpeed`).

---

## High-Level Box2D ECS Wrappers

BRITE bridges EnTT entities with Box2D v3 physics through automated component binding and automatic coordinate conversion.

### Units & Scaling
* **Pixels-Per-Metre Constant**: `PPM = 30.0f` ($1\text{ metre} = 30\text{ pixels}$).
* Position and size values in ECS components are specified in pixels and degrees, while Box2D physics calculations operate in metres and radians.

### ECS Components (`BRITE` namespace)
* `TransformComponent`: Positional coordinates (`Vector2 Position`), angle (`float Rotation` in degrees), and scale (`Vector2 Scale`).
* `SpriteComponent`: Renderable texture data (`Texture2D`), source/destination bounds, rotation origin, and tint colour (`Color`).
* `RigidBodyComponent`: Body classification (`Static`, `Kinematic`, `Dynamic`), `FixedRotation`, `GravityScale`, and internal Box2D handle (`b2BodyId RuntimeBody`).
* `BoxColliderComponent`: Half-extent dimensions (`Vector2 Size` in pixels), offset vector, material properties (`Density`, `Friction`, `Restitution`), and shape handle (`b2ShapeId RuntimeShape`).
* `CircleColliderComponent`: Radius in pixels (`float Radius`), offset vector, material properties, and shape handle (`b2ShapeId RuntimeShape`).

### Physics System Integration (`BRITE::PhysicsSystem`)
* **`PhysicsSystem::PreStep`**:
  * Lazily constructs Box2D bodies (`b2CreateBody`) and shapes (`b2CreatePolygonShape`, `b2CreateCircleShape`) when new `RigidBodyComponent` entities are registered.
  * Attaches entity handles to `b2BodyDef::userData` for contact listener callbacks.
  * Synchronises kinematic bodies from ECS transforms prior to physics steps.
* **`PhysicsSystem::PostStep`**:
  * Retrieves updated positions and rotation angles from Box2D simulation.
  * Converts Box2D positions (metres) back to pixel coordinates ($x_{\text{px}} = x_{\text{m}} \times \text{PPM}$) and updates `TransformComponent`.

---

## Building & Integration

### Requirements
* **CMake**: Version 3.20 or higher.
* **C++ Compiler**: C++20 compliant compiler (GCC, Clang, or MSVC).
* **Dependencies**: All third-party dependencies are fetched and built automatically via CMake `FetchContent`.

### Build Instructions
```bash
git clone https://github.com/YourOrg/BRITE-Engine.git
cd BRITE-Engine
mkdir build && cd build
cmake ..
cmake --build .
```

---

## Example Usage

```cpp
#include <Framework/Application.hpp>
#include <Framework/Scene.hpp>
#include <ECS/Components.hpp>
#include <Systems/RenderSystem.hpp>

class MainScene : public brite::framework::Scene {
public:
    MainScene(brite::framework::Application* app) : Scene(app) {}

    void OnStart() override {
        // Create a physics-driven entity
        auto entity = m_registry.create();
        
        m_registry.emplace<BRITE::TransformComponent>(entity, Vector2{ 400.0f, 100.0f });
        
        auto& rb = m_registry.emplace<BRITE::RigidBodyComponent>(entity);
        rb.Type = BRITE::RigidBodyComponent::BodyType::Dynamic;
        
        auto& box = m_registry.emplace<BRITE::BoxColliderComponent>(entity);
        box.Size = { 40.0f, 40.0f };
    }

    void OnRender() override {
        BRITE::RenderSystem::Update(m_registry);
    }
};

class GameApp : public brite::framework::Application {
public:
    GameApp() : Application("BRITE Demo", 1280, 720) {}

protected:
    void OnStart() override {
        ChangeScene(std::make_shared<MainScene>(this));
    }
};

int main() {
    GameApp app;
    app.Run();
    return 0;
}
```

---

## License

BRITE Engine is released under the MIT License.
