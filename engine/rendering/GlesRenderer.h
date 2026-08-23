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

    int viewportWidth() const { return viewportWidth_; }
    int viewportHeight() const { return viewportHeight_; }

private:
    Mat4 computeViewProj() const;
    void drawGrid();
    void drawSprites(const RenderScene& scene);

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

    GLint gridUPpu_ = -1;
    GLint gridUCenter_ = -1;
    GLint gridUHalfSize_ = -1;
    GLint gridUVisible_ = -1;
};

} // namespace nova
