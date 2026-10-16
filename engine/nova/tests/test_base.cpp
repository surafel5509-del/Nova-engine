// Foundation tests: math, JSON, filesystem, RNG, time.
#include "tests/TestHarness.h"

#include <set>

#include "base/FileSystem.h"
#include "base/Json.h"
#include "base/Random.h"
#include "base/Time.h"
#include "math/Math.h"

using namespace nova;

// ---------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------

NOVA_TEST(Math, Vec2Basics) {
    const Vec2 a{3, 4};
    CHECK_NEAR(a.length(), 5.0, 1e-5);
    CHECK_NEAR(a.normalized().length(), 1.0, 1e-5);
    CHECK_NEAR(Vec2{1, 0}.dot(Vec2{0, 1}), 0.0, 1e-6);
    CHECK_NEAR(Vec2{1, 0}.cross(Vec2{0, 1}), 1.0, 1e-6);
    // Perpendicular is a 90 degree CCW rotation.
    const Vec2 p = Vec2{1, 0}.perpendicular();
    CHECK_NEAR_VEC3(Vec3(p.x, p.y, 0), Vec3(0, 1, 0), 1e-6);
    CHECK_NEAR(Vec2{5, 0}.clamped(3).length(), 3.0, 1e-5);
    CHECK_NEAR(Vec2{1, 1}.clamped(10).length(), std::sqrt(2.0), 1e-5);
}

NOVA_TEST(Math, Vec3CrossAndDot) {
    const Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
    CHECK_NEAR(Vec3::cross(x, y).z, 1.0, 1e-6);
    CHECK_NEAR(x.cross(y).dot(z), 1.0, 1e-6);
    CHECK_NEAR(x.dot(y), 0.0, 1e-6);
    // Anticommutativity: a×b == -(b×a)
    CHECK_NEAR_VEC3(x.cross(y), y.cross(x) * -1.0f, 1e-6);
    // A perpendicular is orthogonal and unit length.
    const Vec3 perp = Vec3{0.3f, -0.7f, 0.2f}.anyPerpendicular();
    CHECK_NEAR(perp.length(), 1.0, 1e-4);
    CHECK_NEAR(perp.dot(Vec3{0.3f, -0.7f, 0.2f}), 0.0, 1e-4);
}

NOVA_TEST(Math, QuatRotation) {
    const Quat q = Quat::fromAngleAxis(90.0f, Vec3{0, 0, 1});
    const Vec3 rotated = q.rotate(Vec3{1, 0, 0});
    CHECK_NEAR_VEC3(rotated, Vec3(0, 1, 0), 1e-4);
    // Rotating by +90 twice gives 180.
    CHECK_NEAR_VEC3((q * q).rotate(Vec3{1, 0, 0}), Vec3(-1, 0, 0), 1e-4);
    // Inverse undoes the rotation.
    CHECK_NEAR_VEC3(q.inverse().rotate(q.rotate(Vec3{0.4f, 0.2f, -0.9f})),
                    Vec3(0.4f, 0.2f, -0.9f), 1e-4);
}

NOVA_TEST(Math, QuatEulerRoundTrip) {
    const Vec3 euler{23.0f, -47.0f, 88.0f};
    const Quat q = Quat::fromEuler(euler.x, euler.y, euler.z);
    float p, y, r;
    q.toEuler(p, y, r);
    CHECK_NEAR(p, euler.x, 1e-2);
    CHECK_NEAR(y, euler.y, 1e-2);
    CHECK_NEAR(r, euler.z, 1e-2);
}

NOVA_TEST(Math, QuatSlerpTakesShortestPath) {
    const Quat a = Quat::fromAngleAxis(0.0f, Vec3{0, 1, 0});
    const Quat b = Quat::fromAngleAxis(270.0f, Vec3{0, 1, 0});
    // Halfway between 0 and 270 the short way round is -45 degrees of yaw.
    const Quat mid = Quat::slerp(a, b, 0.5f);
    float p, y, r;
    mid.toEuler(p, y, r);
    CHECK_NEAR(wrapAngle(y), 315.0, 0.5);
    CHECK_NEAR(mid.norm(), 1.0, 1e-4);
}

