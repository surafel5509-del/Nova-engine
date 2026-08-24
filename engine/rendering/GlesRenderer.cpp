#include "rendering/GlesRenderer.h"

#include "core/Log.h"
#include "particles/ParticleSystem.h"

namespace nova {

namespace {

// Fullscreen triangle, positions already in NDC.
const char* kGridVertexShader = R"GLSL(#version 300 es
layout(location = 0) in vec2 aNdc;
out vec2 vNdc;
void main() {
    vNdc = aNdc;
    gl_Position = vec4(aNdc, 0.0, 1.0);
}
)GLSL";

// Infinite world-space grid: minor 1u, major 10u, colored axes, zoom fade.
const char* kGridFragmentShader = R"GLSL(#version 300 es
precision highp float;
in vec2 vNdc;
uniform vec2 uCenter;      // world-space camera center
uniform vec2 uHalfSizePx;  // viewport half size in pixels
uniform float uPpu;        // pixels per world unit
uniform float uVisible;
out vec4 fragColor;

float gridLine(vec2 p, float spacing) {
    vec2 q = p / spacing;
    vec2 g = abs(fract(q - 0.5) - 0.5) / fwidth(q);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main() {
    if (uVisible < 0.5) { fragColor = vec4(0.0); return; }
    vec2 world = uCenter + vNdc * uHalfSizePx / uPpu;

    float minor = gridLine(world, 1.0);
    float major = gridLine(world, 10.0);
    float minorFade = clamp((uPpu - 6.0) / 14.0, 0.0, 1.0);

    vec3 color = vec3(0.0);
    float alpha = 0.0;

    color += vec3(0.22, 0.25, 0.31) * minor * minorFade;
    alpha = max(alpha, minor * minorFade * 0.35);
    color += vec3(0.32, 0.36, 0.44) * major;
    alpha = max(alpha, major * 0.55);

    // Axes: X axis (y = 0) red, Y axis (x = 0) green.
    float axisX = 1.0 - min(abs(world.y) / max(fwidth(world.y) * 1.5, 1e-6), 1.0);
    float axisY = 1.0 - min(abs(world.x) / max(fwidth(world.x) * 1.5, 1.0e-6), 1.0);
    color = mix(color, vec3(0.75, 0.32, 0.32), axisX);
    alpha = max(alpha, axisX * 0.9);
    color = mix(color, vec3(0.35, 0.72, 0.38), axisY);
    alpha = max(alpha, axisY * 0.9);

    fragColor = vec4(color, alpha);
}
)GLSL";

constexpr float kBackgroundR = 0.078f;
constexpr float kBackgroundG = 0.090f;
constexpr float kBackgroundB = 0.110f;

} // namespace

