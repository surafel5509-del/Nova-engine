// Host-side tests for 3D meshes, 3D parsing, and the animation sampler.
#include <cmath>
#include <cstdio>

#include "math/Mat4.h"
#include "rendering/Mesh3D.h"
#include "scene/AnimationSampler.h"
#include "scene/RenderScene.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* name) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", name);
    }
}

bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) < eps;
}

void testMeshCube() {
    const auto cube = nova::MeshBuilder::cube();
    check(cube.size() == 36, "cube has 36 vertices");
    bool hasTopNormal = false;
    for (const auto& v : cube) {
        if (v.ny == 1.0f) hasTopNormal = true;
    }
    check(hasTopNormal, "cube has top face normals");
}

void testMeshCylinder() {
    const auto cyl = nova::MeshBuilder::cylinder(8);
    // 8 segments * (6 side + 3 top + 3 bottom) = 96 vertices.
    check(cyl.size() == 96, "cylinder vertex count scales with segments");
}

void testMeshPlane() {
    const auto plane = nova::MeshBuilder::plane(10.0f);
    check(plane.size() == 6, "plane has 6 vertices");
    check(plane[0].ny == 1.0f, "plane normal is +Y");
}

void testMat4Perspective() {
    const nova::Mat4 p = nova::Mat4::perspective(90.0f, 1.0f, 0.1f, 100.0f);
    check(nearly(p.m[0], 1.0f) && nearly(p.m[5], 1.0f), "perspective 90deg f=1");
    check(nearly(p.m[11], -1.0f), "perspective w=-z");
}

void testMat4LookAt() {
    // Camera at origin looking down -Z: identity view.
    const nova::Mat4 v = nova::Mat4::lookAt(0, 0, 0, 0, 0, -1, 0, 1, 0);
    float x, y, z;
    v.transformPoint3(1, 2, 3, x, y, z);
    check(nearly(x, 1.0f) && nearly(y, 2.0f) && nearly(z, 3.0f), "lookAt -Z is identity");
}

void testParse3DScene() {
    const std::string json = R"({
        "version": 1, "mode3d": true,
        "sprites": [],
        "objects3d": [
            {"id": "box", "shape": "cube", "x": 1, "y": 0.5, "z": -2,
             "ry": 45, "sx": 2, "r": 0.8, "g": 0.2, "b": 0.3}
        ],
        "light": {"dirY": -1, "ambientR": 0.2}
    })";
    nova::RenderScene scene;
    std::string error;
    check(scene.parseFrom(json, &error), "parse 3D scene");
    check(scene.mode3d, "mode3d flag parsed");
    check(scene.objects3d.size() == 1, "one 3D object");
    check(scene.objects3d[0].shape == "cube", "object shape");
    check(nearly(scene.objects3d[0].z, -2.0f), "object z");
    check(nearly(scene.objects3d[0].ry, 45.0f), "object rotation y");
    check(scene.light.present, "light present");
    check(nearly(scene.light.ambientR, 0.2f), "ambient parsed");
}

void testAnimationSampling() {
    nova::RenderScene scene;
    nova::SpriteInstance sprite;
    sprite.id = "hero";
    scene.sprites.push_back(sprite);

    nova::AnimationTrack track;
    track.entityId = "hero";
    track.property = "x";
    track.keys.push_back({0.0f, 0.0f});
    track.keys.push_back({2.0f, 10.0f});
    scene.animations.push_back(track);

    nova::AnimationSampler::apply(scene, 1.0f);
    check(nearly(scene.sprites[0].x, 5.0f), "animation lerp at midpoint");

    nova::AnimationSampler::apply(scene, 2.5f);
    // Loop duration 2s: t=2.5 wraps to 0.5s -> x = 2.5.
    check(nearly(scene.sprites[0].x, 2.5f), "looped animation wraps");
}

void testAnimationRotationAndScale() {
    nova::RenderScene scene;
    nova::SpriteInstance sprite;
    sprite.id = "s";
    scene.sprites.push_back(sprite);
    nova::AnimationTrack rot;
    rot.entityId = "s";
    rot.property = "rotation";
    rot.loop = false;
    rot.keys.push_back({0.0f, 0.0f});
    rot.keys.push_back({1.0f, 90.0f});
    scene.animations.push_back(rot);
    nova::AnimationSampler::apply(scene, 5.0f);  // past end, non-looping -> clamped
    check(nearly(scene.sprites[0].rotation, 90.0f), "non-looping animation clamps");
}

} // namespace

int run3DTests() {
    testMeshCube();
    testMeshCylinder();
    testMeshPlane();
    testMat4Perspective();
    testMat4LookAt();
    testParse3DScene();
    testAnimationSampling();
    testAnimationRotationAndScale();
    std::printf("3d/animation: %d checks, %d failures\n", checks, failures);
    return failures;
}
