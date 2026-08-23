#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "physics/PhysicsWorld.h"
#include "rendering/GlesRenderer.h"
#include "scene/RenderScene.h"

namespace nova {

/**
 * Engine facade driven by the Android editor and the game runtime through JNI.
 * Owns the renderer, the latest render scene, and the physics simulation.
 * All methods are called on the GL thread.
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

    // ---- Simulation (Play mode / runtime) ----
    /** Loads the current scene's bodies into the physics world. */
    void startSimulation();
    void stopSimulation();
    /** Advances physics + animation by [dt] seconds. */
    void stepSimulation(float dt);
    bool isSimulating() const { return simulating_; }
    /** Serializes current simulated world positions back to JSON. */
    std::string snapshotPositionsJson() const;

    // ---- Game view / runtime ----
    void setUseGameCamera(bool use);
    void setShowGameCamera(bool show);
    void setShowPhysicsDebug(bool show);

    // ---- Input (runtime) ----
    void setInputAxis(float x, float y);   // -1..1 movement axis
    void setInputJump(bool pressed);

    const RenderScene& scene() const { return scene_; }

private:
    void applyGameCameraIfEnabled();
    void syncAnimatedSprites();

    GlesRenderer renderer_;
    RenderScene scene_;
    PhysicsWorld physics_;
    bool glReady_ = false;
    bool simulating_ = false;
    bool useGameCamera_ = false;

    // Animation state: accumulated time per sprite id.
    std::unordered_map<std::string, float> animTime_;
    float inputAxisX_ = 0.0f;
    float inputAxisY_ = 0.0f;
    bool inputJump_ = false;
};

} // namespace nova
