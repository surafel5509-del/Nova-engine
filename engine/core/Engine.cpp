#include "core/Engine.h"

#include <cmath>
#include <cstdio>

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
    if (!renderer3d_.initialize(&error)) {
        LOGE("Renderer3D initialization failed: %s", error.c_str());
        // 3D is optional; the 2D pipeline still works.
    }
    glReady_ = true;
    LOGI("Nova engine surface created");
}

void Engine::onSurfaceChanged(int width, int height) {
    renderer_.setViewportSize(width, height);
    renderer3d_.setViewportSize(width, height);
}

void Engine::onDrawFrame() {
    if (!glReady_) return;
    const auto start = std::chrono::steady_clock::now();
    if (scene_.mode3d) {
        renderer3d_.drawFrame(scene_);
    } else {
        applyGameCameraIfEnabled();
        renderer_.drawFrame(scene_, simulating_ ? &particles_ : nullptr);
    }
    const auto end = std::chrono::steady_clock::now();
    frameMs_ = std::chrono::duration<float, std::milli>(end - start).count();

    ++fpsFrames_;
    fpsAccumSec_ += std::chrono::duration<float>(end - lastFrameStart_).count();
    lastFrameStart_ = end;
    if (fpsAccumSec_ >= 0.5f) {
        fps_ = fpsFrames_ / fpsAccumSec_;
        fpsFrames_ = 0;
        fpsAccumSec_ = 0.0f;
    }
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
        startSimulation(); // re-seed bodies + particles + scripts from the fresh scene
    }
}

void Engine::setViewport(float centerX, float centerY, float pixelsPerUnit) {
    CameraState camera;
    camera.centerX = centerX;
    camera.centerY = centerY;
    camera.pixelsPerUnit = pixelsPerUnit > 0.0f ? pixelsPerUnit : 100.0f;
    renderer_.setCamera(camera);
}

void Engine::setViewport3D(float yawDeg, float pitchDeg, float distance,
                           float targetX, float targetY, float targetZ, float fovDeg) {
    renderer3d_.setCamera(yawDeg, pitchDeg, distance, targetX, targetY, targetZ, fovDeg);
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

void Engine::loadScript(const std::string& name, const std::string& source) {
    scripting_.loadScript(name, source);
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
    particles_.configure(scene_.emitters);

    // Start Lua: bind the live world, run scripts, fire on_start.
    scripting_.bind(&scene_, &physics_);
    std::string scriptError;
    scripting_.start(&scriptError);
    scripting_.callOnStart();
    for (const std::string& line : scripting_.logMessages()) {
        LOGI("%s", line.c_str());
    }
    scripting_.logMessages().clear();

    simulating_ = true;
    simElapsed_ = 0.0f;
    LOGI("Simulation started (%zu bodies, %zu emitters, %zu scripts)",
         physics_.bodies().size(), scene_.emitters.size(), scene_.scripts.size());
}

void Engine::stopSimulation() {
    simulating_ = false;
    simElapsed_ = 0.0f;
    scripting_.stop();
    particles_.clear();
    physics_.clear();
    LOGI("Simulation stopped");
}

void Engine::stepSimulation(float dt) {
    if (!simulating_) return;
    simElapsed_ += dt;

    // Keyframe animations (tracks on sprite properties) apply at sim time.
    if (!scene_.animations.empty()) {
        AnimationSampler::apply(scene_, simElapsed_);
    }

    // Scripts run first so script-set velocities/positions apply this frame.
    scripting_.update(dt, inputAxisX_, inputAxisY_, inputJump_);

    // Built-in character controller fallback: if no script drives the dynamic
    // body, apply the input axis directly.
    if (scene_.scripts.empty()) {
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
    }

    physics_.step(dt);

    // Write simulated positions back into the render scene.
    for (const auto& b : physics_.bodies()) {
        if (SpriteInstance* s = scene_.findSprite(b.id)) {
            s->x = b.x;
            s->y = b.y;
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

    // Emit + integrate particles.
    particles_.update(dt);

    // Camera follows its target (if configured).
    updateCameraFollow(dt);

    // Script-triggered UI text changes ride the event queue to Kotlin.
    for (const auto& evt : scripting_.uiTextEvents()) {
        uiTextEvents_.push_back(evt);
    }
    scripting_.uiTextEvents().clear();
}

void Engine::updateCameraFollow(float dt) {
    if (scene_.gameCamera.followId.empty()) return;
    const SpriteInstance* target = scene_.findSprite(scene_.gameCamera.followId);
    if (!target) return;
    const float t = 1.0f - std::exp(-scene_.gameCamera.followLerp * dt);
    scene_.gameCamera.x += (target->x - scene_.gameCamera.x) * t;
    scene_.gameCamera.y += (target->y - scene_.gameCamera.y) * t;
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

namespace {
std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string stringArrayJson(std::vector<std::string>& items) {
    std::string out = "[";
    bool first = true;
    for (const std::string& item : items) {
        if (!first) out += ",";
        first = false;
        out += "\"" + jsonEscape(item) + "\"";
    }
    out += "]";
    items.clear();
    return out;
}
} // namespace

std::string Engine::consumeSoundEventsJson() {
    return stringArrayJson(scripting_.soundEvents());
}

std::string Engine::consumeLogsJson() {
    for (const std::string& line : scripting_.logMessages()) {
        LOGI("%s", line.c_str());
    }
    return stringArrayJson(scripting_.logMessages());
}

std::string Engine::consumeUiTextEventsJson() {
    std::string out = "[";
    bool first = true;
    for (const auto& evt : uiTextEvents_) {
        if (!first) out += ",";
        first = false;
        out += "{\"id\":\"" + jsonEscape(evt.first) +
               "\",\"text\":\"" + jsonEscape(evt.second) + "\"}";
    }
    out += "]";
    uiTextEvents_.clear();
    return out;
}

void Engine::onTap(float worldX, float worldY) {
    // UI hit-test: topmost (last) element whose rect contains the tap.
    for (auto it = scene_.uiElements.rbegin(); it != scene_.uiElements.rend(); ++it) {
        const UiElementRecord& u = *it;
        const float cx = scene_.gameCamera.present ? scene_.gameCamera.x + u.offsetX : u.offsetX;
        const float cy = scene_.gameCamera.present ? scene_.gameCamera.y + u.offsetY : u.offsetY;
        if (std::fabs(worldX - cx) <= u.width / 2.0f && std::fabs(worldY - cy) <= u.height / 2.0f) {
            scripting_.queueUiPress(u.id);
            return;
        }
    }
}

std::string Engine::statsJson() const {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
        "{\"fps\":%.1f,\"frameMs\":%.2f,\"drawCalls\":%d,\"sprites\":%zu,"
        "\"bodies\":%zu,\"particles\":%zu,\"scripts\":%zu}",
        fps_, frameMs_, renderer_.lastDrawCalls(), scene_.sprites.size(),
        physics_.bodies().size(), particles_.totalParticles(), scene_.scripts.size());
    return std::string(buffer);
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
