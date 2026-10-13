#include "Framework/Application.hpp"
#include "Framework/Scene.hpp"

#include "Core/InputManager.hpp"
#include "Core/Profiler.hpp"
#include "Systems/PhysicsSystem.hpp"
#include <algorithm>
#include <cassert>
#include <spdlog/spdlog.h>

namespace brite {
namespace framework {

Application::Application(std::unique_ptr<BRITE::Backends::IApplicationBackend> appBackend,
                         std::unique_ptr<BRITE::Backends::IInputBackend> inputBackend,
                         std::unique_ptr<BRITE::Backends::IRenderBackend> renderBackend, const std::string& title,
                         const std::string& orgName, const std::string& appName, int width, int height,
                         const ApplicationOptions& options)
    : m_appBackend(std::move(appBackend)), m_inputBackend(std::move(inputBackend)),
      m_renderBackend(std::move(renderBackend)), m_title(title), m_options(options), m_orgName(orgName),
      m_appName(appName), m_width(width), m_height(height), m_running(false), m_fixedDt(1.0 / 60.0), m_timeScale(1.0) {
    InitSubsystems(title, width, height);
}

Application::~Application() {
    ShutdownSubsystems();
}

void Application::InitSubsystems(const std::string& title, int width, int height) {
    // 1. Initialize spdlog
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Starting BRITE Engine Framework...");

    // The profiler runs for this Application's lifetime, and once per process (Core/Profiler.hpp).
    m_startedProfiler = BRITE::Profiler::Start();
    if (!m_startedProfiler && BRITE::Profiler::CurrentState() == BRITE::Profiler::State::Stopped) {
        spdlog::info("Profiler: it ran for an earlier Application in this process and does not run again");
    }

    // 2. PhysicsFS, the application's files and a saved game state are the persistence module's (SaveStore),
    // owned by an application that wants them: the framework itself reads and writes no file.

    if (m_inputBackend) {
        BRITE::InputManager::Initialize(m_inputBackend.get());
    }

    // 4. Initialize backend window
    if (m_appBackend) {
        BRITE::Backends::WindowOptions window;
        window.MultisampleCount = m_options.MultisampleCount;
        window.Mode = m_options.Mode;
        window.Resizable = m_options.Resizable;
        m_appBackend->Init(title, width, height, window);
        m_appBackend->SetTargetFPS(144);
        m_windowMode = m_appBackend->GetWindowMode();
        if (m_windowMode != m_options.Mode) {
            spdlog::warn("Window: asked to open in another mode and the backend could not; it opened windowed");
        }
    }
    // Before the first frame, so the sizes are valid from OnStart on; with no
    // window, the size the window would have had.
    m_windowSize = {width, height};
    ReadSizes();
    m_drawSizeChanged = false;

    // 5. Initialize SoLoud removed
}

void Application::SetWindowMode(BRITE::Backends::WindowMode mode) {
    m_modeRequested = true;
    m_requestedMode = mode;
}

void Application::SetWindowSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        spdlog::warn("Window: a size of {} x {} is no size, and is ignored", width, height);
        return;
    }
    m_sizeRequested = true;
    m_requestedSize = {width, height};
}

void Application::ApplyWindowRequests() {
    if (!m_appBackend) {
        // Nothing to change; the requests are dropped rather than kept for a
        // window that will never exist.
        m_modeRequested = false;
        m_sizeRequested = false;
        return;
    }
    using BRITE::Backends::WindowMode;
    if (m_modeRequested) {
        m_modeRequested = false;
        if (!m_appBackend->SetWindowMode(m_requestedMode)) {
            spdlog::warn("Window: the backend could not change the window's mode; it stays as it was");
        }
        m_windowMode = m_appBackend->GetWindowMode();
    }
    // A size is only a windowed one: while full screen it waits for windowed.
    if (m_sizeRequested && m_windowMode == WindowMode::Windowed) {
        m_sizeRequested = false;
        if (!m_appBackend->SetWindowSize(m_requestedSize.Width, m_requestedSize.Height)) {
            spdlog::warn("Window: the backend could not resize the window to {} x {}", m_requestedSize.Width,
                         m_requestedSize.Height);
        }
    }
}

