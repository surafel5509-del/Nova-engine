#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace nova {

struct RenderScene;
class PhysicsWorld;

/**
 * Lua game-script runtime. Scripts are loaded by name, then started with the
 * scene + physics world they may manipulate. Each script runs in its own
 * environment (falling back to globals) and may define:
 *
 *   function on_start(entityId) ... end
 *   function on_update(entityId, dt) ... end
 *
 * Inside scripts, the global table `nova` exposes the engine API:
 *   nova.get_position(id) -> x, y
 *   nova.set_position(id, x, y)
 *   nova.get_velocity(id) -> vx, vy
 *   nova.set_velocity(id, vx, vy)
 *   nova.set_animation_frame(id, frame)
 *   nova.is_grounded(id) -> boolean
 *   nova.input_axis() -> x, y        (-1 .. 1)
 *   nova.input_jump() -> boolean
 *   nova.play_sound("assets/audio/jump.wav")
 *   nova.log("message")
 *
 * GL-free; host-testable.
 */
class LuaScriptEngine {
public:
    LuaScriptEngine() = default;
    ~LuaScriptEngine();

    /** Registers/overwrites script source (before start). */
    void loadScript(const std::string& name, const std::string& source);
    void clearScripts();

    /** Creates the VM, registers `nova`, executes all loaded scripts. */
    bool start(std::string* outError = nullptr);
    void stop();
    bool running() const { return state_ != nullptr; }

    /** Binds the mutable world the API operates on (called each simulation start). */
    void bind(RenderScene* scene, PhysicsWorld* physics);

    /** Calls on_start(entityId) for every script binding in the scene. */
    void callOnStart();
    /** Calls on_update(entityId, dt) for every script binding in the scene. */
    void update(float dt, float inputX, float inputY, bool jump);

    /** Sound paths queued via nova.play_sound (drained by Engine). */
    std::vector<std::string>& soundEvents() { return soundEvents_; }
    /** Log lines from nova.log and script errors (drained by Engine). */
    std::vector<std::string>& logMessages() { return logMessages_; }

    float inputX() const { return inputX_; }
    float inputY() const { return inputY_; }
    bool inputJump() const { return inputJump_; }

    // ---- native API implementations (also used by lua_CFunctions) ----
    void apiSetPosition(const std::string& id, float x, float y);
    void apiGetPosition(const std::string& id, float* outX, float* outY);
    void apiSetVelocity(const std::string& id, float vx, float vy);
    void apiGetVelocity(const std::string& id, float* outVx, float* outVy);
    bool apiIsGrounded(const std::string& id);
    void apiSetAnimationFrame(const std::string& id, int frame);

    /** Registry lookup (public for the file-local trampoline functions). */
    static LuaScriptEngine* self(lua_State* L);

private:
    void callScriptMethod(const std::string& scriptName, const char* method,
                          const std::string& entityId, float dt);

    lua_State* state_ = nullptr;
    RenderScene* scene_ = nullptr;
    PhysicsWorld* physics_ = nullptr;
    // Sources staged by loadScript() (until start()).
    std::unordered_map<std::string, std::string> pendingSources_;
    // For each script name after start(): registry ref to its environment table.
    std::unordered_map<std::string, int> scriptEnvRefs_;
    std::vector<std::string> scriptOrder_;
    std::vector<std::string> soundEvents_;
    std::vector<std::string> logMessages_;
    float inputX_ = 0.0f;
    float inputY_ = 0.0f;
    bool inputJump_ = false;
};

} // namespace nova
