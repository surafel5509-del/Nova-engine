#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "particles/ParticleSystem.h"
#include "physics/PhysicsWorld.h"
#include "rendering/GlesRenderer.h"
#include "scene/RenderScene.h"
#include "scripting/LuaScriptEngine.h"

namespace nova {

/**
 * Engine facade driven by the Android editor and the game runtime through JNI.
 * Owns the renderer, the latest render scene, the physics simulation, the
 * particle system, and the Lua script VM. All methods are called on the GL
 * thread.
 */
class Engine {
public:
    Engine() = default;
    ~Engine();

    void onSurfaceCreated();
    void onSurfaceChanged(int width, int height);
    void onDrawFrame();

    void setSceneJson(const std::string& json);
    void setViewport(float centerX, float centerY, float pixelsPerUnit);
    void setGridVisible(bool visible);
    void loadTexture(const std::string& key, const unsigned char* rgba, int width, int height);
    void removeTexture(const std::string& key);

    /** Registers a Lua script source under [name] (used on next startSimulation). */
    void loadScript(const std::string& name, const std::string& source);

    // ---- Simulation (Play mode / runtime) ----
    /** Loads the current scene's bodies into the physics world + starts scripts. */
    void startSimulation();
    void stopSimulation();
    /** Advances scripts + physics + animation + particles by [dt] seconds. */
    void stepSimulation(float dt);
    bool isSimulating() const { return simulating_; }
    /** Serializes current simulated world positions back to JSON. */
    std::string snapshotPositionsJson() const;

    /** Drains sound paths queued by scripts (JSON array). */
    std::string consumeSoundEventsJson();
    /** Drains script log/error lines (JSON array) and forwards to logcat. */
    std::string consumeLogsJson();
    /** Drains UI text updates queued by scripts (JSON array of {id, text}). */
    std::string consumeUiTextEventsJson();
    /** Profiler snapshot: fps, frame ms, draw calls, entities, particles, bodies. */
    std::string statsJson() const;

    // ---- Game view / runtime ----
    void setUseGameCamera(bool use);
    void setShowGameCamera(bool show);
    void setShowPhysicsDebug(bool show);

    // ---- Input (runtime) ----
    void setInputAxis(float x, float y);   // -1..1 movement axis
    void setInputJump(bool pressed);
    /** Registers a tap at world coordinates (UI hit-testing during play). */
    void onTap(float worldX, float worldY);

    const RenderScene& scene() const { return scene_; }

private:
    void applyGameCameraIfEnabled();
    void syncAnimatedSprites();
    void updateCameraFollow(float dt);

    GlesRenderer renderer_;
    RenderScene scene_;
    PhysicsWorld physics_;
    ParticleSystem particles_;
    LuaScriptEngine scripting_;
    bool glReady_ = false;
    bool simulating_ = false;
    bool useGameCamera_ = false;
    std::vector<std::pair<std::string, std::string>> uiTextEvents_;

    // Animation state: accumulated time per sprite id.
    std::unordered_map<std::string, float> animTime_;
    float inputAxisX_ = 0.0f;
    float inputAxisY_ = 0.0f;
    bool inputJump_ = false;

    // Profiler state.
    std::chrono::steady_clock::time_point lastFrameStart_;
    float frameMs_ = 0.0f;
    float fps_ = 0.0f;
    int fpsFrames_ = 0;
    float fpsAccumSec_ = 0.0f;
};

} // namespace nova
