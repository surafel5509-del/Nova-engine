#include "core/Engine.h"

#include <cmath>

#include "core/Log.h"

namespace nova {

namespace {
constexpr float kCharacterSpeed = 5.0f;   // world units / s
constexpr float kJumpVelocity = 8.0f;     // world units / s
} // namespace

Engine::~Engine() {
    if (glReady_) {
        renderer_.shutdown();
        glReady_ = false;
    }
}

void Engine::onSurfaceCreated() {
    std::string error;
    if (!renderer_.initialize(&error)) {
        LOGE("Renderer initialization failed: %s", error.c_str());
        return;
    }
    glReady_ = true;
    LOGI("Nova engine surface created");
}

void Engine::onSurfaceChanged(int width, int height) {
    renderer_.setViewportSize(width, height);
}

void Engine::onDrawFrame() {
    if (!glReady_) return;
    applyGameCameraIfEnabled();
    renderer_.drawFrame(scene_);
}

void Engine::setSceneJson(const std::string& json) {
    std::string error;
    if (!scene_.parseFrom(json, &error)) {
        LOGE("Scene update rejected: %s", error.c_str());
        return;
    }
    // New scene invalidates animation timers; physics restarts on next startSimulation.
    animTime_.clear();
    if (simulating_) {
        startSimulation(); // re-seed bodies from the fresh scene
    }
}

void Engine::setViewport(float centerX, float centerY, float pixelsPerUnit) {
    CameraState camera;
    camera.centerX = centerX;
    camera.centerY = centerY;
    camera.pixelsPerUnit = pixelsPerUnit > 0.0f ? pixelsPerUnit : 100.0f;
    renderer_.setCamera(camera);
}

void Engine::setGridVisible(bool visible) {
    renderer_.setGridVisible(visible);
}

void Engine::loadTexture(const std::string& key, const unsigned char* rgba, int width, int height) {
    if (!glReady_) return;
    renderer_.uploadTexture(key, rgba, width, height);
}

void Engine::removeTexture(const std::string& key) {
    if (!glReady_) return;
    renderer_.removeTexture(key);
}

// ---- Simulation ----

void Engine::startSimulation() {
    physics_.clear();
    physics_.gravity = -9.8f;
    for (const BodyRecord& rec : scene_.bodies) {
        PhysicsBody b;
        b.id = rec.id;
        b.bodyType = rec.bodyType;
        b.x = rec.x;
        b.y = rec.y;
        b.halfW = rec.halfW;
        b.halfH = rec.halfH;
        b.mass = rec.mass;
        b.gravityScale = rec.gravityScale;
        b.friction = rec.friction;
        b.restitution = rec.restitution;
        physics_.addBody(b);
    }
    simulating_ = true;
    LOGI("Simulation started (%zu bodies)", physics_.bodies().size());
}

void Engine::stopSimulation() {
    simulating_ = false;
    physics_.clear();
    LOGI("Simulation stopped");
}

void Engine::stepSimulation(float dt) {
    if (!simulating_) return;

    // Character-style input: drive the first dynamic body horizontally.
    for (auto& b : physics_.bodies()) {
        if (b.bodyType == 1) {
            if (inputAxisX_ != 0.0f) {
                b.vx = inputAxisX_ * kCharacterSpeed;
            }
            if (inputJump_ && b.grounded) {
                b.vy = kJumpVelocity;
                b.grounded = false;
            }
        }
    }

    physics_.step(dt);

    // Write simulated positions back into the render scene.
    for (const auto& b : physics_.bodies()) {
        for (auto& s : scene_.sprites) {
            if (s.id == b.id) {
                s.x = b.x;
                s.y = b.y;
            }
        }
    }

    // Advance sprite-sheet animations.
    for (auto& s : scene_.sprites) {
        if (s.frameCols * s.frameRows > 1) {
            float& t = animTime_[s.id];
            t += dt;
            const float fps = 8.0f;
            const int frames = s.frameCols * s.frameRows;
            s.frameIndex = static_cast<int>(t * fps) % frames;
        }
    }
}

std::string Engine::snapshotPositionsJson() const {
    std::string out = "{";
    bool first = true;
    for (const auto& b : physics_.bodies()) {
        if (!first) out += ",";
        first = false;
        out += "\"" + b.id + "\":{\"x\":" + std::to_string(b.x) +
               ",\"y\":" + std::to_string(b.y) + "}";
    }
    out += "}";
    return out;
}

// ---- Game view / runtime ----

void Engine::setUseGameCamera(bool use) {
    useGameCamera_ = use;
    renderer_.setUseGameCamera(use);
}

void Engine::setShowGameCamera(bool show) {
    renderer_.setShowGameCamera(show);
}

void Engine::setShowPhysicsDebug(bool show) {
    renderer_.setShowPhysicsDebug(show);
}

void Engine::applyGameCameraIfEnabled() {
    if (!useGameCamera_ || !scene_.gameCamera.present) return;
    const GameCamera& c = scene_.gameCamera;
    renderer_.setClearColor(c.bgR, c.bgG, c.bgB);
    CameraState cam;
    cam.centerX = c.x;
    cam.centerY = c.y;
    cam.pixelsPerUnit = c.zoom;
    renderer_.setCamera(cam);
}

// ---- Input ----

void Engine::setInputAxis(float x, float y) {
    inputAxisX_ = x;
    inputAxisY_ = y;
}

void Engine::setInputJump(bool pressed) {
    inputJump_ = pressed;
}

} // namespace nova
