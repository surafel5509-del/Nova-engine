#pragma once

#include <memory>
#include <string>

#include "rendering/GlesRenderer.h"
#include "scene/RenderScene.h"

namespace nova {

/**
 * Engine facade driven by the Android editor through JNI.
 * Owns the renderer and the latest render scene. All methods are called
 * on the GL thread.
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

    const RenderScene& scene() const { return scene_; }

private:
    GlesRenderer renderer_;
    RenderScene scene_;
    bool glReady_ = false;
};

} // namespace nova
