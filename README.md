# BRITE Engine

**BRITE** is a lightweight, modern C++20 data-driven game engine framework built as a static library.

## Overview & Acronym Breakdown

The name **BRITE** is an acronym representing the core tech stack initially integrated into the library:
* **B** — **B**ox2D / Box3D (Physics Engines)
* **R** — **R**aylib (Rendering & Math)
* **I** — **I**mGui / **I**nput (Debug UI & Input)
* **T** — **T**racy / **T**weeny (Profiling & Animation)
* **E** — **E**nT T (Entity Component System)

This engine serves as an experimental sandbox for learning game engine architecture from first principles. Development started the natural way when learning from scratch: focusing on direct implementation first, and then refactoring towards abstraction. 

While the engine is still in an early stage and currently very basic, the overarching goal is for BRITE to serve as a **Backend-Agnostic ECS Orchestrator**. The true core of the engine is the "E" (EnTT), with all other systems (Rendering, Physics, Input) decoupled behind polymorphic interfaces.

*Note: Even though the core architecture is heavily abstracted, the BRITE static library still builds and packs Raylib and Box2D/Box3D out-of-the-box, providing these robust backend implementations ready to use.*

---

## Core Architecture: Data-Driven & Agnostic

At its heart, BRITE treats the `entt::registry` as the absolute source of truth. There is no hidden state—everything is a pure data structure (Component) attached to an Entity ID.

To maintain purity, BRITE defines strict interface contracts for its subsystems:

*   **`IApplicationBackend`**: Manages the application lifecycle, window creation, and the deterministic fixed-timestep game loop.
*   **`IRenderBackend`**: A contract for drawing textures and managing cameras using generic BRITE math types (`BRITE::Vector2`, `BRITE::Color`). (Currently very basic, supporting minimal sprite batching and render targets).
*   **`IPhysicsBackend`**: A contract for running physics simulations (`Init`, `PreStep`, `Step`). This allows swapping seamlessly between different solvers (e.g., `Box2DBackend` for 2D, or `Box3DBackend` for 3D physics).
*   **`IInputBackend`**: Abstracts OS-level keyboard and mouse events, mapping them into logical engine-wide inputs.

### Orchestrating Systems

The engine's "Systems" act as translators between the EnTT registry and the active Backends:
*   **`PhysicsSystem`**: Commands the active `IPhysicsBackend` to step forward in time, synchronizing physics simulation results back into the ECS's `TransformComponent`.
*   **`RenderSystem`**: Iterates over entities with `TransformComponent` and `SpriteComponent` data, pushing generic draw commands to the active `IRenderBackend`.

---

## Game Loop & Scene Management

### Application & Fixed Timestep Loop (`brite::framework::Application`)
* Implements a deterministic **Fixed Timestep Game Loop** (defaulting to 60 Hz physics updates) with variable rendering framerates (up to 144 FPS target).
* Loop execution pipeline:
  `Scene Transition -> PreStep -> PhysicsSystem::PreStep -> Backend Step -> PhysicsSystem::PostStep -> PostStep -> Render`

### Scene Lifecycle (`brite::framework::Scene`)
* Encapsulates an `entt::registry` for entity management.
* Provides overridable lifecycle hooks:
  * `OnStart()`: Resource allocation and entity spawning.
  * `OnPreStep(double dt)`: Pre-physics game logic.
  * `OnPostStep(double dt)`: Post-physics logic.
  * `OnRender()`: Rendering hooks.
  * `OnShutdown()`: Scene cleanup.

---

## Building & Integration

### Requirements
* **CMake**: Version 3.20 or higher.
* **C++ Compiler**: C++20 compliant compiler (GCC, Clang, or MSVC).

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
#include <Systems/PhysicsSystem.hpp>

class MainScene : public brite::framework::Scene {
public:
    MainScene(brite::framework::Application* app) : Scene(app) {}

    void OnStart() override {
        // Create an entity in the registry
        auto entity = m_registry.create();
        
        m_registry.emplace<BRITE::TransformComponent>(entity, BRITE::Vector2{ 400.0f, 100.0f });
        
        auto& rb = m_registry.emplace<BRITE::RigidBodyComponent>(entity);
        rb.Type = BRITE::RigidBodyComponent::BodyType::Dynamic;
        
        auto& box = m_registry.emplace<BRITE::BoxColliderComponent>(entity);
        box.Size = { 40.0f, 40.0f };
    }

    void OnRender() override {
        // RenderSystem orchestrates the active IRenderBackend
        BRITE::RenderSystem::Update(m_registry);
    }
};
```

---

## License

BRITE Engine is released under the MIT License.
