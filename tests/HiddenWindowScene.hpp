#pragma once

// A scene of flat quads under an orthographic camera, drawn on the GPU under a hidden window (never shown, never
// focused) and read back -- for the render cases that need a GL context. The camera at (0, 0, 5) looks down -z at the
// origin with +y up, and the view is 4 world units across, so on a SIZE x SIZE target world -2..2 maps onto SIZE
// pixels: column (x + 2) * SIZE / 4, row (2 - y) * SIZE / 4 from the top. Nearer the camera is larger z.

#include <Backends/MeshData.hpp>
#include <Backends/Raylib/RaylibRenderBackend.hpp>
#include <Backends/RenderPass.hpp>
#include <gtest/gtest.h>

#include <raylib.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

namespace HiddenWindow {

inline constexpr int SIZE = 64;

/// The pixel index under world (x, y) on a SIZE x SIZE picture, top row first.
inline std::size_t At(float x, float y) {
    const int column = static_cast<int>((x + 2.0f) * (SIZE / 4.0f));
    const int row = static_cast<int>((2.0f - y) * (SIZE / 4.0f));
    return static_cast<std::size_t>(row * SIZE + column);
}

/// The same pixel in a picture read BOTTOM row first, four floats a pixel (ReadRenderTargetLinear, ReadLastHdrScene).
inline std::size_t AtLinear(float x, float y) {
    const int column = static_cast<int>((x + 2.0f) * (SIZE / 4.0f));
    const int row = SIZE - 1 - static_cast<int>((2.0f - y) * (SIZE / 4.0f));
    return static_cast<std::size_t>(row * SIZE + column) * 4;
}

inline bool Within(BRITE::Color c, int r, int g, int b, int tolerance) {
    return std::abs(c.r - r) <= tolerance && std::abs(c.g - g) <= tolerance && std::abs(c.b - b) <= tolerance;
}

inline BRITE::Camera3D Camera() {
    return {{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 4.0f, BRITE::CameraProjection::Orthographic};
}

class Scene : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        ::SetTraceLogLevel(LOG_WARNING);
        ::SetConfigFlags(FLAG_WINDOW_HIDDEN);
        ::InitWindow(SIZE, SIZE, "BRITE hidden render tests");
    }
    static void TearDownTestSuite() {
        ::CloseWindow();
    }

    void TearDown() override {
        for (const BRITE::ModelHandle model : m_models)
            m_backend.UnloadModel(model);
    }

    /// A flat rectangle from (x0, y0) to (x1, y1) at depth z, facing the camera; its normals `normal` unless the
    /// four corners' normals are given (x0y0, x1y0, x1y1, x0y1).
    BRITE::ModelHandle Quad(float x0, float y0, float x1, float y1, float z, std::vector<BRITE::Vector3> normals = {}) {
        BRITE::MeshData mesh;
        mesh.Positions = {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}};
        if (normals.size() == 4)
            mesh.Normals = normals;
        else
            mesh.Normals.assign(4, {0.0f, 0.0f, 1.0f});
        mesh.Indices = {0, 1, 2, 0, 2, 3};
        const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
        EXPECT_NE(model, BRITE::NullModelHandle);
        m_models.push_back(model);
        return model;
    }

    /// A box of half-size h centred at (x, y, z), each face wound both ways so it shows from either side, its normals
    /// pointing out.
    BRITE::ModelHandle Box(float x, float y, float z, float h) {
        BRITE::MeshData mesh;
        const BRITE::Vector3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const BRITE::Vector3& N : n) {
            const BRITE::Vector3 u = std::fabs(N.y) > 0.5f ? BRITE::Vector3{1, 0, 0} : BRITE::Vector3{0, 1, 0};
            const BRITE::Vector3 v{N.y * u.z - N.z * u.y, N.z * u.x - N.x * u.z, N.x * u.y - N.y * u.x};
            const std::uint32_t base = static_cast<std::uint32_t>(mesh.Positions.size());
            for (auto [a, b] : {std::pair{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}}) {
                mesh.Positions.push_back({x + (N.x + a * u.x + b * v.x) * h, y + (N.y + a * u.y + b * v.y) * h,
                                          z + (N.z + a * u.z + b * v.z) * h});
                mesh.Normals.push_back(N);
            }
            mesh.Indices.insert(mesh.Indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3, base, base + 2,
                                                     base + 1, base, base + 3, base + 2});
        }
        const BRITE::ModelHandle model = m_backend.LoadModelFromMesh(mesh);
        EXPECT_NE(model, BRITE::NullModelHandle);
        m_models.push_back(model);
        return model;
    }

    /// The draw of `model` in `tint`, unlit and seen from both sides.
    static BRITE::ModelDrawCommand Unlit(BRITE::ModelHandle model, BRITE::Color tint) {
        BRITE::ModelDrawCommand draw;
        draw.Model = model;
        draw.Material.Unlit = true;
        draw.Material.DoubleSided = true;
        draw.Material.AlbedoTint = tint;
        draw.Position = {0.0f, 0.0f, 0.0f};
        draw.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        draw.Scale = {1.0f, 1.0f, 1.0f};
        return draw;
    }

    /// Submit `pass` into `target` (a fresh RGBA8 render texture when none) under the camera above, cleared to
    /// `pass.ClearColor`, and read it back, top row first.
    std::vector<BRITE::Color> Picture(BRITE::RenderPass& pass, BRITE::TextureHandle target = BRITE::NullTextureHandle) {
        BRITE::Camera3D camera = Camera();
        const bool own = target == BRITE::NullTextureHandle;
        if (own)
            target = m_backend.LoadRenderTexture(SIZE, SIZE);
        pass.TargetFramebuffer = target;
        pass.Camera3DPtr = &camera;
        m_backend.SubmitRenderPass(pass);
        int width = 0, height = 0;
        std::vector<BRITE::Color> pixels;
        EXPECT_TRUE(m_backend.ReadRenderTexture(target, width, height, pixels));
        if (own)
            m_backend.UnloadRenderTexture(target);
        pass.Camera3DPtr = nullptr;
        return pixels;
    }

    /// The last HDR pass's linear scene, bottom row first.
    std::vector<float> LinearScene() {
        int width = 0, height = 0;
        std::vector<float> rgba;
        EXPECT_TRUE(m_backend.ReadLastHdrScene(width, height, rgba));
        return rgba;
    }

    BRITE::Backends::Raylib::RaylibRenderBackend m_backend;
    std::vector<BRITE::ModelHandle> m_models;
};

} // namespace HiddenWindow
