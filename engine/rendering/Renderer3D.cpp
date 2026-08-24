#include "rendering/Renderer3D.h"

#include <GLES3/gl3.h>
#include <cmath>

#include "core/Log.h"

namespace nova {

namespace {

const char* kMesh3DVertexShader = R"GLSL(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMatrix;
out vec3 vNormal;
out vec3 vWorldPos;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalize(uNormalMatrix * aNormal);
    gl_Position = uViewProj * world;
}
)GLSL";

const char* kMesh3DFragmentShader = R"GLSL(#version 300 es
precision mediump float;
in vec3 vNormal;
in vec3 vWorldPos;
uniform vec4 uColor;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAmbient;
out vec4 fragColor;
void main() {
    float diff = max(dot(normalize(vNormal), normalize(-uLightDir)), 0.0);
    vec3 lit = uColor.rgb * (uAmbient + uLightColor * diff);
    fragColor = vec4(lit, uColor.a);
}
)GLSL";

const char* kLine3DVertexShader = R"GLSL(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)GLSL";

const char* kLine3DFragmentShader = R"GLSL(#version 300 es
precision mediump float;
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)GLSL";

struct LineVertex { float x, y, z, r, g, b, a; };

} // namespace

Renderer3D::~Renderer3D() {
    shutdown();
}

bool Renderer3D::initialize(std::string* outError) {
    if (initialized_) return true;
    if (!shader3d_.build(kMesh3DVertexShader, kMesh3DFragmentShader, outError)) return false;
    if (!lineShader_.build(kLine3DVertexShader, kLine3DFragmentShader, outError)) return false;

    glGenVertexArrays(1, &vao3d_);
    glGenVertexArrays(1, &vaoLine_);
    glGenBuffers(1, &lineVbo_);

    initialized_ = true;
    LOGI("Renderer3D initialized");
    return true;
}

void Renderer3D::shutdown() {
    for (auto& [shape, vbo] : shapeVbos_) glDeleteBuffers(1, &vbo);
    shapeVbos_.clear();
    shapeVertexCounts_.clear();
    if (lineVbo_ != 0) { glDeleteBuffers(1, &lineVbo_); lineVbo_ = 0; }
    if (vao3d_ != 0) { glDeleteVertexArrays(1, &vao3d_); vao3d_ = 0; }
    if (vaoLine_ != 0) { glDeleteVertexArrays(1, &vaoLine_); vaoLine_ = 0; }
    initialized_ = false;
}

void Renderer3D::setCamera(float yawDeg, float pitchDeg, float distance,
                           float targetX, float targetY, float targetZ, float fovDeg) {
    yaw_ = yawDeg;
    pitch_ = pitchDeg;
    distance_ = distance;
    targetX_ = targetX;
    targetY_ = targetY;
    targetZ_ = targetZ;
    fov_ = fovDeg;
}

Mat4 Renderer3D::viewProj() const {
    const float aspect = viewportHeight_ > 0
        ? static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_)
        : 1.0f;
    const float yawRad = yaw_ * 3.14159265f / 180.0f;
    const float pitchRad = pitch_ * 3.14159265f / 180.0f;
    const float ex = targetX_ + distance_ * std::cos(pitchRad) * std::sin(yawRad);
    const float ey = targetY_ + distance_ * std::sin(pitchRad);
    const float ez = targetZ_ + distance_ * std::cos(pitchRad) * std::cos(yawRad);
    const Mat4 view = Mat4::lookAt(ex, ey, ez, targetX_, targetY_, targetZ_, 0.0f, 1.0f, 0.0f);
    const Mat4 proj = Mat4::perspective(fov_, aspect, 0.1f, 200.0f);
    return Mat4::multiply(proj, view);
}

GLuint Renderer3D::vboForShape(const std::string& shape, int* outVertexCount) {
    auto it = shapeVbos_.find(shape);
    if (it != shapeVbos_.end()) {
        *outVertexCount = shapeVertexCounts_[shape];
        return it->second;
    }
    const auto vertices = MeshBuilder::forShape(shape);
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(MeshVertex3D)),
                 vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    shapeVbos_[shape] = vbo;
    shapeVertexCounts_[shape] = static_cast<int>(vertices.size());
    *outVertexCount = static_cast<int>(vertices.size());
    return vbo;
}

Mat4 Renderer3D::modelMatrix(const Object3DRecord& obj) const {
    Mat4 m = Mat4::translation(obj.x, obj.y, obj.z);
    m = Mat4::multiply(m, Mat4::rotationY(obj.ry));
    m = Mat4::multiply(m, Mat4::rotationX(obj.rx));
    m = Mat4::multiply(m, Mat4::rotationZ(obj.rz));
    m = Mat4::multiply(m, Mat4::scaling(obj.sx, obj.sy, obj.sz));
    return m;
}

