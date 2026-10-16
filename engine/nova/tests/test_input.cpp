// Input tests: device state, per-frame edges, action mapping and round-tripping.
#include "tests/TestHarness.h"

#include <cmath>
#include <string>
#include <vector>

#include "input/Input.h"

using namespace nova;
using namespace nova::input;

namespace {

/// Feeds device state the way a platform layer does: open the frame, apply what
/// the devices reported, close it. The *Now helpers run a whole frame so the
/// tests read like the thing they describe.
struct Device {
    InputState state;

    void open(float dt) { state.beginFrame(dt); }
    void close() { state.endFrame(); }

    void press(Key k) { state.keys[static_cast<size_t>(k)] = true; }
    void release(Key k) { state.keys[static_cast<size_t>(k)] = false; }
    void click(MouseButton b) { state.mouseButtons[static_cast<size_t>(b)] = true; }
    void unclick(MouseButton b) { state.mouseButtons[static_cast<size_t>(b)] = false; }
    void move(Vec2 p) { state.mousePosition = p; }
    void pressPointer(int id, Vec2 p) { state.setPointer(id, p, true); }
    void movePointer(int id, Vec2 p) { state.setPointer(id, p, true); }
    void releasePointer(int id, Vec2 p) { state.setPointer(id, p, false); }

    void tick(float dt = 0.016f) {
        open(dt);
        close();
    }
    void pressNow(Key k, float dt = 0.016f) {
        open(dt);
        press(k);
        close();
    }
    void releaseNow(Key k, float dt = 0.016f) {
        open(dt);
        release(k);
        close();
    }
    void clickNow(MouseButton b, float dt = 0.016f) {
        open(dt);
        click(b);
        close();
    }
    void unclickNow(MouseButton b, float dt = 0.016f) {
        open(dt);
        unclick(b);
        close();
    }
    void moveNow(Vec2 p, float dt = 0.016f) {
        open(dt);
        move(p);
        close();
    }
};

Binding keyBind(Key k, uint32_t mods = kModNone) {
    Binding b(k);
    b.modifiers = mods;
    return b;
}

}  // namespace

NOVA_TEST(Input, KeyEdgesLastExactlyOneFrame) {
    Device d;
    d.tick();
    CHECK(!d.state.keyPressed(Key::Space));
    CHECK(!d.state.keyDown(Key::Space));

    d.pressNow(Key::Space);
    CHECK(d.state.keyPressed(Key::Space));
    CHECK(d.state.keyDown(Key::Space));

    d.tick();
    CHECK(!d.state.keyPressed(Key::Space));
    CHECK(d.state.keyDown(Key::Space));

    d.releaseNow(Key::Space);
    CHECK(d.state.keyReleased(Key::Space));
    CHECK(!d.state.keyDown(Key::Space));

    d.tick();
    CHECK(!d.state.keyReleased(Key::Space));
}

NOVA_TEST(Input, OutOfRangeKeysAreIgnored) {
    Device d;
    d.open(0.016f);
    d.press(Key::Unknown);
    d.press(Key::Count);
    d.close();
    CHECK(!d.state.keyDown(Key::Unknown));
    CHECK(!d.state.keyPressed(Key::Unknown));
    CHECK(!d.state.keyReleased(Key::Unknown));
    CHECK(!d.state.keyDown(Key::Count));
    CHECK(!d.state.mouseDown(MouseButton::Count));
}

NOVA_TEST(Input, KeyRepeatWaitsForTheDelayThenFires) {
    Device d;
    d.pressNow(Key::Right, 0.1f);
    // The repeat timer starts at the press, so nothing fires immediately.
    CHECK(!d.state.keyRepeating(Key::Right, 0.5f, 0.1f));
    for (int i = 0; i < 5; ++i) d.tick(0.1f);
    // 0.5s of hold have now passed, and 0.1 divides into it exactly.
    CHECK(d.state.keyRepeating(Key::Right, 0.5f, 0.1f));
    d.tick(0.05f);
    CHECK(!d.state.keyRepeating(Key::Right, 0.5f, 0.1f));
    // A zero rate must not divide by zero.
    d.tick(0.01f);
    CHECK(!d.state.keyRepeating(Key::Right, 0.5f, 0.0f));
    d.releaseNow(Key::Right);
    CHECK(!d.state.keyRepeating(Key::Right, 0.5f, 0.1f));
}

NOVA_TEST(Input, MouseEdgesAndDeltas) {
    Device d;
    d.moveNow({10, 20});
    CHECK_NEAR(d.state.mouseDelta.x, 10.0f, 0.001);
    CHECK_NEAR(d.state.mouseDelta.y, 20.0f, 0.001);

    d.moveNow({13, 25});
    CHECK_NEAR(d.state.mouseDelta.x, 3.0f, 0.001);
    CHECK_NEAR(d.state.mouseDelta.y, 5.0f, 0.001);

    d.clickNow(MouseButton::Left);
    CHECK(d.state.mousePressed(MouseButton::Left));
    d.tick();
    CHECK(!d.state.mousePressed(MouseButton::Left));
    CHECK(d.state.mouseDown(MouseButton::Left));
    d.unclickNow(MouseButton::Left);
    CHECK(d.state.mouseReleased(MouseButton::Left));
    CHECK(!d.state.mouseDown(MouseButton::Right));
}

