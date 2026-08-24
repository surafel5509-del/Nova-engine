#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "math/Mat4.h"
#include "rendering/Shader.h"
#include "rendering/SpriteBatch.h"
#include "rendering/Texture.h"
#include "scene/RenderScene.h"

namespace nova {

class ParticleSystem;

struct CameraState {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float pixelsPerUnit = 100.0f;
};

/**
 * OpenGL ES 3 editor renderer: infinite reference grid, batched sprites,
 * selection outlines. Deliberately small — a Vulkan backend can later sit
 * next to this behind the same Engine-facing interface.
 */
class GlesRenderer {
public:
    GlesRenderer() = default;

    /** Requires a current GL context. */
    bool initialize(std::string* outError = nullptr);
    void shutdown();

    void setViewportSize(int width, int height);
    void setCamera(const CameraState& camera) { camera_ = camera; }
    void setGridVisible(bool visible) { gridVisible_ = visible; }

    void uploadTexture(const std::string& key, const unsigned char* rgba, int width, int height);
    void removeTexture(const std::string& key);

    void drawFrame(const RenderScene& scene);
    void drawFrame(const RenderScene& scene, const ParticleSystem* particles);

    int viewportWidth() const { return viewportWidth_; }
    int viewportHeight() const { return viewportHeight_; }

    void setShowGameCamera(bool show) { showGameCamera_ = show; }
    void setShowPhysicsDebug(bool show) { showPhysicsDebug_ = show; }
    void setUseGameCamera(bool use) { useGameCamera_ = use; }
    void setClearColor(float r, float g, float b) { clearR_ = r; clearG_ = g; clearB_ = b; }

    /** GL draw calls issued during the last frame (profiler). */
    int lastDrawCalls() const { return lastDrawCalls_; }

private:
    Mat4 computeViewProj() const;
    void drawGrid();
    void drawSprites(const RenderScene& scene);
    void drawTilemaps(const RenderScene& scene);
    void drawParticles(const ParticleSystem& particles);
    void drawUi(const RenderScene& scene);
    void drawLineBox(float cx, float cy, float halfW, float halfH, float rotationDeg,
                     float r, float g, float b, float a, const Mat4& viewProj);
    void drawGameCameraFrame(const RenderScene& scene, const Mat4& viewProj);
    void drawPhysicsDebug(const RenderScene& scene, const Mat4& viewProj);

    SpriteBatch spriteBatch_;
    Shader gridShader_;
    GLuint gridVao_ = 0;
    GLuint gridVbo_ = 0;
    Texture whiteTexture_;
    std::unordered_map<std::string, Texture> textures_;

    CameraState camera_;
    int viewportWidth_ = 1;
    int viewportHeight_ = 1;
    bool gridVisible_ = true;
    bool initialized_ = false;
    bool showGameCamera_ = true;
    bool showPhysicsDebug_ = false;
    bool useGameCamera_ = false;
    float clearR_ = 0.078f;
    float clearG_ = 0.090f;
    float clearB_ = 0.110f;
    int lastDrawCalls_ = 0;

    GLint gridUPpu_ = -1;
    GLint gridUCenter_ = -1;
    GLint gridUHalfSize_ = -1;
    GLint gridUVisible_ = -1;
};

} // namespace nova