void Renderer3D::drawFrame(const RenderScene& scene) {
    if (!initialized_) return;
    glViewport(0, 0, viewportWidth_, viewportHeight_);
    glClearColor(clearR_, clearG_, clearB_, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    const Mat4 vp = viewProj();

    drawGrid();
    drawAxes();

    // Light from the scene (or a sensible default).
    float lx = -0.4f, ly = -1.0f, lz = -0.3f;
    float lr = 0.95f, lg = 0.93f, lb = 0.85f;
    float ar = 0.18f, ag = 0.18f, ab = 0.20f;
    if (scene.light.present) {
        lx = scene.light.dirX; ly = scene.light.dirY; lz = scene.light.dirZ;
        lr = scene.light.r; lg = scene.light.g; lb = scene.light.b;
        ar = scene.light.ambientR; ag = scene.light.ambientG; ab = scene.light.ambientB;
    }
    shader3d_.use();
    glUniformMatrix4fv(shader3d_.uniformLocation("uViewProj"), 1, GL_FALSE, vp.m);
    glUniform3f(shader3d_.uniformLocation("uLightDir"), lx, ly, lz);
    glUniform3f(shader3d_.uniformLocation("uLightColor"), lr, lg, lb);
    glUniform3f(shader3d_.uniformLocation("uAmbient"), ar, ag, ab);

    glBindVertexArray(vao3d_);
    drawObjects(scene);
    glBindVertexArray(0);
    glDisable(GL_DEPTH_TEST);
}

void Renderer3D::drawObjects(const RenderScene& scene) {
    shader3d_.use();
    const Mat4 vp = viewProj();
    glUniformMatrix4fv(shader3d_.uniformLocation("uViewProj"), 1, GL_FALSE, vp.m);

    // Light from the scene (or a sensible default).
    float lx = -0.4f, ly = -1.0f, lz = -0.3f;
    float lr = 0.95f, lg = 0.93f, lb = 0.85f;
    float ar = 0.18f, ag = 0.18f, ab = 0.20f;
    if (scene.light.present) {
        lx = scene.light.dirX; ly = scene.light.dirY; lz = scene.light.dirZ;
        lr = scene.light.r; lg = scene.light.g; lb = scene.light.b;
        ar = scene.light.ambientR; ag = scene.light.ambientG; ab = scene.light.ambientB;
    }
    glUniform3f(shader3d_.uniformLocation("uLightDir"), lx, ly, lz);
    glUniform3f(shader3d_.uniformLocation("uLightColor"), lr, lg, lb);
    glUniform3f(shader3d_.uniformLocation("uAmbient"), ar, ag, ab);

    for (const Object3DRecord& obj : scene.objects3d) {
        int vertexCount = 0;
        const GLuint vbo = vboForShape(obj.shape, &vertexCount);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex3D), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex3D),
                              (void*)(3 * sizeof(float)));

        const Mat4 model = modelMatrix(obj);
        glUniformMatrix4fv(shader3d_.uniformLocation("uModel"), 1, GL_FALSE, model.m);
        // Normal matrix = upper-left 3x3 of model (uniform-scale primitives).
        const float normal3[9] = {
            model.m[0], model.m[1], model.m[2],
            model.m[4], model.m[5], model.m[6],
            model.m[8], model.m[9], model.m[10],
        };
        glUniformMatrix3fv(shader3d_.uniformLocation("uNormalMatrix"), 1, GL_FALSE, normal3);
        glUniform4f(shader3d_.uniformLocation("uColor"), obj.r, obj.g, obj.b, obj.a);

        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        if (obj.selected) drawSelectedOutline(obj);
    }
}

void Renderer3D::drawSelectedOutline(const Object3DRecord& obj) {
    // Selection highlight: redraw the mesh slightly larger, unlit yellow,
    // with depth test off so it glows around the object (no glPolygonMode in ES).
    glUniform4f(shader3d_.uniformLocation("uColor"), 1.0f, 0.85f, 0.2f, 0.35f);
    glUniform3f(shader3d_.uniformLocation("uAmbient"), 1.0f, 1.0f, 1.0f);
    Mat4 model = modelMatrix(obj);
    model = Mat4::multiply(model, Mat4::scaling(1.04f, 1.04f, 1.04f));
    glUniformMatrix4fv(shader3d_.uniformLocation("uModel"), 1, GL_FALSE, model.m);
    int vertexCount = 0;
    vboForShape(obj.shape, &vertexCount);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glEnable(GL_DEPTH_TEST);
}

void Renderer3D::drawGrid() {
    std::vector<LineVertex> lines;
    lines.reserve(2 * 21 * 2);
    const float half = 10.0f;
    for (int i = -10; i <= 10; ++i) {
        const float p = static_cast<float>(i);
        const float shade = (i == 0) ? 0.45f : 0.22f;
        lines.push_back({p, 0, -half, shade, shade, shade, 1});
        lines.push_back({p, 0, half, shade, shade, shade, 1});
        lines.push_back({-half, 0, p, shade, shade, shade, 1});
        lines.push_back({half, 0, p, shade, shade, shade, 1});
    }

    lineShader_.use();
    const Mat4 vp = viewProj();
    glUniformMatrix4fv(lineShader_.uniformLocation("uViewProj"), 1, GL_FALSE, vp.m);
    glBindVertexArray(vaoLine_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(LineVertex)),
                 lines.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)(3 * sizeof(float)));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
    glBindVertexArray(0);
}

void Renderer3D::drawAxes() {
    const LineVertex axes[] = {
        {0, 0, 0, 1, 0.3f, 0.3f, 1}, {5, 0, 0, 1, 0.3f, 0.3f, 1},   // X red
        {0, 0, 0, 0.3f, 1, 0.3f, 1}, {0, 5, 0, 0.3f, 1, 0.3f, 1},   // Y green
        {0, 0, 0, 0.3f, 0.5f, 1, 1}, {0, 0, 5, 0.3f, 0.5f, 1, 1},   // Z blue
    };
    lineShader_.use();
    const Mat4 vp = viewProj();
    glUniformMatrix4fv(lineShader_.uniformLocation("uViewProj"), 1, GL_FALSE, vp.m);
    glBindVertexArray(vaoLine_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axes), axes, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex), (void*)(3 * sizeof(float)));
    glLineWidth(3.0f);
    glDrawArrays(GL_LINES, 0, 6);
    glLineWidth(1.0f);
    glBindVertexArray(0);
}

} // namespace nova
