#include "Framework/Scene.hpp"

namespace brite {
namespace framework {

Scene::Scene(Application* app) : m_app(app) {
    // Initialize Box2D v3 world with default gravity
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity.x = 0.0f;
    worldDef.gravity.y = -10.0f;
    m_worldId = b2CreateWorld(&worldDef);
}

Scene::~Scene() {
    // Clean up ECS and Physics for this scene
    m_registry.clear();
    if (b2World_IsValid(m_worldId)) {
        b2DestroyWorld(m_worldId);
    }
}

} // namespace framework
} // namespace brite
