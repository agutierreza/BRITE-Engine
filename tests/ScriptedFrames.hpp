#pragma once
// An Application driven by a scripted clock, and a scene that records what each
// frame saw: shared by the cases on the fixed timestep's residual
// (TickFractionTests.cpp) and on running an application one frame at a time
// (FrameStepTests.cpp).
//
// The clock is a window backend whose GetTime returns the instants fed to it and
// whose window "closes" when they run out. The application reads the first
// instant as it starts, then one per frame.

#include <Backends/IApplicationBackend.hpp>
#include <Backends/IRenderBackend.hpp>
#include <Framework/Application.hpp>
#include <Framework/Scene.hpp>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace scripted_frames {

/// A clock that answers GetTime with the next scripted instant and closes the
/// window once the script is spent.
class ScriptedClock : public BRITE::Backends::IApplicationBackend {
  public:
    explicit ScriptedClock(std::vector<double> times) : m_times(std::move(times)) {}

    void Init(const std::string&, int, int) override {}
    void Shutdown() override {}
    bool WindowShouldClose() override {
        return m_next >= m_times.size();
    }
    void SetTargetFPS(int) override {}
    double GetTime() override {
        if (m_next < m_times.size())
            return m_times[m_next++];
        return m_times.back();
    }
    int GetScreenWidth() override {
        return 1;
    }
    int GetScreenHeight() override {
        return 1;
    }

    /// More instants after the script was spent: the window that "closed" stops asking to close, as a host's window
    /// may.
    void Extend(const std::vector<double>& more) {
        m_times.insert(m_times.end(), more.begin(), more.end());
    }

  private:
    std::vector<double> m_times;
    std::size_t m_next = 0;
};

/// Draws nothing. Present only so that a frame reaches OnRender, which it does
/// not without a render backend.
class NullRenderBackend : public BRITE::Backends::IRenderBackend {
  public:
    void SubmitRenderPass(const BRITE::RenderPass&) override {}
    BRITE::TextureHandle LoadRenderTexture(int, int) override {
        return BRITE::NullTextureHandle;
    }
    void UnloadRenderTexture(BRITE::TextureHandle) override {}
    bool ReadRenderTexture(BRITE::TextureHandle, int&, int&, std::vector<BRITE::Color>&) override {
        return false;
    }
    BRITE::TextureHandle LoadTexture(const char*) override {
        return BRITE::NullTextureHandle;
    }
    void UnloadTexture(BRITE::TextureHandle) override {}
    BRITE::ModelHandle LoadModel(const char*) override {
        return BRITE::NullModelHandle;
    }
    BRITE::ModelHandle LoadModelFromMesh(const BRITE::MeshData&) override {
        return BRITE::NullModelHandle;
    }
    void UnloadModel(BRITE::ModelHandle) override {}
    BRITE::ShaderHandle LoadShader(const char*, const char*) override {
        return BRITE::NullShaderHandle;
    }
    BRITE::ShaderHandle LoadShaderFromMemory(const char*, const char*) override {
        return BRITE::NullShaderHandle;
    }
    void UnloadShader(BRITE::ShaderHandle) override {}
    BRITE::EnvironmentMap LoadEnvironmentMap(const char*) override {
        return BRITE::EnvironmentMap{};
    }
    void UnloadEnvironmentMap(BRITE::EnvironmentMap) override {}
    int GetShaderLocation(BRITE::ShaderHandle, const char*) override {
        return -1;
    }
    void SetShaderValue(BRITE::ShaderHandle, int, const void*, BRITE::Backends::ShaderUniformDataType) override {}
};

/// What one drawn frame saw: the fraction read in OnRender, and how many
/// ticks ran since the previous frame.
struct FrameReading {
    double fraction;
    int ticks;
};

/// Records the fraction where it is valid, and also what it reads during a
/// tick, to pin the documented staleness there.
class RecordingScene : public brite::framework::Scene {
  public:
    explicit RecordingScene(brite::framework::Application* app) : Scene(app) {}

    std::vector<FrameReading> frames;
    std::vector<double> readDuringTicks; // one per OnLogicStep

    void OnLogicStep(double) override {
        ++m_ticksSinceRender;
        readDuringTicks.push_back(GetApp()->TickFraction());
    }
    void OnRender(BRITE::RenderPass&) override {
        frames.push_back({GetApp()->TickFraction(), m_ticksSinceRender});
        m_ticksSinceRender = 0;
    }

  private:
    int m_ticksSinceRender = 0;
};

class RecordingApp : public brite::framework::Application {
  public:
    explicit RecordingApp(std::vector<double> times) : RecordingApp(new ScriptedClock(std::move(times))) {}

    std::shared_ptr<RecordingScene> scene;
    ScriptedClock* clock; // owned by the Application
    int starts = 0;       // how many times OnStart ran

  protected:
    void OnStart() override {
        ++starts;
        scene = std::make_shared<RecordingScene>(this);
        ChangeScene(scene);
    }

  private:
    explicit RecordingApp(ScriptedClock* owned)
        : Application(std::unique_ptr<ScriptedClock>(owned), nullptr, std::make_unique<NullRenderBackend>(),
                      "ScriptedFrames", "BRITE", "Engine", 1, 1),
          clock(owned) {}
};

constexpr double TICK = 1.0 / 64.0;   // 0.015625 s, exact in binary
constexpr double FRAME = 1.0 / 512.0; // 0.001953125 s: eight frames per tick

} // namespace scripted_frames
