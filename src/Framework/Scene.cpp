#include "Framework/Scene.hpp"
#include <soloud_wav.h>
#include <soloud_wavstream.h>

namespace brite {
namespace framework {

Scene::Scene(Application* app) : m_app(app) {
    m_physicsSystem.Init(m_registry);
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
