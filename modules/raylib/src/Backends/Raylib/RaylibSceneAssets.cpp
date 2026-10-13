#include <Backends/Raylib/RaylibSceneAssets.hpp>

namespace BRITE::Backends::Raylib {

RaylibSceneAssets::~RaylibSceneAssets() {
    for (auto& texture : m_textures) {
        ::UnloadTexture(texture);
    }
    for (auto& font : m_fonts) {
        ::UnloadFont(font);
    }
}

Texture2D RaylibSceneAssets::LoadTexture(const std::string& path) {
    Texture2D texture = ::LoadTexture(path.c_str());
    m_textures.push_back(texture);
    return texture;
}

Font RaylibSceneAssets::LoadFont(const std::string& path) {
    Font font = ::LoadFont(path.c_str());
    m_fonts.push_back(font);
    return font;
}

} // namespace BRITE::Backends::Raylib