NOVA_TEST(Math, Mat3RotationMatchesQuat) {
    // Mat3::rotation(q) must agree with q.rotate(v) for every axis.
    const Quat q = Quat::fromEuler(31.0f, -64.0f, 17.0f);
    const Mat3 m = Mat3::rotation(q);
    for (const Vec3& v : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{0.3f, -0.5f, 0.8f}}) {
        CHECK_NEAR_VEC3(m * v, q.rotate(v), 1e-4);
    }
    // A rotation matrix is orthonormal with determinant +1.
    // (M * M^T) is the Gram matrix of M's columns and must be the identity.
    const Mat3 gram = m * m.transpose();
    CHECK_NEAR(gram.m[0], 1.0, 1e-4);
    CHECK_NEAR(gram.m[4], 1.0, 1e-4);
    CHECK_NEAR(gram.m[8], 1.0, 1e-4);
    CHECK_NEAR(gram.m[1], 0.0, 1e-4);
    CHECK_NEAR(gram.m[2], 0.0, 1e-4);
    CHECK_NEAR(gram.m[5], 0.0, 1e-4);
    CHECK_NEAR(m.determinant(), 1.0, 1e-4);
    // M * M^-1 * v == v
    CHECK_NEAR_VEC3(m * m.inverse() * Vec3(1, 2, 3), Vec3(1, 2, 3), 1e-4);
}

NOVA_TEST(Math, NormalMatrixHandlesNonUniformScale) {
    // Under non-uniform scale, normals must use the inverse-transpose or
    // lighting is wrong on non-uniformly scaled geometry.
    const Mat3 m = Mat3::scale(Vec3{1, 4, 1});
    const Vec3 n = m.normalMatrix() * Vec3{0, 1, 0};
    // Scaling y by 4 should shrink the y component of a y-facing normal.
    CHECK_NEAR(n.y, 0.25, 1e-4);
    // Singular matrices fall back to identity instead of NaN.
    const Mat3 degenerate = Mat3::scale(Vec3{0, 0, 0});
    CHECK(std::isfinite(degenerate.inverse().m[0]));
}

NOVA_TEST(Math, QuatFromToRotation) {
    const Vec3 from{1, 0, 0}, to{0, 0, 1};
    const Quat q = Quat::fromToRotation(from, to);
    CHECK_NEAR_VEC3(q.rotate(from), to, 1e-4);
    // Antiparallel case must not produce NaN.
    const Quat anti = Quat::fromToRotation(from, Vec3(-1, 0, 0));
    CHECK_NEAR_VEC3(anti.rotate(from), Vec3(-1, 0, 0), 1e-4);
    CHECK(std::isfinite(anti.x));
}

NOVA_TEST(Math, Mat4MultiplyAndInverse) {
    const Mat4 t = Mat4::translation({1, 2, 3});
    const Mat4 s = Mat4::scaling({2, 2, 2});
    // Compose: scale then translate.
    const Vec3 p = (t * s).transformPoint({1, 1, 1});
    CHECK_NEAR_VEC3(p, Vec3(3, 4, 5), 1e-5);
    // Directions ignore translation.
    const Vec3 d = t.transformDirection({1, 0, 0});
    CHECK_NEAR_VEC3(d, Vec3(1, 0, 0), 1e-6);
}

