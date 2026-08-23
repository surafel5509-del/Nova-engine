#include "core/Engine.h"

#include "core/Log.h"

namespace nova {

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
    renderer_.drawFrame(scene_);
}

void Engine::setSceneJson(const std::string& json) {
    std::string error;
    if (!scene_.parseFrom(json, &error)) {
        LOGE("Scene update rejected: %s", error.c_str());
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

} // namespace nova
