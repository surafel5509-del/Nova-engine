#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "math/Mat4.h"
#include "rendering/Mesh3D.h"
#include "rendering/Shader.h"
#include "scene/RenderScene.h"

namespace nova {

/**
 * 3D rendering path: perspective camera (orbit), lit primitive meshes with
 * directional light + ambient, ground grid + world axes, object selection
 * outline. Uses its own VBO cache per shape.
 */
class Renderer3D {
public:
    Renderer3D() = default;
    ~Renderer3D();

    bool initialize(std::string* outError = nullptr);
    void shutdown();

    void setViewportSize(int width, int height) {
        viewportWidth_ = width;
        viewportHeight_ = height;
    }

    /** Orbit camera state from the editor/runtime. */
    void setCamera(float yawDeg, float pitchDeg, float distance,
                   float targetX, float targetY, float targetZ, float fovDeg);

    void drawFrame(const RenderScene& scene);

private:
    void drawGrid();
    void drawAxes();
    void drawObjects(const RenderScene& scene);
    void drawSelectedOutline(const Object3DRecord& obj);
    GLuint vboForShape(const std::string& shape, int* outVertexCount);
    Mat4 viewProj() const;
    Mat4 modelMatrix(const Object3DRecord& obj) const;

    Shader shader3d_;
    Shader lineShader_;
    GLuint vao3d_ = 0;
    GLuint vaoLine_ = 0;
    GLuint lineVbo_ = 0;
    std::unordered_map<std::string, GLuint> shapeVbos_;
    std::unordered_map<std::string, int> shapeVertexCounts_;

    int viewportWidth_ = 0;
    int viewportHeight_ = 0;

    float yaw_ = 45.0f;
    float pitch_ = 30.0f;
    float distance_ = 12.0f;
    float targetX_ = 0.0f;
    float targetY_ = 0.0f;
    float targetZ_ = 0.0f;
    float fov_ = 50.0f;

    float clearR_ = 0.08f;
    float clearG_ = 0.09f;
    float clearB_ = 0.12f;

    bool initialized_ = false;
};

} // namespace nova
