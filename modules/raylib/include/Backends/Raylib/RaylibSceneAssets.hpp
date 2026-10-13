#pragma once

#include <raylib.h>
#include <string>
#include <vector>

namespace BRITE::Backends::Raylib {

/// Textures and fonts a scene loads straight through raylib and keeps for as long as it lives: each unloaded with
/// this. Part of the raylib module; a scene that draws through raylib directly holds one as a member. Needs the
/// raylib window, so it is made after the Application and destroyed before it.
class RaylibSceneAssets {
  public:
    RaylibSceneAssets() = default;
    ~RaylibSceneAssets();

    RaylibSceneAssets(const RaylibSceneAssets&) = delete;
    RaylibSceneAssets& operator=(const RaylibSceneAssets&) = delete;

    /// raylib's LoadTexture, kept: id 0 if the file could not be read (raylib's log says why).
    Texture2D LoadTexture(const std::string& path);
    /// raylib's LoadFont, kept: raylib's default font if the file could not be read.
    Font LoadFont(const std::string& path);

  private:
    std::vector<Texture2D> m_textures;
    std::vector<Font> m_fonts;
};

} // namespace BRITE::Backends::Raylib