NOVA_TEST(Math, Mat4PerspectiveAndLookAt) {
    const Mat4 proj = Mat4::perspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    // A point on the near plane maps to z_ndc = -1, far plane to +1.
    const Vec4 nearP = proj.project({0, 0, -0.1f});
    CHECK_NEAR(nearP.z / nearP.w, -1.0, 1e-4);
    const Vec4 farP = proj.project({0, 0, -1000.0f});
    CHECK_NEAR(farP.z / farP.w, 1.0, 1e-4);
    // A point behind the camera has w < 0 so the divide flips it.
    const Vec4 behind = proj.project({0, 0, 10.0f});
    CHECK(behind.w < 0.0f);

    const Mat4 view = Mat4::lookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
    const Vec3 originInView = view.transformPoint({0, 0, 0});
    CHECK_NEAR_VEC3(originInView, Vec3(0, 0, -10), 1e-4);
}

NOVA_TEST(Math, Mat4LookAtHandlesDegenerateUp) {
    // Looking straight down with up == forward must not produce NaN.
    const Mat4 view = Mat4::lookAt({0, 10, 0}, {0, 0, 0}, {0, 1, 0});
    const Vec3 p = view.transformPoint({0, 0, 0});
    CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
}

NOVA_TEST(Math, Aabb2) {
    const Aabb2 box{{-1, -1}, {1, 1}};
    CHECK(box.contains({0, 0}));
    CHECK(!box.contains({1.5f, 0}));
    CHECK(box.intersects(Aabb2{{0.5f, 0.5f}, {2, 2}}));
    CHECK(!box.intersects(Aabb2{{2, 2}, {3, 3}}));
    CHECK_NEAR(box.area(), 4.0, 1e-6);
    CHECK_NEAR_VEC3(Vec3(box.closestPoint({5, 0}).x, box.closestPoint({5, 0}).y, 0),
                    Vec3(1, 0, 0), 1e-6);
    // Merging grows to cover both.
    const Aabb2 merged = box.merged(Aabb2{{3, 0}, {4, 0}});
    CHECK_NEAR(merged.max.x, 4.0, 1e-6);
}

NOVA_TEST(Math, Aabb2Raycast) {
    const Aabb2 box{{0, 0}, {2, 2}};
    float t = -1.0f;
    CHECK(box.raycast({-5, 1}, {1, 0}, 100.0f, t));
    CHECK_NEAR(t, 5.0, 1e-4);  // hits the left face at x=0
    // Ray pointing away never hits.
    CHECK(!box.raycast({-5, 1}, {-1, 0}, 100.0f, t));
    // Ray missing vertically.
    CHECK(!box.raycast({-5, 9}, {1, 0}, 100.0f, t));
    // maxDist shorter than the hit distance must reject the hit.
    CHECK(!box.raycast({-5, 1}, {1, 0}, 2.0f, t));
    // Just long enough to reach the face at t = 5.
    CHECK(box.raycast({-5, 1}, {1, 0}, 5.0f, t));
    CHECK_NEAR(t, 5.0, 1e-4);
    // A ray starting inside reports t=0.
    CHECK(box.raycast({1, 1}, {1, 0}, 100.0f, t));
    CHECK_NEAR(t, 0.0, 1e-4);
    // Parallel-but-inside slab is accepted.
    CHECK(box.raycast({1, 1}, {0, 1}, 100.0f, t));
    CHECK_NEAR(t, 0.0, 1e-4);
}

NOVA_TEST(Math, Aabb3TransformedIsConservative) {
    const Aabb3 box{{-1, -1, -1}, {1, 1, 1}};
    const Aabb3 rotated = box.transformed(
        Mat4::rotation(Quat::fromAngleAxis(45.0f, Vec3{0, 0, 1})));
    // A 45-degree rotated cube's AABB grows to sqrt(2) on x and y.
    CHECK(rotated.max.x > 1.4f && rotated.max.x < 1.42f);
    CHECK(rotated.max.y > 1.4f && rotated.max.y < 1.42f);
    CHECK_NEAR(rotated.max.z, 1.0, 1e-4);
    // Original corner must be inside the new box.
    CHECK(rotated.contains({1, 1, 1}));
}