void Application::ReadSizes() {
    if (m_appBackend) {
        const PixelSize reported{m_appBackend->GetScreenWidth(), m_appBackend->GetScreenHeight()};
        // Minimised, a window may report nothing; it keeps the size it had.
        if (reported.Width > 0 && reported.Height > 0) {
            m_windowSize = reported;
        }
    }
    m_drawSize = m_useInternalResolution
                     ? PixelSize{static_cast<int>(m_internalResolution.x), static_cast<int>(m_internalResolution.y)}
                     : m_windowSize;
    // Against what the previous frame saw at ITS top, not against m_drawSize:
    // SetInternalResolution changes m_drawSize during a frame, and a frame
    // that compared against it would miss that change.
    m_drawSizeChanged = !(m_drawSize == m_frameTopDrawSize);
    m_frameTopDrawSize = m_drawSize;
}

void Application::ShutdownSubsystems() {
    spdlog::info("Shutting down BRITE Engine Framework...");

    // Clear active scenes to ensure destructors run before subsystems shutdown
    for (auto it = m_sceneStack.rbegin(); it != m_sceneStack.rend(); ++it) {
        (*it)->OnShutdown();
    }
    m_sceneStack.clear();
    m_pendingActions.clear();

    // rlImGuiShutdown();

    if (m_useInternalResolution && m_renderBackend) {
        m_renderBackend->UnloadRenderTexture(m_framebuffer);
        if (m_framebufferAlt != BRITE::NullTextureHandle) {
            m_renderBackend->UnloadRenderTexture(m_framebufferAlt);
        }
    }
    if (m_appBackend) {
        m_appBackend->Shutdown();
    }
    // The input manager is process-wide and keeps a pointer to the backend it
    // polls. The backend dies with this Application, so the pointer goes with
    // it -- if it is still this one's: a later Application that brought its own
    // has taken the manager over, and it is not this one's to clear. (An
    // Application with no input backend leaves the manager alone: it has
    // nothing to hand it, and clearing it would switch off the input of one
    // that is still alive.)
    if (m_inputBackend && BRITE::InputManager::Backend() == m_inputBackend.get()) {
        BRITE::InputManager::Initialize(nullptr);
    }
    // Flushed, never shut down. The logger belongs to the process, not to one
    // application: spdlog::shutdown() dropped the default logger for good, and
    // the next Application in the same process logged through nothing as it
    // started. Flushing keeps what shutting down was for -- nothing written
    // is lost -- and leaves the logger for whoever comes next.
    if (auto logger = spdlog::default_logger())
        logger->flush();
    // Last: every scene is gone, so no zone of the framework's is open. Tracy's threads end here,
    // inside main, where their thread_local destructors still run.
    if (m_startedProfiler) {
        BRITE::Profiler::Stop();
        m_startedProfiler = false;
    }
}

void Application::SetTargetFPS(int fps) {
    if (m_appBackend) {
        m_appBackend->SetTargetFPS(fps);
    }
}

void Application::SetFixedTimeStep(double dt) {
    m_fixedDt = dt;
}

void Application::SetTimeScale(double scale) {
    m_timeScale = scale;
}

void Application::SetInternalResolution(int width, int height) {
    if (m_useInternalResolution && m_renderBackend) {
        m_renderBackend->UnloadRenderTexture(m_framebuffer);
        if (m_framebufferAlt != BRITE::NullTextureHandle) {
            m_renderBackend->UnloadRenderTexture(m_framebufferAlt);
        }
    }
    m_internalResolution = {(float)width, (float)height};
    if (m_renderBackend) {
        // The scenes are drawn into m_framebuffer, so it is the one that needs
        // the samples; the alternate only ever receives full-screen
        // post-process passes, which have no edges of their own to smooth.
        m_framebuffer = m_renderBackend->LoadMultisampledRenderTexture(width, height, m_options.MultisampleCount);
        m_framebufferAlt = m_renderBackend->LoadRenderTexture(width, height);
    }
    m_useInternalResolution = true;
    // The framebuffer the scenes draw into is replaced now, so the draw size
    // is too, rather than at the top of the next frame.
    const PixelSize drawn{width, height};
    if (!(drawn == m_drawSize)) {
        m_drawSize = drawn;
        m_drawSizeChanged = true;
    }
}

void Application::PushScene(std::shared_ptr<Scene> newScene) {
    m_pendingActions.push_back({SceneActionType::Push, newScene});
}

