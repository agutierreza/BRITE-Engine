#include "Framework/Application.hpp"
#include "Framework/Scene.hpp"

#include "../Core/InputManager.hpp"
#include "../Systems/PhysicsSystem.hpp"
#include <algorithm>
#include <physfs.h>
#include <spdlog/spdlog.h>

namespace brite {
namespace framework {

// --- PhysFS Raylib Callbacks ---
static unsigned char* LoadFileDataCustom(const char* fileName, int* dataSize) {
    if (!PHYSFS_exists(fileName)) {
        spdlog::error("PHYSFS: File not found: {}", fileName);
        return nullptr;
    }
    PHYSFS_File* file = PHYSFS_openRead(fileName);
    if (!file)
        return nullptr;
    PHYSFS_sint64 size = PHYSFS_fileLength(file);
    unsigned char* data = (unsigned char*)MemAlloc(size);
    PHYSFS_readBytes(file, data, size);
    PHYSFS_close(file);
    if (dataSize)
        *dataSize = (int)size;
    return data;
}

static char* LoadFileTextCustom(const char* fileName) {
    if (!PHYSFS_exists(fileName)) {
        spdlog::error("PHYSFS: File not found: {}", fileName);
        return nullptr;
    }
    PHYSFS_File* file = PHYSFS_openRead(fileName);
    if (!file)
        return nullptr;
    PHYSFS_sint64 size = PHYSFS_fileLength(file);
    char* text = (char*)MemAlloc(size + 1);
    PHYSFS_readBytes(file, text, size);
    text[size] = '\0';
    PHYSFS_close(file);
    return text;
}

Application::Application(std::unique_ptr<BRITE::Backends::IApplicationBackend> appBackend,
                         std::unique_ptr<BRITE::Backends::IInputBackend> inputBackend,
                         std::unique_ptr<BRITE::Backends::IRenderBackend> renderBackend, const std::string& title,
                         const std::string& orgName, const std::string& appName, int width, int height)
    : m_appBackend(std::move(appBackend)), m_inputBackend(std::move(inputBackend)),
      m_renderBackend(std::move(renderBackend)), m_title(title), m_orgName(orgName), m_appName(appName), m_width(width),
      m_height(height), m_running(false), m_fixedDt(1.0 / 60.0), m_timeScale(1.0) {
    InitSubsystems(title, width, height);
}

Application::~Application() {
    ShutdownSubsystems();
}

void Application::InitSubsystems(const std::string& title, int width, int height) {
    // 1. Initialize spdlog
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Starting BRITE Engine Framework...");

    // 2. Initialize PhysicsFS
    if (!PHYSFS_init(m_appName.c_str())) {
        spdlog::critical("PHYSFS: Failed to initialize! Error: {}", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
    }

    const char* prefDir = PHYSFS_getPrefDir(m_orgName.c_str(), m_appName.c_str());
    if (prefDir) {
        if (PHYSFS_setWriteDir(prefDir) == 0) {
            spdlog::error("PHYSFS: Failed to set write dir to {}. Error: {}", prefDir,
                          PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        } else {
            spdlog::info("PHYSFS: Write dir set to {}", prefDir);
        }
    } else {
        spdlog::error("PHYSFS: Failed to get pref dir for {} / {}", m_orgName, m_appName);
    }

    if (PHYSFS_mount("assets.pak", NULL, 1)) {
        spdlog::info("PHYSFS: Mounted assets.pak");
    } else if (PHYSFS_mount("build/assets.pak", NULL, 1)) {
        spdlog::info("PHYSFS: Mounted build/assets.pak");
    } else if (PHYSFS_mount("assets", "assets", 1)) {
        spdlog::info("PHYSFS: Mounted local assets folder to /assets");
    } else {
        spdlog::warn("PHYSFS: Failed to mount any assets!");
    }

    // 3. (Removed Raylib file callbacks through PhysicsFS; should be handled by backend or manually if needed)

    if (m_inputBackend) {
        BRITE::InputManager::Initialize(m_inputBackend.get());
    }

    // 4. Initialize backend window
    if (m_appBackend) {
        m_appBackend->Init(title, width, height);
        m_appBackend->SetTargetFPS(144);
    }

    // 5. Initialize SoLoud removed
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
    }
    if (m_appBackend) {
        m_appBackend->Shutdown();
    }
    PHYSFS_deinit();
    spdlog::shutdown();
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
    }
    m_internalResolution = {(float)width, (float)height};
    if (m_renderBackend) {
        m_framebuffer = m_renderBackend->LoadRenderTexture(width, height);
    }
    m_useInternalResolution = true;
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
    double accumulator = 0.0;
    double previousTime = m_appBackend ? m_appBackend->GetTime() : 0.0;

    OnStart(); // Let the user game configure the initial scene

    while (m_running && (!m_appBackend || !m_appBackend->WindowShouldClose())) {
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
        accumulator += (frameTime * m_timeScale);

        // Fixed timestep loop
        while (accumulator >= m_fixedDt) {
            if (!m_sceneStack.empty()) {
                auto& activeScene = m_sceneStack.back();
                entt::dispatcher* dispatcher = activeScene->GetRegistry().ctx().find<entt::dispatcher>();
                if (dispatcher) {
                    BRITE::InputManager::FlushFixed(*dispatcher);
                }
            }

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
            accumulator -= m_fixedDt;
        }

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
            m_renderBackend->BeginTextureMode(m_framebuffer);
            m_renderBackend->ClearBackground({0, 0, 0, 255}); // BLACK

            for (auto& scene : scenesToRender) {
                scene->OnRender();
            }

            m_renderBackend->EndTextureMode();

            m_renderBackend->BeginDrawing();
            m_renderBackend->ClearBackground({0, 0, 0, 255}); // BLACK

            int screenWidth = m_appBackend->GetScreenWidth();
            int screenHeight = m_appBackend->GetScreenHeight();

            float scale =
                std::min((float)screenWidth / m_internalResolution.x, (float)screenHeight / m_internalResolution.y);

            BRITE::Rectangle sourceRec = {0.0f, 0.0f, m_internalResolution.x, -m_internalResolution.y};
            BRITE::Rectangle destRec = {(screenWidth - m_internalResolution.x * scale) * 0.5f,
                                        (screenHeight - m_internalResolution.y * scale) * 0.5f,
                                        m_internalResolution.x * scale, m_internalResolution.y * scale};

            m_renderBackend->DrawSprite(m_framebuffer, sourceRec, destRec, {0.0f, 0.0f}, 0.0f,
                                        {255, 255, 255, 255}); // WHITE
            m_renderBackend->EndDrawing();
        } else if (m_appBackend && m_renderBackend) {
            m_renderBackend->BeginDrawing();
            m_renderBackend->ClearBackground({0, 0, 0, 255}); // BLACK

            for (auto& scene : scenesToRender) {
                scene->OnRender();
            }

            m_renderBackend->EndDrawing();
        }
    }
}

bool Application::SaveState(const std::string& filename) {
    try {
        std::string serialized = m_gameState.dump(4);

        PHYSFS_File* file = PHYSFS_openWrite(filename.c_str());
        if (!file) {
            spdlog::error("PHYSFS: Failed to open {} for writing. Error: {}", filename,
                          PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
            return false;
        }

        PHYSFS_sint64 written = PHYSFS_writeBytes(file, serialized.c_str(), serialized.length());
        PHYSFS_close(file);

        if (written != serialized.length()) {
            spdlog::error("PHYSFS: Failed to write all bytes to {}", filename);
            return false;
        }

        spdlog::info("Saved state to {}", filename);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Exception saving state: {}", e.what());
        return false;
    }
}

bool Application::LoadState(const std::string& filename) {
    if (!PHYSFS_exists(filename.c_str())) {
        spdlog::warn("Save file {} does not exist", filename);
        return false;
    }

    PHYSFS_File* file = PHYSFS_openRead(filename.c_str());
    if (!file) {
        spdlog::error("PHYSFS: Failed to open {} for reading. Error: {}", filename,
                      PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }

    PHYSFS_sint64 size = PHYSFS_fileLength(file);
    std::string buffer;
    buffer.resize(size);

    PHYSFS_sint64 readBytes = PHYSFS_readBytes(file, buffer.data(), size);
    PHYSFS_close(file);

    if (readBytes != size) {
        spdlog::error("PHYSFS: Failed to read all bytes from {}", filename);
        return false;
    }

    try {
        m_gameState = nlohmann::json::parse(buffer);
        spdlog::info("Loaded state from {}", filename);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Exception loading state: {}", e.what());
        return false;
    }
}

} // namespace framework
} // namespace brite