NOVA_TEST(Math, ColorPacking) {
    const Color c{1, 0, 0.5f, 1};
    CHECK(c.toHex() == 0xFF0080);
    const Color back = Color::fromHex(0xFF0080);
    CHECK_NEAR(back.r, 1.0, 0.01);
    CHECK_NEAR(back.b, 0.5, 0.01);
    const uint32_t packed = c.toRgba8();
    CHECK((packed >> 24 & 0xFF) == 255);
    CHECK((packed & 0xFF) == 128);
    CHECK_NEAR(c.luminance(), 0.2126f + 0.0722f * 0.5f, 1e-3);
}

NOVA_TEST(Math, ScalarHelpers) {
    CHECK_NEAR(damp(0.0f, 10.0f, 5.0f, 1.0f), 10.0f * (1.0f - std::exp(-5.0f)), 1e-4);
    CHECK_NEAR(moveTowards(0.0f, 10.0f, 3.0f), 3.0, 1e-5);
    CHECK_NEAR(moveTowards(0.0f, 10.0f, 30.0f), 10.0, 1e-5);
    CHECK_NEAR(angleDeltaDeg(350.0f, 10.0f), 20.0, 1e-3);
    CHECK_NEAR(angleDeltaDeg(10.0f, 350.0f), -20.0, 1e-3);
    CHECK_NEAR(inverseLerp(0.0f, 10.0f, 5.0f), 0.5, 1e-5);
    CHECK_NEAR(inverseLerp(5.0f, 5.0f, 5.0f), 0.0, 1e-5);  // no division by zero
    CHECK_NEAR(smoothstep(0.0f, 1.0f, 0.5f), 0.5, 1e-5);
    CHECK_NEAR(smoothstep(0.0f, 1.0f, 0.0f), 0.0, 1e-5);
    CHECK_NEAR(wrapAngle(-90.0f), 270.0, 1e-4);
    CHECK_NEAR(wrapAngle(450.0f), 90.0, 1e-4);
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

NOVA_TEST(Json, ParseObject) {
    std::string err;
    const Json j = Json::parse(R"({"a":1,"b":"two","c":[1,2,3],"d":{"e":true}})", &err);
    CHECK_MSG(err.empty(), err);
    CHECK(j.isObject());
    CHECK(j["a"].asInt() == 1);
    CHECK(j["b"].asString() == "two");
    CHECK(j["c"].size() == 3);
    CHECK_NEAR(j["c"].at(2).asFloat(), 3.0, 1e-6);
    CHECK(j["d"]["e"].asBool());
    CHECK(!j.has("missing"));
    CHECK(j["missing"].isNull());
    CHECK(j["missing"].asInt(7) == 7);
}

NOVA_TEST(Json, RoundTripPreservesValues) {
    const std::string src = R"({
        "name": "level1",
        "gravity": -9.81,
        "spawn": [1.5, -2.0, 0.0],
        "tint": "#ff8800",
        "nested": {"flags": [true, false, null], "count": 42},
        "escaped": "line\nbreak \"quoted\" \\ \u00e9"
    })";
    std::string err;
    const Json a = Json::parse(src, &err);
    CHECK_MSG(err.empty(), err);
    const Json b = Json::parse(a.dump(), &err);
    CHECK_MSG(err.empty(), err);

    CHECK(b["name"].asString() == "level1");
    CHECK_NEAR(b["gravity"].asFloat(), -9.81, 1e-5);
    CHECK_NEAR_VEC3(b["spawn"].asVec3(), Vec3(1.5f, -2.0f, 0.0f), 1e-5);
    CHECK(b["tint"].asColor() == Color::fromHex(0xFF8800));
    CHECK(b["nested"]["count"].asInt() == 42);
    CHECK(b["nested"]["flags"].size() == 3);
    CHECK(b["nested"]["flags"].at(2).isNull());
    // Unicode escape round-trips as UTF-8.
    CHECK(b["escaped"].asString().find("\xc3\xa9") != std::string::npos);
    CHECK(b["escaped"].asString().find('\n') != std::string::npos);
    // Re-dumping is byte-stable.
    CHECK(a.dump() == b.dump());
}

