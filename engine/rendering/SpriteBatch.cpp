#include "rendering/SpriteBatch.h"

namespace nova {

namespace {

const char* kSpriteVertexShader = R"GLSL(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uViewProj;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 0.0, 1.0);
}
)GLSL";

const char* kSpriteFragmentShader = R"GLSL(#version 300 es
precision mediump float;
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, vUV) * vColor;
}
)GLSL";

constexpr size_t kVertexStrideFloats = 8; // x,y,u,v,r,g,b,a

} // namespace

SpriteBatch::~SpriteBatch() {
    shutdown();
}

bool SpriteBatch::initialize(std::string* outError) {
    if (initialized_) return true;
    if (!shader_.build(kSpriteVertexShader, kSpriteFragmentShader, outError)) {
        return false;
    }
    uViewProj_ = shader_.uniformLocation("uViewProj");
    uTex_ = shader_.uniformLocation("uTex");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(kMaxVertices * sizeof(SpriteVertex)),
                 nullptr, GL_DYNAMIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(kVertexStrideFloats * sizeof(float));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));
    glBindVertexArray(0);

    vertices_.reserve(kMaxVertices);
    initialized_ = true;
    return true;
}

void SpriteBatch::shutdown() {
    if (vbo_ != 0) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_ != 0) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    initialized_ = false;
}

void SpriteBatch::beginFrame(const Mat4& viewProj) {
    viewProj_ = viewProj;
    vertices_.clear();
    currentTexture_ = 0;
}

void SpriteBatch::drawSprite(const SpriteInstance& sprite, GLuint texture, GLuint whiteTexture) {
    const GLuint effective = texture != 0 ? texture : whiteTexture;
    if (vertices_.size() + 6 > kMaxVertices || (currentTexture_ != 0 && effective != currentTexture_)) {
        flush();
    }
    currentTexture_ = effective;

    float cx[4];
    float cy[4];
    computeQuadCorners(sprite.x, sprite.y, sprite.rotation,
                       sprite.scaleX, sprite.scaleY,
                       sprite.width, sprite.height, cx, cy);

    const float uvs[4][2] = { {0, 1}, {1, 1}, {1, 0}, {0, 0} };
    const int tris[6] = { 0, 1, 2, 0, 2, 3 };

    for (int i = 0; i < 6; ++i) {
        const int c = tris[i];
        SpriteVertex v;
        v.x = cx[c];
        v.y = cy[c];
        v.u = uvs[c][0];
        v.v = uvs[c][1];
        v.r = sprite.r;
        v.g = sprite.g;
        v.b = sprite.b;
        v.a = sprite.a;
        vertices_.push_back(v);
    }
}

void SpriteBatch::drawSelectionOutline(const SpriteInstance& sprite, GLuint whiteTexture) {
    // Flush quads first so the outline sits on top.
    flush();

    float cx[4];
    float cy[4];
    computeQuadCorners(sprite.x, sprite.y, sprite.rotation,
                       sprite.scaleX, sprite.scaleY,
                       sprite.width, sprite.height, cx, cy);

    SpriteVertex outline[5];
    for (int i = 0; i < 5; ++i) {
        const int c = i % 4;
        outline[i].x = cx[c];
        outline[i].y = cy[c];
        outline[i].u = 0.0f;
        outline[i].v = 0.0f;
        outline[i].r = 0.31f;
        outline[i].g = 0.76f;
        outline[i].b = 0.97f;
        outline[i].a = 1.0f;
    }

    shader_.use();
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj_.m);
    glUniform1i(uTex_, 0);
    glBindTexture(GL_TEXTURE_2D, whiteTexture);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(outline), outline);
    glDrawArrays(GL_LINE_STRIP, 0, 5);
    glBindVertexArray(0);
}

void SpriteBatch::endFrame() {
    flush();
}

void SpriteBatch::flush() {
    if (vertices_.empty()) return;
    shader_.use();
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj_.m);
    glUniform1i(uTex_, 0);
    glBindTexture(GL_TEXTURE_2D, currentTexture_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(vertices_.size() * sizeof(SpriteVertex)),
                    vertices_.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
    glBindVertexArray(0);
    vertices_.clear();
    currentTexture_ = 0;
}

} // namespace nova