NOVA_TEST(Input, WheelDeltaResetsEveryFrame) {
    Device d;
    // The host writes the wheel delta after opening the frame; it survives to
    // the end of that frame and is cleared when the next one opens.
    d.open(0.016f);
    d.state.wheelDelta = 3.0f;
    d.close();
    CHECK_NEAR(d.state.wheelDelta, 3.0f, 0.001);
    d.tick();
    CHECK_NEAR(d.state.wheelDelta, 0.0f, 0.001);
}

NOVA_TEST(Input, PointersTrackPressesAndTaps) {
    Device d;
    d.open(0.016f);
    d.pressPointer(0, {10, 10});
    d.close();
    CHECK(d.state.pointerCount() == 1);
    CHECK(d.state.pointers[0].pressed);
    CHECK(d.state.pointers[0].down);
    CHECK_NEAR(d.state.pointers[0].startPosition.x, 10.0f, 0.001);

    d.open(0.016f);
    d.movePointer(0, {12, 14});
    d.close();
    CHECK(!d.state.pointers[0].pressed);
    CHECK_NEAR(d.state.pointers[0].delta.x, 2.0f, 0.001);
    CHECK_NEAR(d.state.pointers[0].travel().length(), std::sqrt(20.0f), 0.01);

    // A quick, still press is a tap.
    d.open(0.016f);
    d.releasePointer(0, {12, 14});
    d.close();
    CHECK(d.state.pointers[0].released);
    CHECK(d.state.wasTap(d.state.pointers[0], 0.3f, 24.0f));
    // The same press with a tighter distance budget is not.
    CHECK(!d.state.wasTap(d.state.pointers[0], 0.3f, 2.0f));
}

NOVA_TEST(Input, LongPressIsNotATap) {
    Device d;
    d.open(0.05f);
    d.pressPointer(0, {5, 5});
    d.close();
    for (int i = 0; i < 29; ++i) d.tick(0.05f);  // about 1.5 seconds in total
    d.open(0.05f);
    d.releasePointer(0, {5, 5});
    d.close();
    CHECK(d.state.pointers[0].released);
    CHECK(!d.state.wasTap(d.state.pointers[0], 0.3f, 24.0f));
}

NOVA_TEST(Input, MultiplePointersAreIndependent) {
    Device d;
    d.open(0.016f);
    d.pressPointer(0, {0, 0});
    d.pressPointer(1, {50, 50});
    d.close();
    CHECK(d.state.pointerCount() == 2);
    CHECK(d.state.pointers[0].pressed);
    CHECK(d.state.pointers[1].pressed);

    d.open(0.016f);
    d.releasePointer(1, {50, 50});
    d.close();
    CHECK(d.state.pointers[0].down);
    CHECK(!d.state.pointers[1].down);
    CHECK(d.state.pointers[1].released);

    d.open(0.016f);
    d.state.endAllPointers();
    d.close();
    CHECK(!d.state.pointers[0].down);
}

NOVA_TEST(Input, UnknownPointerReleasesAreIgnored) {
    Device d;
    d.state.setPointer(7, {1, 1}, false);
    CHECK(d.state.pointerCount() == 0);
    d.state.releasePointer(3, {1, 1});
    CHECK(d.state.pointerCount() == 0);
    d.state.setPointer(-1, {1, 1}, true);
    CHECK(d.state.pointerCount() == 0);

    // The pointer table is bounded: a flood of ids cannot grow it forever.
    for (int i = 0; i < 100; ++i) d.state.setPointer(i, {1, 1}, true);
    CHECK(d.state.pointerCount() <= 16);
    d.open(0.016f);
    d.close();
}

NOVA_TEST(Input, ActionMapResolvesBindingsAndAxes) {
    ActionMap map;
    map.bind("Move", {keyBind(Key::A), keyBind(Key::Left), keyBind(Key::D),
                      keyBind(Key::Right)});

    Device d;
    d.tick();
    map.update(d.state, 0.016f);
    CHECK(!map.down("Move"));
    CHECK_NEAR(map.axis("Move"), -1.0f, 0.001);

    d.pressNow(Key::D);
    map.update(d.state, 0.016f);
    CHECK(map.down("Move"));
    CHECK(map.pressed("Move"));
    CHECK_NEAR(map.axis("Move"), 1.0f, 0.001);
    CHECK_NEAR(map.held("Move"), 0.016f, 0.001);

    // Left and D are both down: the axis resolves to zero, not to a random sum.
    d.pressNow(Key::A);
    map.update(d.state, 0.016f);
    CHECK(map.down("Move"));
    CHECK(!map.pressed("Move"));
    CHECK_NEAR(map.axis("Move"), 0.0f, 0.001);

    d.open(0.016f);
    d.release(Key::D);
    d.release(Key::A);
    d.close();
    map.update(d.state, 0.016f);
    CHECK(map.released("Move"));
    CHECK(!map.down("Move"));
    CHECK_NEAR(map.held("Move"), 0.0f, 0.001);
}