NOVA_TEST(Json, BuildObject) {
    Json obj = Json::object();
    obj["x"] = Json(1.5f);
    obj["label"] = Json("player");
    // Chained push must build a 2-element array, not nest into one element.
    obj["tags"] = Json::array().push("a").push("b");
    const std::string out = obj.dump(0);
    std::string err;
    const Json back = Json::parse(out, &err);
    CHECK_MSG(err.empty(), err);
    CHECK_NEAR(back["x"].asFloat(), 1.5, 1e-6);
    CHECK(back["label"].asString() == "player");
    CHECK(back["tags"].isArray());
    CHECK(back["tags"].size() == 2);
    CHECK(back["tags"].at(0).asString() == "a");
    CHECK(back["tags"].at(1).asString() == "b");
    // Key insertion order is preserved (stable diffs for version control).
    const auto keys = back.keys();
    CHECK(keys.size() == 3);
    CHECK(keys[0] == "x" && keys[1] == "label" && keys[2] == "tags");
    // The vec/color helpers use the same chaining and must be correct too.
    CHECK(Json::vec2({1, 2}).size() == 2);
    CHECK(Json::vec3({1, 2, 3}).size() == 3);
    CHECK(Json::color({1, 0, 0, 1}).size() == 4);
}

NOVA_TEST(Json, MalformedInputReportsError) {
    std::string err;
    CHECK(Json::parse("{\"a\":}", &err).isNull());
    CHECK(!err.empty());
    err.clear();
    CHECK(Json::parse("[1,2,", &err).isNull());
    CHECK(!err.empty());
    err.clear();
    CHECK(Json::parse("{} trailing", &err).isNull());
    CHECK(!err.empty());
    err.clear();
    CHECK(Json::parse("{\"s\": \"unterminated}", &err).isNull());
    CHECK(!err.empty());
    // Empty input is a clean failure, not a crash.
    err.clear();
    CHECK(Json::parse("", &err).isNull());
}

NOVA_TEST(Json, TypeCoercionIsSafe) {
    const Json s = Json("12.5");
    CHECK_NEAR(s.asFloat(), 12.5, 1e-5);
    const Json n = Json(7);
    CHECK_NEAR(n.asFloat(), 7.0, 1e-5);
    CHECK(n.asString() == "7");
    const Json nul;
    CHECK_NEAR(nul.asFloat(-1.0f), -1.0, 1e-6);
    CHECK(nul.asString("def") == "def");
    CHECK(nul.asInt(3) == 3);
    // Setting a key on a null promotes it to an object.
    Json promoted;
    promoted["k"] = Json(1);
    CHECK(promoted.isObject());
    CHECK(promoted["k"].asInt() == 1);
}

NOVA_TEST(Json, EraseAndHas) {
    Json obj = Json::object();
    obj["a"] = Json(1);
    obj["b"] = Json(2);
    CHECK(obj.erase("a"));
    CHECK(!obj.has("a"));
    CHECK(!obj.erase("a"));
    CHECK(obj.size() == 1);
}

// ---------------------------------------------------------------------------
// Random
// ---------------------------------------------------------------------------

NOVA_TEST(Random, DeterministicForSameSeed) {
    Random a(1234), b(1234);
    for (int i = 0; i < 100; ++i) CHECK(a.nextUInt() == b.nextUInt());

    Random c(1234), d(4321);
    bool anyDifferent = false;
    for (int i = 0; i < 20; ++i) if (c.nextUInt() != d.nextUInt()) anyDifferent = true;
    CHECK(anyDifferent);
}