void Application::PopScene() {
    m_pendingActions.push_back({SceneActionType::Pop, nullptr});
}

void Application::ChangeScene(std::shared_ptr<Scene> newScene) {
    m_pendingActions.push_back({SceneActionType::Change, newScene});
}

void Application::Quit() {
    m_running = false;
}

void Application::Run() {
    m_running = true;
    m_accumulator = 0.0;
    m_tickFraction = 0.0;
    double previousTime = m_appBackend ? m_appBackend->GetTime() : 0.0;

    OnStart(); // Let the user game configure the initial scene

    while (m_running && (!m_appBackend || !m_appBackend->WindowShouldClose())) {
        // The window first: whatever was asked of it last frame, then the
        // sizes every phase of this frame reads.
        ApplyWindowRequests();
        ReadSizes();

        // Handle Scene Transitions
        for (auto& action : m_pendingActions) {
            if (action.type == SceneActionType::Push) {
                if (action.scene) {
                    m_sceneStack.push_back(action.scene);
                    action.scene->OnStart();
                }
            } else if (action.type == SceneActionType::Pop) {
                if (!m_sceneStack.empty()) {
                    m_sceneStack.back()->OnShutdown();
                    m_sceneStack.pop_back();
                }
            } else if (action.type == SceneActionType::Change) {
                for (auto it = m_sceneStack.rbegin(); it != m_sceneStack.rend(); ++it) {
                    (*it)->OnShutdown();
                }
                m_sceneStack.clear();
                if (action.scene) {
                    m_sceneStack.push_back(action.scene);
                    action.scene->OnStart();
                }
            }
        }
        m_pendingActions.clear();

        if (!m_sceneStack.empty()) {
            auto& activeScene = m_sceneStack.back();
            entt::dispatcher* dispatcher = activeScene->GetRegistry().ctx().find<entt::dispatcher>();
            if (!dispatcher) {
                dispatcher = &activeScene->GetRegistry().ctx().emplace<entt::dispatcher>();
            }
            BRITE::InputManager::PollVariable(*dispatcher);
        }

        if (m_inputBackend) {
            m_inputBackend->PollEvents();
        }

        double currentTime = m_appBackend ? m_appBackend->GetTime() : 0.0;
        double frameTime = currentTime - previousTime;
        previousTime = currentTime;

        if (frameTime > 0.25)
            frameTime = 0.25; // Spiral of death prevention
        m_accumulator += (frameTime * m_timeScale);

        // Fixed timestep loop
        while (m_accumulator >= m_fixedDt) {
            if (!m_sceneStack.empty()) {
                auto& activeScene = m_sceneStack.back();
                entt::dispatcher* dispatcher = activeScene->GetRegistry().ctx().find<entt::dispatcher>();
                if (dispatcher) {
                    BRITE::InputManager::FlushFixed(*dispatcher);
                }
            }

            // The application's own tick: the input above is this tick's, and
            // no scene has read it yet.
            OnFixedTick(m_fixedDt);

            for (auto it = m_sceneStack.rbegin(); it != m_sceneStack.rend(); ++it) {
                auto& scene = *it;

                // Phase 1: Instantiation (Entities are spawned/destroyed)
                scene->OnInstantiation();

                // Phase 2: Physics Initialization (Box2D bodies created)
                scene->GetPhysicsSystem().PreStep(scene->GetRegistry());

                // Phase 3: Game Logic & Input
                scene->OnLogicStep(m_fixedDt);

                // Phase 4: Physics Step (b2World_Step + PostStep sync)
                scene->GetPhysicsSystem().Step(scene->GetRegistry(), m_fixedDt);

                // Phase 5: Render Prep
                scene->OnRenderPrepStep(m_fixedDt);

                if (scene->BlocksUpdate()) {
                    break;
                }
            }
            m_accumulator -= m_fixedDt;
        }

        // The residual, stored once per frame and only here. The loop above
        // has just guaranteed m_accumulator < m_fixedDt, so the fraction is in
        // [0, 1) by construction; anything else is a broken invariant, not an
        // input to clamp. Read it during rendering -- see TickFraction() for
        // what it means in the other phases.
        m_tickFraction = m_accumulator / m_fixedDt;
        assert(m_tickFraction >= 0.0 && m_tickFraction < 1.0 &&
               "the tick loop left more than a tick in the accumulator");

        // Determine scenes to render (top to bottom to find blocking, then render
        // bottom to top)
        std::vector<std::shared_ptr<Scene>> scenesToRender;
        for (auto it = m_sceneStack.rbegin(); it != m_sceneStack.rend(); ++it) {
            scenesToRender.push_back(*it);
            if ((*it)->BlocksRender()) {
                break;
            }
        }
        std::reverse(scenesToRender.begin(), scenesToRender.end());

        // Render loop
        if (m_useInternalResolution && m_renderBackend) {
            BRITE::RenderPass internalPass;
            internalPass.TargetFramebuffer = m_framebuffer;
            internalPass.ClearColor = BRITE::Black; // BLACK
            internalPass.ShouldClear = true;

            for (auto& scene : scenesToRender) {
                scene->OnRender(internalPass);
            }
            m_renderBackend->SubmitRenderPass(internalPass);

            // Post-Processing Ping-Pong
            BRITE::TextureHandle finalFramebuffer = m_framebuffer;

            if (!m_postProcessShaders.empty()) {
                BRITE::TextureHandle sourceFBO = m_framebuffer;
                BRITE::TextureHandle destFBO = m_framebufferAlt;

                for (size_t i = 0; i < m_postProcessShaders.size(); ++i) {
                    BRITE::RenderPass ppPass;
                    ppPass.TargetFramebuffer = destFBO;
                    ppPass.ClearColor = BRITE::Black;
                    ppPass.ShouldClear = true;
                    ppPass.Shader = m_postProcessShaders[i];

                    BRITE::SpriteDrawCommand ppSprite;
                    ppSprite.Material.AlbedoMap = sourceFBO;
                    ppSprite.SourceRect = {0.0f, 0.0f, m_internalResolution.x, -m_internalResolution.y}; // Flip Y
                    ppSprite.DestRect = {0.0f, 0.0f, m_internalResolution.x, m_internalResolution.y};
                    ppSprite.Origin = {0.0f, 0.0f};
                    ppSprite.RotationDeg = 0.0f;
                    ppSprite.Material.AlbedoTint = BRITE::White;

                    ppPass.SpriteCommands.push_back(ppSprite);

                    m_renderBackend->SubmitRenderPass(ppPass);

                    // Swap for next iteration
                    finalFramebuffer = destFBO;
                    std::swap(sourceFBO, destFBO);
                }
            }

            BRITE::RenderPass screenPass;
            screenPass.TargetFramebuffer = BRITE::NullTextureHandle;
            screenPass.ClearColor = BRITE::Black; // BLACK
            screenPass.ShouldClear = true;

            const int screenWidth = m_windowSize.Width;
            const int screenHeight = m_windowSize.Height;

            float scale =
                std::min((float)screenWidth / m_internalResolution.x, (float)screenHeight / m_internalResolution.y);

            BRITE::Rectangle sourceRec = {0.0f, 0.0f, m_internalResolution.x, -m_internalResolution.y};
            BRITE::Rectangle destRec = {(screenWidth - m_internalResolution.x * scale) * 0.5f,
                                        (screenHeight - m_internalResolution.y * scale) * 0.5f,
                                        m_internalResolution.x * scale, m_internalResolution.y * scale};

            BRITE::SpriteDrawCommand screenSprite;
            screenSprite.Material.AlbedoMap = finalFramebuffer;
            screenSprite.SourceRect = sourceRec;
            screenSprite.DestRect = destRec;
            screenSprite.Origin = {0.0f, 0.0f};
            screenSprite.RotationDeg = 0.0f;
            screenSprite.Material.AlbedoTint = BRITE::White; // WHITE

            screenPass.SpriteCommands.push_back(screenSprite);

            m_renderBackend->SubmitRenderPass(screenPass);
        } else if (m_appBackend && m_renderBackend) {
            BRITE::RenderPass screenPass;
            screenPass.TargetFramebuffer = BRITE::NullTextureHandle;
            screenPass.ClearColor = BRITE::Black; // BLACK
            screenPass.ShouldClear = true;

            for (auto& scene : scenesToRender) {
                scene->OnRender(screenPass);
            }

            m_renderBackend->SubmitRenderPass(screenPass);
        }
    }
}

} // namespace framework
} // namespace brite
