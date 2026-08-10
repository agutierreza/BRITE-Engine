#pragma once

#include "Backends/IRenderBackend.hpp"
#include <unordered_map>

namespace BRITE {
namespace Backends {
namespace Raylib {

class RaylibRenderBackend : public IRenderBackend {
  public:
    void SubmitRenderPass(const BRITE::RenderPass& pass) override;

    BRITE::TextureHandle LoadRenderTexture(int width, int height) override;
    void UnloadRenderTexture(BRITE::TextureHandle target) override;

    BRITE::TextureHandle LoadTexture(const char* fileName) override;
    void UnloadTexture(BRITE::TextureHandle texture) override;

  private:
    uint64_t m_nextId = 1;

    // To cleanly separate Texture2D vs RenderTexture2D, we store a boolean.
    // Real implementation would just use a wrapper struct allocated on heap.
    struct TextureData {
        bool isRenderTexture;
        void* ptr; // Points to either a Texture2D or a RenderTexture2D
    };
    std::unordered_map<BRITE::TextureHandle, TextureData> m_textures;
};

} // namespace Raylib
} // namespace Backends
} // namespace BRITE
