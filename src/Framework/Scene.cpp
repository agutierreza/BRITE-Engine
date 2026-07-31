#include "Framework/Scene.hpp"
#include <soloud_wav.h>
#include <soloud_wavstream.h>

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
    // Unload Raylib Assets
    for (auto& tex : m_trackedTextures) {
        UnloadTexture(tex);
    }
    for (auto& font : m_trackedFonts) {
        UnloadFont(font);
    }
    m_trackedTextures.clear();
    m_trackedFonts.clear();

    // SoLoud assets are cleaned up automatically via unique_ptr
    m_trackedWavs.clear();
    m_trackedWavStreams.clear();

    // Clean up ECS and Physics for this scene
    m_registry.clear();
    if (b2World_IsValid(m_worldId)) {
        b2DestroyWorld(m_worldId);
    }
}

Texture2D Scene::LoadSceneTexture(const std::string& path) {
    Texture2D tex = LoadTexture(path.c_str());
    m_trackedTextures.push_back(tex);
    return tex;
}

Font Scene::LoadSceneFont(const std::string& path) {
    Font font = LoadFont(path.c_str());
    m_trackedFonts.push_back(font);
    return font;
}

SoLoud::Wav* Scene::LoadSceneWav(const std::string& path) {
    auto wav = std::make_unique<SoLoud::Wav>();
    wav->load(path.c_str());
    SoLoud::Wav* ptr = wav.get();
    m_trackedWavs.push_back(std::move(wav));
    return ptr;
}

SoLoud::WavStream* Scene::LoadSceneWavStream(const std::string& path) {
    auto wav = std::make_unique<SoLoud::WavStream>();
    wav->load(path.c_str());
    SoLoud::WavStream* ptr = wav.get();
    m_trackedWavStreams.push_back(std::move(wav));
    return ptr;
}

} // namespace framework
} // namespace brite
