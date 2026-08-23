// Host-side engine unit tests: scene parsing, matrices, quad geometry.
#include <cmath>
#include <cstdio>
#include <string>

#include "math/Mat4.h"
#include "rendering/Geometry.h"
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

bool nearly(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

void testSceneParsing() {
    const std::string json = R"({
        "version": 1,
        "sprites": [
            {"id": "a", "x": 1.5, "y": -2.0, "rotation": 45.0, "scaleX": 2.0, "scaleY": 0.5,
             "width": 3.0, "height": 1.0, "r": 0.25, "g": 0.5, "b": 0.75, "a": 0.9,
             "texture": "assets/textures/hero.png", "selected": true},
            {"id": "b"}
        ]
    })";
    nova::RenderScene scene;
    std::string error;
    check(scene.parseFrom(json, &error), "parse valid scene");
    check(scene.sprites.size() == 2, "two sprites parsed");
    check(scene.sprites[0].id == "a", "sprite id");
    check(nearly(scene.sprites[0].x, 1.5f), "sprite x");
    check(nearly(scene.sprites[0].rotation, 45.0f), "sprite rotation");
    check(nearly(scene.sprites[0].scaleX, 2.0f), "sprite scaleX");
    check(nearly(scene.sprites[0].a, 0.9f), "sprite alpha");
    check(scene.sprites[0].texture == "assets/textures/hero.png", "sprite texture");
    check(scene.sprites[0].selected, "sprite selected");
    // Defaults for missing fields.
    check(scene.sprites[1].width == 1.0f && scene.sprites[1].a == 1.0f, "defaults applied");
    check(!scene.sprites[1].selected, "selected defaults to false");
}

void testSceneParsingErrors() {
    nova::RenderScene scene;
    std::string error;
    check(!scene.parseFrom("{not json", &error), "reject invalid json");
    check(!error.empty(), "error message provided");
    check(!scene.parseFrom("{\"version\":1}", &error), "reject missing sprites array");
    check(scene.sprites.empty(), "scene untouched on failure");
}

void testMat4() {
    const nova::Mat4 id = nova::Mat4::identity();
    float x = 0, y = 0;
    id.transformPoint(3.0f, -7.0f, x, y);
    check(nearly(x, 3.0f) && nearly(y, -7.0f), "identity transform");

    const nova::Mat4 t = nova::Mat4::translation(2.0f, 3.0f, 0.0f);
    const nova::Mat4 s = nova::Mat4::scaling(2.0f, 4.0f, 1.0f);
    const nova::Mat4 m = nova::Mat4::multiply(t, s); // scale first, then translate
    m.transformPoint(1.0f, 1.0f, x, y);
    check(nearly(x, 4.0f) && nearly(y, 7.0f), "translate * scale");

    const nova::Mat4 o = nova::Mat4::ortho(-10.0f, 10.0f, -5.0f, 5.0f, -1.0f, 1.0f);
    o.transformPoint(10.0f, 5.0f, x, y);
    check(nearly(x, 1.0f) && nearly(y, 1.0f), "ortho maps right/top to +1");
    o.transformPoint(-10.0f, -5.0f, x, y);
    check(nearly(x, -1.0f) && nearly(y, -1.0f), "ortho maps left/bottom to -1");
}

void testQuadCorners() {
    float cx[4];
    float cy[4];
    // No rotation: axis-aligned box around origin.
    nova::computeQuadCorners(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 2.0f, 4.0f, cx, cy);
    check(nearly(cx[0], -1.0f) && nearly(cy[0], -2.0f), "corner BL");
    check(nearly(cx[2], 1.0f) && nearly(cy[2], 2.0f), "corner TR");

    // 90-degree rotation swaps extents.
    nova::computeQuadCorners(0.0f, 0.0f, 90.0f, 1.0f, 1.0f, 2.0f, 4.0f, cx, cy);
    check(nearly(cx[0], 2.0f) && nearly(cy[0], -1.0f), "rotated corner BL");

    // Scale and translation compose.
    nova::computeQuadCorners(5.0f, -3.0f, 0.0f, 2.0f, 0.5f, 2.0f, 4.0f, cx, cy);
    check(nearly(cx[0], 3.0f) && nearly(cy[0], -4.0f), "scaled+translated corner");
}

} // namespace

int runPhysicsTests();

int main() {
    testSceneParsing();
    testSceneParsingErrors();
    testMat4();
    testQuadCorners();

    std::printf("scene/math: %d checks, %d failures\n", checks, failures);
    const int physicsFailures = runPhysicsTests();
    const int total = failures + physicsFailures;
    std::printf("TOTAL: %d failures\n", total);
    return total == 0 ? 0 : 1;
}
