#include "scripting/LuaScriptEngine.h"

#include <algorithm>

#include <lua.hpp>

#include "physics/PhysicsWorld.h"
#include "scene/RenderScene.h"

namespace nova {

namespace {

// Registry key for the owning LuaScriptEngine instance.
const char* kSelfKey = "nova_script_engine_self";

// ---- Lua -> engine trampolines ----

int l_get_position(lua_State* L) {
    LuaScriptEngine* self = LuaScriptEngine::self(L);
    const char* id = luaL_checkstring(L, 1);
    float x = 0.0f, y = 0.0f;
    self->apiGetPosition(id, &x, &y);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

int l_set_position(lua_State* L) {
    LuaScriptEngine* self = LuaScriptEngine::self(L);
    const char* id = luaL_checkstring(L, 1);
    const float x = static_cast<float>(luaL_checknumber(L, 2));
    const float y = static_cast<float>(luaL_checknumber(L, 3));
    self->apiSetPosition(id, x, y);
    return 0;
}

int l_get_velocity(lua_State* L) {
    LuaScriptEngine* self = LuaScriptEngine::self(L);
    const char* id = luaL_checkstring(L, 1);
    float vx = 0.0f, vy = 0.0f;
    self->apiGetVelocity(id, &vx, &vy);
    lua_pushnumber(L, vx);
    lua_pushnumber(L, vy);
    return 2;
}

int l_set_velocity(lua_State* L) {
    LuaScriptEngine* self = LuaScriptEngine::self(L);
    const char* id = luaL_checkstring(L, 1);
    const float vx = static_cast<float>(luaL_checknumber(L, 2));
    const float vy = static_cast<float>(luaL_checknumber(L, 3));
    self->apiSetVelocity(id, vx, vy);
    return 0;
}

int l_is_grounded(lua_State* L) {
    LuaScriptEngine* self = LuaScriptEngine::self(L);
    const char* id = luaL_checkstring(L, 1);
    lua_pushboolean(L, self->apiIsGrounded(id));
    return 1;
}

int l_input_axis(lua_State* L) {
    LuaScriptEngine* self = LuaScriptEngine::self(L);
    lua_pushnumber(L, self->inputX());
    lua_pushnumber(L, self->inputY());
    return 2;
}

int l_input_jump(lua_State* L) {
    LuaScriptEngine* self = LuaScriptEngine::self(L);
    lua_pushboolean(L, self->inputJump());
    return 1;
}

int l_play_sound(lua_State* L) {
    LuaScriptEngine* self = LuaScriptEngine::self(L);
    const char* path = luaL_checkstring(L, 1);
    self->soundEvents().push_back(path);
    return 0;
}

int l_set_animation_frame(lua_State* L) {
    LuaScriptEngine* self = LuaScriptEngine::self(L);
    const char* id = luaL_checkstring(L, 1);
    const int frame = static_cast<int>(luaL_checkinteger(L, 2));
    self->apiSetAnimationFrame(id, frame);
    return 0;
}

int l_log(lua_State* L) {
    LuaScriptEngine* self = LuaScriptEngine::self(L);
    const char* msg = luaL_checkstring(L, 1);
    self->logMessages().push_back(std::string("[lua] ") + msg);
    return 0;
}

} // namespace

LuaScriptEngine::~LuaScriptEngine() {
    stop();
}

LuaScriptEngine* LuaScriptEngine::self(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kSelfKey);
    auto* engine = static_cast<LuaScriptEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return engine;
}

void LuaScriptEngine::loadScript(const std::string& name, const std::string& source) {
    pendingSources_[name] = source;
    if (std::find(scriptOrder_.begin(), scriptOrder_.end(), name) == scriptOrder_.end()) {
        scriptOrder_.push_back(name);
    }
}

void LuaScriptEngine::clearScripts() {
    pendingSources_.clear();
    scriptOrder_.clear();
}

void LuaScriptEngine::bind(RenderScene* scene, PhysicsWorld* physics) {
    scene_ = scene;
    physics_ = physics;
}

bool LuaScriptEngine::start(std::string* outError) {
    stop();
    state_ = luaL_newstate();
    if (!state_) {
        if (outError) *outError = "luaL_newstate failed";
        return false;
    }
    luaL_openlibs(state_);

    // registry[SELF] = this
    lua_pushlightuserdata(state_, this);
    lua_setfield(state_, LUA_REGISTRYINDEX, kSelfKey);

    // Create the global `nova` API table.
    lua_newtable(state_);
    lua_pushcfunction(state_, l_get_position);
    lua_setfield(state_, -2, "get_position");
    lua_pushcfunction(state_, l_set_position);
    lua_setfield(state_, -2, "set_position");
    lua_pushcfunction(state_, l_get_velocity);
    lua_setfield(state_, -2, "get_velocity");
    lua_pushcfunction(state_, l_set_velocity);
    lua_setfield(state_, -2, "set_velocity");
    lua_pushcfunction(state_, l_is_grounded);
    lua_setfield(state_, -2, "is_grounded");
    lua_pushcfunction(state_, l_input_axis);
    lua_setfield(state_, -2, "input_axis");
    lua_pushcfunction(state_, l_input_jump);
    lua_setfield(state_, -2, "input_jump");
    lua_pushcfunction(state_, l_play_sound);
    lua_setfield(state_, -2, "play_sound");
    lua_pushcfunction(state_, l_set_animation_frame);
    lua_setfield(state_, -2, "set_animation_frame");
    lua_pushcfunction(state_, l_log);
    lua_setfield(state_, -2, "log");
    lua_setglobal(state_, "nova");

    // Execute each script in its own environment so `on_update` stays per-script.
    for (const std::string& name : scriptOrder_) {
        const std::string& source = pendingSources_[name];
        if (luaL_loadbuffer(state_, source.c_str(), source.size(), name.c_str()) != LUA_OK) {
            logMessages_.push_back(name + ": load error: " + lua_tostring(state_, -1));
            lua_pop(state_, 1);
            continue;
        }
        // env = {} with metatable { __index = _G }; set as chunk _ENV.
        lua_newtable(state_);
        lua_newtable(state_);
        lua_getglobal(state_, "_G");
        lua_setfield(state_, -2, "__index");
        lua_setmetatable(state_, -2);
        lua_pushvalue(state_, -1);          // keep a copy of env for setupvalue below
        lua_setupvalue(state_, -3, 1);      // chunk _ENV = env
        int envRef = luaL_ref(state_, LUA_REGISTRYINDEX);
        if (lua_pcall(state_, 0, 0, 0) != LUA_OK) {
            logMessages_.push_back(name + ": run error: " + lua_tostring(state_, -1));
            lua_pop(state_, 1);
        }
        scriptEnvRefs_[name] = envRef;
    }
    return true;
}

void LuaScriptEngine::stop() {
    if (state_) {
        lua_close(state_);
        state_ = nullptr;
    }
    scriptEnvRefs_.clear();
}

void LuaScriptEngine::callScriptMethod(const std::string& name, const char* method,
                                       const std::string& entityId, float dt) {
    auto it = scriptEnvRefs_.find(name);
    if (it == scriptEnvRefs_.end()) return;
    lua_rawgeti(state_, LUA_REGISTRYINDEX, it->second);   // env
    lua_getfield(state_, -1, method);
    if (!lua_isfunction(state_, -1)) {
        lua_pop(state_, 2);
        return;
    }
    lua_pushstring(state_, entityId.c_str());
    lua_pushnumber(state_, dt);
    if (lua_pcall(state_, 2, 0, 0) != LUA_OK) {
        logMessages_.push_back(name + ": " + method + " error: " + lua_tostring(state_, -1));
        lua_pop(state_, 1);
    }
    lua_pop(state_, 1); // env
}

void LuaScriptEngine::callOnStart() {
    if (!state_ || !scene_) return;
    for (const ScriptRecord& binding : scene_->scripts) {
        callScriptMethod(binding.script, "on_start", binding.id, 0.0f);
    }
}

void LuaScriptEngine::update(float dt, float inputX, float inputY, bool jump) {
    if (!state_ || !scene_) return;
    inputX_ = inputX;
    inputY_ = inputY;
    inputJump_ = jump;
    for (const ScriptRecord& binding : scene_->scripts) {
        callScriptMethod(binding.script, "on_update", binding.id, dt);
    }
}

// ---- native API implementations ----

void LuaScriptEngine::apiSetPosition(const std::string& id, float x, float y) {
    if (physics_) {
        for (auto& body : physics_->bodies()) {
            if (body.id == id) {
                body.x = x;
                body.y = y;
                break;
            }
        }
    }
    if (scene_) {
        if (SpriteInstance* sprite = scene_->findSprite(id)) {
            sprite->x = x;
            sprite->y = y;
        }
    }
}

void LuaScriptEngine::apiGetPosition(const std::string& id, float* outX, float* outY) {
    if (physics_) {
        for (const auto& body : physics_->bodies()) {
            if (body.id == id) {
                *outX = body.x;
                *outY = body.y;
                return;
            }
        }
    }
    if (scene_) {
        if (const SpriteInstance* sprite = scene_->findSprite(id)) {
            *outX = sprite->x;
            *outY = sprite->y;
        }
    }
}

void LuaScriptEngine::apiSetVelocity(const std::string& id, float vx, float vy) {
    if (!physics_) return;
    for (auto& body : physics_->bodies()) {
        if (body.id == id) {
            body.vx = vx;
            body.vy = vy;
            return;
        }
    }
}

void LuaScriptEngine::apiGetVelocity(const std::string& id, float* outVx, float* outVy) {
    if (!physics_) return;
    for (const auto& body : physics_->bodies()) {
        if (body.id == id) {
            *outVx = body.vx;
            *outVy = body.vy;
            return;
        }
    }
}

bool LuaScriptEngine::apiIsGrounded(const std::string& id) {
    if (!physics_) return false;
    for (const auto& body : physics_->bodies()) {
        if (body.id == id) return body.grounded;
    }
    return false;
}

void LuaScriptEngine::apiSetAnimationFrame(const std::string& id, int frame) {
    if (!scene_) return;
    if (SpriteInstance* sprite = scene_->findSprite(id)) {
        sprite->frameIndex = frame;
    }
}

} // namespace nova