NOVA_TEST(Input, MouseBindingsDriveActions) {
    ActionMap map;
    map.bind("Fire", {Binding(MouseButton::Left)});
    Device d;
    d.tick();
    map.update(d.state, 0.016f);
    CHECK(!map.down("Fire"));
    d.clickNow(MouseButton::Left);
    map.update(d.state, 0.016f);
    CHECK(map.pressed("Fire"));
}

NOVA_TEST(Input, RebindingReplacesThePreviousBindings) {
    ActionMap map;
    map.bind("Jump", {keyBind(Key::Space)});
    Device d;
    d.pressNow(Key::Space);
    map.update(d.state, 0.016f);
    CHECK(map.down("Jump"));

    map.bind("Jump", {keyBind(Key::W)});
    map.update(d.state, 0.016f);
    CHECK_MSG(!map.down("Jump"), "old binding still live");
    d.pressNow(Key::W);
    map.update(d.state, 0.016f);
    CHECK(map.down("Jump"));
}

NOVA_TEST(Input, UnknownActionsAreSafe) {
    ActionMap map;
    map.update(InputState{}, 0.016f);
    CHECK(!map.down("nothing"));
    CHECK(!map.pressed("nothing"));
    CHECK(!map.released("nothing"));
    CHECK_NEAR(map.held("nothing"), 0.0f, 0.001);
    CHECK_NEAR(map.axis("nothing"), -1.0f, 0.001);
    CHECK(!map.has("nothing"));
    CHECK(map.size() == 0);
}

NOVA_TEST(Input, BindingsRoundTripThroughJson) {
    ActionMap map;
    map.bind("Move", {keyBind(Key::A), keyBind(Key::D, kModShift)});
    map.bind("Fire", {Binding(MouseButton::Left)});
    const std::string json = map.toJson();

    ActionMap restored;
    CHECK(restored.fromJson(json));
    CHECK(restored.size() == 2);
    CHECK(restored.names()[0] == "Move");
    CHECK(restored.names()[1] == "Fire");
    CHECK(restored.has("Fire"));

    Device d;
    d.pressNow(Key::D);
    restored.update(d.state, 0.016f);
    CHECK(restored.down("Move"));

    // Saving again reproduces the same document.
    CHECK(restored.toJson() == json);
}

NOVA_TEST(Input, MalformedJsonLeavesTheMapUntouched) {
    ActionMap map;
    map.bind("Move", {keyBind(Key::A)});
    const std::string before = map.toJson();
    CHECK(!map.fromJson("not json at all"));
    CHECK(!map.fromJson("[]"));
    CHECK(!map.fromJson("{\"actions\":42}"));
    CHECK(map.toJson() == before);
    CHECK(map.has("Move"));

    // Entries that are not objects are skipped rather than crashing.
    CHECK(map.fromJson("{\"actions\":[7,\"x\",{\"name\":\"Ok\"}]}"));
    CHECK(map.has("Ok"));
    CHECK(map.size() == 1);
}

NOVA_TEST(Input, KeyNamesRoundTrip) {
    for (int i = 1; i < static_cast<int>(Key::Count); ++i) {
        const Key k = static_cast<Key>(i);
        Key parsed = Key::Unknown;
        CHECK_MSG(parseKey(toString(k), parsed), toString(k));
        CHECK(parsed == k);
    }
    // Case does not matter.
    Key k = Key::Unknown;
    CHECK(parseKey("escape", k));
    CHECK(k == Key::Escape);
    CHECK(!parseKey("NotAKey", k));
    CHECK(!parseKey("", k));

    for (int i = 0; i < static_cast<int>(MouseButton::Count); ++i) {
        const MouseButton b = static_cast<MouseButton>(i);
        MouseButton parsed = MouseButton::Count;
        CHECK(parseMouseButton(toString(b), parsed));
        CHECK(parsed == b);
    }
    CHECK(std::string(toString(Key::Unknown)) == "Unknown");
    CHECK(std::string(toString(MouseButton::Count)) == "None");
}

NOVA_TEST(Input, EmptyBindingsAreDropped) {
    Binding b;
    CHECK(b.empty());
    CHECK(!Binding(Key::A).empty());
    CHECK(!Binding(MouseButton::Left).empty());
    CHECK(Binding().withModifiers(kModAlt).empty());
}