NOVA_TEST(Random, RangeIsWithinBounds) {
    Random rng(99);
    bool sawLow = false, sawHigh = false;
    for (int i = 0; i < 5000; ++i) {
        const float v = rng.range(-3.0f, 5.0f);
        CHECK(v >= -3.0f && v < 5.0f);
        if (v < -2.0f) sawLow = true;
        if (v > 4.0f) sawHigh = true;
    }
    CHECK(sawLow && sawHigh);  // the range is actually used

    for (int i = 0; i < 2000; ++i) {
        const int n = rng.rangeInt(3, 7);
        CHECK(n >= 3 && n <= 7);
    }
    CHECK(rng.rangeInt(5, 5) == 5);
    CHECK(rng.rangeInt(9, 2) == 9);  // inverted range is safe
}

NOVA_TEST(Random, UniformDistributionIsFlat) {
    Random rng(2024);
    const int bins = 10;
    int counts[bins] = {0};
    const int n = 100000;
    for (int i = 0; i < n; ++i) {
        const int b = static_cast<int>(rng.nextFloat() * bins);
        counts[std::min(b, bins - 1)]++;
    }
    const float expected = static_cast<float>(n) / bins;
    for (int b = 0; b < bins; ++b) {
        // Within 15% of uniform — catches a broken generator, not luck.
        CHECK_MSG(std::fabs(counts[b] - expected) < expected * 0.15f,
                  "bin " + std::to_string(b) + " count " + std::to_string(counts[b]));
    }
}

NOVA_TEST(Random, GaussianMoments) {
    Random rng(7);
    double sum = 0.0, sumSq = 0.0;
    const int n = 100000;
    for (int i = 0; i < n; ++i) {
        const float v = rng.gaussian(2.0f, 0.5f);
        sum += v;
        sumSq += static_cast<double>(v) * v;
    }
    const double mean = sum / n;
    const double var = sumSq / n - mean * mean;
    CHECK_NEAR(mean, 2.0, 0.02);
    CHECK_NEAR(std::sqrt(var), 0.5, 0.02);
}

NOVA_TEST(Random, SphereSamplingIsUnitLength) {
    Random rng(55);
    for (int i = 0; i < 2000; ++i) {
        const Vec3 v = rng.onUnitSphere();
        CHECK_NEAR(v.length(), 1.0, 1e-3);
    }
    // insideUnitSphere must be strictly inside.
    for (int i = 0; i < 500; ++i) {
        const Vec3 v = rng.insideUnitSphere();
        CHECK(v.length() <= 1.0f + 1e-4f);
    }
}

NOVA_TEST(Random, ShuffleIsAPermutation) {
    Random rng(3);
    int items[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    rng.shuffle(items, 10);
    std::set<int> seen;
    for (int i = 0; i < 10; ++i) seen.insert(items[i]);
    CHECK(seen.size() == 10);
    CHECK(seen.count(0) && seen.count(9));
}

NOVA_TEST(Random, PickWeightedRespectsWeights) {
    Random rng(11);
    const float weights[2] = {0.9f, 0.1f};
    int first = 0;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        const int pick = rng.pickWeighted(weights, 2, weights);
        CHECK(pick == 0 || pick == 1);
        if (pick == 0) ++first;
    }
    const float ratio = static_cast<float>(first) / n;
    CHECK_MSG(ratio > 0.87f && ratio < 0.93f, "ratio was " + std::to_string(ratio));
    // Degenerate tables return -1 rather than indexing out of bounds.
    const float zero[2] = {0.0f, 0.0f};
    CHECK(rng.pickWeighted(weights, 2, zero) == -1);
    CHECK(rng.pickWeighted(weights, 0, weights) == -1);
}

