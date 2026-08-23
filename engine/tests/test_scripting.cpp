// Host-side tests for the Lua scripting runtime (GL-free).
#include <cmath>
#include <cstdio>
#include <string>

#include "physics/PhysicsWorld.h"
#include "scene/RenderScene.h"
#include "scripting/LuaScriptEngine.h"

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

struct Fixture {
    nova::RenderScene scene;
    nova::PhysicsWorld physics;
    nova::LuaScriptEngine scripting;

    Fixture() {
        nova::SpriteInstance sprite;
        sprite.id = "player";
        sprite.x = 1.0f;
        sprite.y = 2.0f;
        scene.sprites.push_back(sprite);

        nova::PhysicsBody body;
        body.id = "player";
        body.bodyType = 1;
        body.x = 1.0f;
        body.y = 2.0f;
        physics.addBody(body);

        nova::ScriptRecord binding;
        binding.id = "player";
        binding.script = "player.lua";
        scene.scripts.push_back(binding);

        scripting.bind(&scene, &physics);
    }
};

void testOnStartAndUpdate() {
    Fixture f;
    f.scripting.loadScript("player.lua", R"LUA(
speed = 5
function on_start(id)
    nova.set_position(id, 10, 20)
    nova.log("started")
end
function on_update(id, dt)
    local ax, ay = nova.input_axis()
    local vx, vy = nova.get_velocity(id)
    nova.set_velocity(id, ax * speed, vy)
    if nova.input_jump() and nova.is_grounded(id) then
        nova.play_sound("assets/audio/jump.wav")
    end
end
)LUA");
    check(f.scripting.start(), "script VM starts");
    f.scripting.callOnStart();
    check(nearly(f.physics.bodies()[0].x, 10.0f), "on_start teleports physics body");
    check(nearly(f.scene.sprites[0].x, 10.0f), "on_start teleports sprite");

    f.scripting.update(1.0f / 60.0f, 1.0f, 0.0f, false);
    check(nearly(f.physics.bodies()[0].vx, 5.0f), "on_update sets velocity from input axis");
    check(f.scripting.soundEvents().empty(), "no sound without jump");

    f.physics.bodies()[0].grounded = true;
    f.scripting.update(1.0f / 60.0f, 0.0f, 0.0f, true);
    check(f.scripting.soundEvents().size() == 1, "jump triggers sound event");
    check(f.scripting.soundEvents()[0] == "assets/audio/jump.wav", "sound event path");
    check(f.scripting.logMessages().size() == 1, "log captured");
    f.scripting.stop();
}

void testScriptErrorIsCaptured() {
    Fixture f;
    f.scripting.loadScript("player.lua", "function on_update(id, dt) error('boom') end");
    check(f.scripting.start(), "VM starts with loadable script");
    f.scripting.update(0.016f, 0.0f, 0.0f, false);
    check(!f.scripting.logMessages().empty(), "runtime error captured");
    check(f.scripting.logMessages()[0].find("boom") != std::string::npos, "error message content");
    f.scripting.stop();
}

void testGetPositionReadsBody() {
    Fixture f;
    f.scripting.loadScript("player.lua", R"LUA(
function on_update(id, dt)
    local x, y = nova.get_position(id)
    if x == 1 and y == 2 then
        nova.set_position(id, x + 1, y)
    end
end
)LUA");
    check(f.scripting.start(), "VM starts");
    f.scripting.update(0.016f, 0.0f, 0.0f, false);
    check(nearly(f.physics.bodies()[0].x, 2.0f), "get_position reads body, set_position writes");
    f.scripting.stop();
}

void testTwoScriptsSeparateEnvironments() {
    Fixture f;
    f.scene.scripts.clear();
    nova::ScriptRecord a{"player", "a.lua"};
    nova::ScriptRecord b{"player", "b.lua"};
    f.scene.scripts.push_back(a);
    f.scene.scripts.push_back(b);
    f.scripting.loadScript("a.lua", "counter = 0\nfunction on_update(id, dt) counter = counter + 1 end");
    f.scripting.loadScript("b.lua", "function on_update(id, dt)\n  if counter ~= nil then error('leak') end\nend");
    check(f.scripting.start(), "VM starts both scripts");
    f.scripting.update(0.016f, 0.0f, 0.0f, false);
    check(f.scripting.logMessages().empty(), "script environments isolated");
    f.scripting.stop();
}

} // namespace

int runScriptingTests() {
    testOnStartAndUpdate();
    testScriptErrorIsCaptured();
    testGetPositionReadsBody();
    testTwoScriptsSeparateEnvironments();
    std::printf("scripting: %d checks, %d failures\n", checks, failures);
    return failures;
}
