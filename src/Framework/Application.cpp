#include "Framework/Application.hpp"
#include "Framework/Scene.hpp"

#include <raylib.h>
#include <physfs.h>
#include <spdlog/spdlog.h>
#include <box2d/box2d.h>
#include "../Systems/PhysicsSystem.hpp"
#include <algorithm>

namespace brite {
namespace framework {

// --- PhysFS Raylib Callbacks ---
static unsigned char* LoadFileDataCustom(const char *fileName, int *dataSize) {
    if (!PHYSFS_exists(fileName)) {
        spdlog::error("PHYSFS: File not found: {}", fileName);
        return nullptr;
    }
    PHYSFS_File *file = PHYSFS_openRead(fileName);
    if (!file) return nullptr;
    PHYSFS_sint64 size = PHYSFS_fileLength(file);
    unsigned char *data = (unsigned char *)MemAlloc(size);
    PHYSFS_readBytes(file, data, size);
    PHYSFS_close(file);
    if (dataSize) *dataSize = (int)size;
    return data;
}

static char* LoadFileTextCustom(const char *fileName) {
    if (!PHYSFS_exists(fileName)) {
        spdlog::error("PHYSFS: File not found: {}", fileName);
        return nullptr;
    }
    PHYSFS_File *file = PHYSFS_openRead(fileName);
    if (!file) return nullptr;
    PHYSFS_sint64 size = PHYSFS_fileLength(file);
    char *text = (char *)MemAlloc(size + 1);
    PHYSFS_readBytes(file, text, size);
    text[size] = '\0';
    PHYSFS_close(file);
    return text;
}

Application::Application(const std::string& title, int width, int height)
    : m_title(title), m_width(width), m_height(height), m_running(false), m_fixedDt(1.0 / 60.0), m_timeScale(1.0) {
    InitSubsystems(title, width, height);
}

Application::~Application() {
    ShutdownSubsystems();
}

void Application::InitSubsystems(const std::string& title, int width, int height) {
    // 1. Initialize spdlog
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Starting BRITE Engine Framework...");

    // 2. Initialize PhysicsFS (Passing "BRITE" as fallback argv0)
    if (!PHYSFS_init("BRITE")) {
        spdlog::critical("PHYSFS: Failed to initialize! Error: {}", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
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

    // 3. Route Raylib file callbacks through PhysicsFS
    SetLoadFileDataCallback(LoadFileDataCustom);
    SetLoadFileTextCallback(LoadFileTextCustom);

    // 4. Initialize Raylib window
    InitWindow(width, height, title.c_str());
    ::SetTargetFPS(144); // Global namespace to avoid name hiding
    
    // 5. Initialize SoLoud
    m_soloud.init();
    
    // ImGui initialization would go here when needed
    // rlImGuiSetup(true);
}

void Application::ShutdownSubsystems() {
    spdlog::info("Shutting down BRITE Engine Framework...");

    // Clear active scenes to ensure destructors run before subsystems shutdown
    m_currentScene.reset();
    m_nextScene.reset();

    // rlImGuiShutdown();

    m_soloud.deinit();
    if (m_useInternalResolution) {
        UnloadRenderTexture(m_framebuffer);
    }
    CloseWindow();
    PHYSFS_deinit();
    spdlog::shutdown();
}

void Application::SetTargetFPS(int fps) {
    ::SetTargetFPS(fps);
}

void Application::SetFixedTimeStep(double dt) {
    m_fixedDt = dt;
}

void Application::SetTimeScale(double scale) {
    m_timeScale = scale;
}

void Application::SetInternalResolution(int width, int height) {
    if (m_useInternalResolution) {
        UnloadRenderTexture(m_framebuffer);
    }
    m_internalResolution = { (float)width, (float)height };
    m_framebuffer = LoadRenderTexture(width, height);
    m_useInternalResolution = true;
}

void Application::ChangeScene(std::shared_ptr<Scene> newScene) {
    m_nextScene = newScene;
}

void Application::Quit() {
    m_running = false;
}

void Application::Run() {
    m_running = true;
    double accumulator = 0.0;
    double previousTime = GetTime();

    OnStart(); // Let the user game configure the initial scene

    while (m_running && !WindowShouldClose()) {
        // Handle Scene Transitions
        if (m_nextScene) {
            if (m_currentScene) {
                m_currentScene->OnShutdown();
            }
            m_currentScene = m_nextScene;
            m_nextScene.reset();
            if (m_currentScene) {
                m_currentScene->OnStart();
            }
        }

        double currentTime = GetTime();
        double frameTime = currentTime - previousTime;
        previousTime = currentTime;

        if (frameTime > 0.25) frameTime = 0.25; // Spiral of death prevention
        accumulator += (frameTime * m_timeScale);

        // Fixed timestep loop
        while (accumulator >= m_fixedDt) {
            if (m_currentScene) {
                // [PRE-STEP ECS SYSTEMS] e.g. Input, Apply Gravity
                m_currentScene->OnPreStep(m_fixedDt);

                // Initialize / sync Box2D bodies
                BRITE::PhysicsSystem::PreStep(m_currentScene->GetRegistry(), m_currentScene->GetPhysicsWorld());

                // [BOX2D WORLD STEP]
                b2World_Step(m_currentScene->GetPhysicsWorld(), m_fixedDt, 4);

                // Sync Box2D bodies back to Transform
                BRITE::PhysicsSystem::PostStep(m_currentScene->GetRegistry());

                // [POST-STEP ECS SYSTEMS] e.g. Sync Transforms, Animations
                m_currentScene->OnPostStep(m_fixedDt);
            }
            accumulator -= m_fixedDt;
        }

        // Render loop
        if (m_useInternalResolution) {
            BeginTextureMode(m_framebuffer);
            ClearBackground(BLACK);

            if (m_currentScene) {
                m_currentScene->OnRender();
            }

            EndTextureMode();

            BeginDrawing();
            ClearBackground(BLACK);

            int screenWidth = GetScreenWidth();
            int screenHeight = GetScreenHeight();

            float scale = std::min((float)screenWidth / m_internalResolution.x, 
                                   (float)screenHeight / m_internalResolution.y);

            Rectangle sourceRec = { 0.0f, 0.0f, m_internalResolution.x, -m_internalResolution.y };
            Rectangle destRec = {
                (screenWidth - m_internalResolution.x * scale) * 0.5f,
                (screenHeight - m_internalResolution.y * scale) * 0.5f,
                m_internalResolution.x * scale,
                m_internalResolution.y * scale
            };

            DrawTexturePro(m_framebuffer.texture, sourceRec, destRec, { 0.0f, 0.0f }, 0.0f, WHITE);
            EndDrawing();
        } else {
            BeginDrawing();
            ClearBackground(BLACK);

            if (m_currentScene) {
                // [RENDER ECS SYSTEMS] e.g. Draw Sprites, Draw UI
                m_currentScene->OnRender();
            }

            EndDrawing();
        }
    }
}

} // namespace framework
} // namespace brite
