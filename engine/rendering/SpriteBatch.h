#pragma once

#include <GLES3/gl3.h>
#include <vector>

#include "math/Mat4.h"
#include "rendering/Geometry.h"
#include "rendering/Shader.h"
#include "scene/RenderScene.h"

namespace nova {

/**
 * Dynamic sprite batcher: accumulates textured quads (6 verts each) and
 * flushes per texture. Selection outlines are drawn as line strips.
 */
class SpriteBatch {
public:
    SpriteBatch() = default;
    ~SpriteBatch();

    bool initialize(std::string* outError = nullptr);
    void shutdown();

    void beginFrame(const Mat4& viewProj);
    /** Draws one sprite with [texture]; texture 0 binds the white fallback. */
    void drawSprite(const SpriteInstance& sprite, GLuint texture, GLuint whiteTexture);
    void drawSelectionOutline(const SpriteInstance& sprite, GLuint whiteTexture);
    void endFrame();

private:
    void flush();

    Shader shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    Mat4 viewProj_ = Mat4::identity();
    std::vector<SpriteVertex> vertices_;
    GLuint currentTexture_ = 0;   // texture the buffered quads belong to
    GLint uViewProj_ = -1;
    GLint uTex_ = -1;
    bool initialized_ = false;
    static constexpr size_t kMaxVertices = 6 * 4096; // 4096 quads per flush
};

} // namespace nova
