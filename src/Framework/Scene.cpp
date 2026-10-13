#include "Framework/Scene.hpp"

namespace brite {
namespace framework {

// A scene has no physics of its own: one that wants it gives its PhysicsSystem a backend.
Scene::Scene(Application* app) : m_app(app) {}

Scene::~Scene() {
    // Clean up ECS and Physics for this scene: the registry is cleared while the physics backend, if any, still lives.
    m_registry.clear();
}

} // namespace framework
} // namespace brite