bool GlesRenderer::initialize(std::string* outError) {
    if (initialized_) return true;

    std::string error;
    if (!spriteBatch_.initialize(&error)) {
        if (outError) *outError = "SpriteBatch: " + error;
        return false;
    }
    if (!gridShader_.build(kGridVertexShader, kGridFragmentShader, &error)) {
        if (outError) *outError = "Grid shader: " + error;
        return false;
    }
    gridUPpu_ = gridShader_.uniformLocation("uPpu");
    gridUCenter_ = gridShader_.uniformLocation("uCenter");
    gridUHalfSize_ = gridShader_.uniformLocation("uHalfSizePx");
    gridUVisible_ = gridShader_.uniformLocation("uVisible");

    const float fullscreenTriangle[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
    glGenVertexArrays(1, &gridVao_);
    glGenBuffers(1, &gridVbo_);
    glBindVertexArray(gridVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gridVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(fullscreenTriangle), fullscreenTriangle, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    if (!whiteTexture_.createSolid(255, 255, 255, 255)) {
        if (outError) *outError = "Failed to create fallback texture";
        return false;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    initialized_ = true;
    LOGI("GlesRenderer initialized: GL %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    return true;
}

void GlesRenderer::shutdown() {
    spriteBatch_.shutdown();
    if (gridVbo_ != 0) { glDeleteBuffers(1, &gridVbo_); gridVbo_ = 0; }
    if (gridVao_ != 0) { glDeleteVertexArrays(1, &gridVao_); gridVao_ = 0; }
    textures_.clear();
    initialized_ = false;
}

void GlesRenderer::setViewportSize(int width, int height) {
    viewportWidth_ = width > 0 ? width : 1;
    viewportHeight_ = height > 0 ? height : 1;
}

void GlesRenderer::uploadTexture(const std::string& key, const unsigned char* rgba, int width, int height) {
    Texture texture;
    if (texture.createFromRgba(rgba, width, height)) {
        textures_[key] = std::move(texture);
    } else {
        LOGE("Failed to upload texture '%s'", key.c_str());
    }
}

void GlesRenderer::removeTexture(const std::string& key) {
    textures_.erase(key);
}

Mat4 GlesRenderer::computeViewProj() const {
    const float halfW = viewportWidth_ * 0.5f;
    const float halfH = viewportHeight_ * 0.5f;
    const Mat4 proj = Mat4::ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);
    const Mat4 view = Mat4::multiply(
        Mat4::scaling(camera_.pixelsPerUnit, camera_.pixelsPerUnit, 1.0f),
        Mat4::translation(-camera_.centerX, -camera_.centerY, 0.0f));
    return Mat4::multiply(proj, view);
}

void GlesRenderer::drawFrame(const RenderScene& scene) {
    drawFrame(scene, nullptr);
}

void GlesRenderer::drawFrame(const RenderScene& scene, const ParticleSystem* particles) {
    if (!initialized_) return;
    glViewport(0, 0, viewportWidth_, viewportHeight_);
    glClearColor(clearR_, clearG_, clearB_, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (gridVisible_ && !useGameCamera_) drawGrid();
    const Mat4 viewProj = computeViewProj();
    spriteBatch_.beginFrame(viewProj);
    drawTilemaps(scene);
    drawSprites(scene);
    if (particles) drawParticles(*particles);
    drawUi(scene);
    spriteBatch_.endFrame();
    for (const SpriteInstance& sprite : scene.sprites) {
        if (sprite.selected) {
            spriteBatch_.drawSelectionOutline(sprite, whiteTexture_.id());
        }
    }
    if (showGameCamera_ && scene.gameCamera.present && !useGameCamera_) {
        drawGameCameraFrame(scene, viewProj);
    }
    if (showPhysicsDebug_) {
        drawPhysicsDebug(scene, viewProj);
    }
    lastDrawCalls_ = spriteBatch_.drawCalls();
}

void GlesRenderer::drawGrid() {
    gridShader_.use();
    glUniform1f(gridUPpu_, camera_.pixelsPerUnit);
    glUniform2f(gridUCenter_, camera_.centerX, camera_.centerY);
    glUniform2f(gridUHalfSize_, viewportWidth_ * 0.5f, viewportHeight_ * 0.5f);
    glUniform1f(gridUVisible_, gridVisible_ ? 1.0f : 0.0f);
    glBindVertexArray(gridVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void GlesRenderer::drawTilemaps(const RenderScene& scene) {
    for (const TilemapRecord& t : scene.tilemaps) {
        GLuint texture = 0;
        if (!t.tileset.empty()) {
            auto it = textures_.find(t.tileset);
            if (it != textures_.end()) texture = it->second.id();
        }
        const int cols = t.cols;
        const int rows = t.rows;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const int idx = t.tiles[static_cast<size_t>(row) * cols + col];
                if (idx < 0) continue;
                // Draw via the sprite batcher as one atlas frame.
                SpriteInstance cell;
                cell.x = t.x + (col + 0.5f) * t.tileSize;
                cell.y = t.y + (row + 0.5f) * t.tileSize;
                cell.width = t.tileSize;
                cell.height = t.tileSize;
                cell.frameCols = t.tilesetCols > 0 ? t.tilesetCols : 1;
                cell.frameRows = t.tilesetRows > 0 ? t.tilesetRows : 1;
                cell.frameIndex = idx;
                spriteBatch_.drawSprite(cell, texture, whiteTexture_.id());
            }
        }
    }
}

void GlesRenderer::drawSprites(const RenderScene& scene) {
    for (const SpriteInstance& sprite : scene.sprites) {
        GLuint texture = 0;
        if (!sprite.texture.empty()) {
            auto it = textures_.find(sprite.texture);
            if (it != textures_.end()) texture = it->second.id();
        }
        // Parallax: shift the sprite toward the camera center so layers with
        // factor < 1 appear to move slower (background) than the world.
        SpriteInstance s = sprite;
        if (sprite.parallaxFactor < 0.999f) {
            s.x = camera_.centerX + (sprite.x - camera_.centerX) * sprite.parallaxFactor;
            s.y = camera_.centerY + (sprite.y - camera_.centerY) * sprite.parallaxFactor;
        }
        spriteBatch_.drawSprite(s, texture, whiteTexture_.id());
    }
}

void GlesRenderer::drawParticles(const ParticleSystem& particles) {
    for (size_t i = 0; i < particles.emitterCount(); ++i) {
        const ParticleEmitterRecord& e = particles.emitters()[i];
        GLuint texture = 0;
        if (!e.texture.empty()) {
            auto it = textures_.find(e.texture);
            if (it != textures_.end()) texture = it->second.id();
        }
        for (const Particle& p : particles.particles(i)) {
            const float t = p.age / p.lifetime;
            SpriteInstance s;
            s.x = p.x;
            s.y = p.y;
            s.width = p.startSize + (p.endSize - p.startSize) * t;
            s.height = s.width;
            s.r = p.r;
            s.g = p.g;
            s.b = p.b;
            s.a = 1.0f - t;   // fade out over lifetime
            spriteBatch_.drawSprite(s, texture, whiteTexture_.id());
        }
    }
}

void GlesRenderer::drawUi(const RenderScene& scene) {
    for (const UiElementRecord& u : scene.uiElements) {
        // Background panel/button.
        SpriteInstance bg;
        bg.x = camera_.centerX + u.offsetX;
        bg.y = camera_.centerY + u.offsetY;
        bg.width = u.width;
        bg.height = u.height;
        bg.r = u.r;
        bg.g = u.g;
        bg.b = u.b;
        bg.a = u.a;
        spriteBatch_.drawSprite(bg, 0, whiteTexture_.id());

        // Text overlay (pre-rendered by the Kotlin side into a texture).
        if (!u.textKey.empty()) {
            auto it = textures_.find(u.textKey);
            if (it != textures_.end()) {
                SpriteInstance text;
                text.x = bg.x;
                text.y = bg.y;
                text.width = u.width * 0.92f;
                text.height = u.height * 0.82f;
                spriteBatch_.drawSprite(text, it->second.id(), whiteTexture_.id());
            }
        }
    }
}

void GlesRenderer::drawLineBox(float cx, float cy, float halfW, float halfH, float rotationDeg,
                               float r, float g, float b, float a, const Mat4& viewProj) {
    (void)viewProj; // batch already holds the current viewProj from beginFrame
    spriteBatch_.drawLineBox(cx, cy, halfW, halfH, rotationDeg, r, g, b, a, whiteTexture_.id());
}

void GlesRenderer::drawGameCameraFrame(const RenderScene& scene, const Mat4& viewProj) {
    const GameCamera& c = scene.gameCamera;
    drawLineBox(c.x, c.y, c.width * 0.5f, c.height * 0.5f, 0.0f,
                0.95f, 0.85f, 0.30f, 1.0f, viewProj);
}

void GlesRenderer::drawPhysicsDebug(const RenderScene& scene, const Mat4& viewProj) {
    for (const BodyRecord& b : scene.bodies) {
        // Green = static, orange = dynamic, blue = kinematic.
        float r = 0.3f, g = 0.9f, bl = 0.4f;
        if (b.bodyType == 1) { r = 0.95f; g = 0.6f; bl = 0.2f; }
        else if (b.bodyType == 2) { r = 0.3f; g = 0.6f; bl = 0.95f; }
        drawLineBox(b.x, b.y, b.halfW, b.halfH, 0.0f, r, g, bl, 0.9f, viewProj);
    }
}

} // namespace nova