NOVA_TEST(Random, ValueNoiseIsDeterministicAndBounded) {
    for (int i = 0; i < 500; ++i) {
        const float x = static_cast<float>(i) * 0.37f;
        const float y = static_cast<float>(i) * -0.11f;
        const float a = valueNoise2D(x, y, 42);
        const float b = valueNoise2D(x, y, 42);
        CHECK(a == b);
        CHECK(a >= 0.0f && a <= 1.0f);
    }
    // Continuity: no jumps between adjacent samples.
    float prev = valueNoise2D(0.0f, 0.0f, 1);
    for (float x = 0.01f; x < 2.0f; x += 0.01f) {
        const float cur = valueNoise2D(x, 0.0f, 1);
        CHECK(std::fabs(cur - prev) < 0.25f);
        prev = cur;
    }
    const float f = fbm2D(1.5f, 2.5f, 5);
    CHECK(f >= 0.0f && f <= 1.0f);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

NOVA_TEST(Time, ClockAdvances) {
    Clock c;
    c.reset();
    sleepSeconds(0.01);
    c.tick();
    CHECK(c.delta() > 0.0);
    CHECK(c.elapsed() > 0.0);
    c.sampleFps(c.delta());
    CHECK(c.fps() > 0.0 && c.fps() < 1000.0);
}

NOVA_TEST(Time, FixedTimeStepperProducesWholeSteps) {
    FixedTimeStepper stepper(0.1f, 5);
    // Raise the spike clamp so it does not eat the deliberate sub-second
    // deltas; the clamp itself is covered by its own test below.
    stepper.setMaxFrameTime(1.0);
    CHECK(stepper.advance(0.3) == 3);
    // Leftover is under one step, so no more steps.
    CHECK(stepper.advance(0.01) == 0);
    CHECK(stepper.advance(0.09) == 1);
    // Alpha is the fraction toward the next step.
    stepper.reset();
    stepper.advance(0.05);
    CHECK_NEAR(stepper.alpha(), 0.5, 1e-4);
    // Ten accumulated 0.1s chunks must total exactly ten steps, not nine.
    FixedTimeStepper exact(0.1f, 0);
    exact.setMaxFrameTime(1.0);
    int total = 0;
    for (int i = 0; i < 10; ++i) total += exact.advance(0.1);
    CHECK(total == 10);
}

NOVA_TEST(Time, FixedTimeStepperClampsLongStalls) {
    // Default clamp: a 2-second stall integrates at most maxFrameTime (0.25s).
    FixedTimeStepper stepper(0.1f, 0);
    CHECK(stepper.advance(2.0) == 2);  // 0.25 / 0.1
    CHECK_NEAR(stepper.maxFrameTime(), 0.25, 1e-9);
}

NOVA_TEST(Time, FixedTimeStepperClampsSpiralOfDeath) {
    FixedTimeStepper stepper(0.01f, 3);
    // A 5 second hitch would otherwise demand 500 steps.
    const int steps = stepper.advance(5.0);
    CHECK(steps <= 3);
    CHECK(stepper.droppedTime() > 0.0);
    // The simulation recovers rather than staying behind forever.
    CHECK(stepper.advance(0.02) == 2);
}

NOVA_TEST(Time, FixedTimeStepperManualStep) {
    FixedTimeStepper stepper(0.1f, 5);
    stepper.requestStep();
    CHECK(stepper.consumePendingSteps() == 1);
    CHECK(stepper.consumePendingSteps() == 0);
    // A requested step runs even with no accumulated time.
    CHECK(stepper.advance(0.0) == 0);
    stepper.requestStep();
    CHECK(stepper.advance(0.0) == 0);  // pending consumed separately
    CHECK(stepper.consumePendingSteps() == 1);
}

// ---------------------------------------------------------------------------
// FileSystem
// ---------------------------------------------------------------------------

NOVA_TEST(FileSystem, PathHelpers) {
    CHECK(FileSystem::normalize("assets/../textures/./hero.png") == "textures/hero.png");
    CHECK(FileSystem::normalize("a/b/../c") == "a/c");
    CHECK(FileSystem::toEnginePath("assets\\levels\\one.nova") == "assets/levels/one.nova");
    CHECK(FileSystem::extensionOf("assets/hero.PNG") == "png");
    CHECK(FileSystem::extensionOf("noext") == "");
    CHECK(FileSystem::stemOf("assets/hero.png") == "hero");
    CHECK(FileSystem::filenameOf("assets/sub/hero.png") == "hero.png");
    CHECK(FileSystem::parentOf("assets/sub/hero.png") == "assets/sub");
    CHECK(FileSystem::join("assets", "sub/hero.png") == "assets/sub/hero.png");
    CHECK(FileSystem::join("", "hero.png") == "hero.png");
}

NOVA_TEST(FileSystem, ReadWriteRoundTrip) {
    FileSystem fs("build/host/testdata");
    CHECK(fs.writeText("hello.txt", "Hello, Nova!"));
    CHECK(fs.isFile("hello.txt"));
    CHECK(fs.isDirectory("."));

    std::string text;
    CHECK(fs.readText("hello.txt", text));
    CHECK(text == "Hello, Nova!");

    const std::vector<uint8_t> bytes{0, 1, 2, 250, 255};
    CHECK(fs.writeBinary("data.bin", bytes.data(), bytes.size()));
    std::vector<uint8_t> back;
    CHECK(fs.readBinary("data.bin", back));
    CHECK(back == bytes);

    CHECK(fs.fileSize("hello.txt") == 12);
    CHECK(fs.exists("nope.txt") == false);
    CHECK(fs.readText("nope.txt", text) == false);

    // Nested paths create their directories.
    CHECK(fs.writeText("deep/nested/file.json", "{\"ok\":true}"));
    CHECK(fs.isFile("deep/nested/file.json"));
    CHECK(fs.readText("deep/nested/file.json", text));
    CHECK(text == "{\"ok\":true}");

    const auto entries = fs.list(".");
    CHECK(entries.size() >= 3);
    CHECK(entries[0].isDirectory);  // directories sort first
}

NOVA_TEST(FileSystem, CopyAndRemove) {
    FileSystem fs("build/host/testdata2");
    CHECK(fs.writeText("src.txt", "content"));
    CHECK(fs.copyFile("src.txt", "dst/copy.txt"));
    std::string t;
    CHECK(fs.readText("dst/copy.txt", t));
    CHECK(t == "content");

    CHECK(fs.remove("src.txt"));
    CHECK(!fs.exists("src.txt"));
    // Recursive delete removes the whole subtree.
    CHECK(fs.removeTree("dst"));
    CHECK(!fs.exists("dst"));
    // Deleting something that is not there is a clean false, not a crash.
    CHECK(!fs.remove("ghost.txt"));
    fs.removeTree(".");
}

NOVA_TEST(FileSystem, WalkFindsFilesByExtension) {
    FileSystem fs("build/host/testdata3");
    fs.writeText("a/one.nova", "{}");
    fs.writeText("a/b/two.nova", "{}");
    fs.writeText("a/three.png", "x");
    const auto scenes = fs.walk(".", "nova");
    CHECK(scenes.size() == 2);
    for (const auto& s : scenes) CHECK(FileSystem::extensionOf(s) == "nova");
    const auto all = fs.walk(".");
    CHECK(all.size() == 3);
    fs.removeTree(".");
}

NOVA_TEST(FileSystem, AtomicWriteLeavesNoTemp) {
    FileSystem fs("build/host/testdata4");
    CHECK(fs.writeTextAtomic("scene.nova", "{\"v\":1}"));
    std::string t;
    CHECK(fs.readText("scene.nova", t));
    CHECK(t == "{\"v\":1}");
    // Overwriting replaces cleanly and leaves no .tmp behind.
    CHECK(fs.writeTextAtomic("scene.nova", "{\"v\":2}"));
    CHECK(fs.readText("scene.nova", t));
    CHECK(t == "{\"v\":2}");
    CHECK(!fs.exists("scene.nova.tmp"));
    fs.removeTree(".");
}
