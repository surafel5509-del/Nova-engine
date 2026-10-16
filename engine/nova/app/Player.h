// The Nova runtime: the object that actually runs a game.
//
// A Player owns the whole stack for one running game — the scene graph, the
// ECS, the 2D physics world, input, and the software rasterizer — and drives
// them in a fixed order every frame. It knows nothing about windows, sockets or
// files: it is handed an InputState and hands back a finished framebuffer.
// That separation is what lets the same runtime back the standalone player,
// the editor's play mode, and the unit tests.

#ifndef NOVA_APP_PLAYER_H
#define NOVA_APP_PLAYER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "base/Random.h"
#include "base/Time.h"
#include "ecs/World.h"
#include "input/Input.h"
#include "physics/Physics2D.h"
#include "render/Camera.h"
#include "render/Image.h"
#include "render/Renderer.h"
#include "scene/Node.h"

namespace nova {
namespace app {

using render::Camera;
using render::Framebuffer;
using render::Image;
using render::RenderSettings;
using render::Renderer2D;
using render::ProjectionMode;
using render::SpriteItem;
using input::InputState;
using input::ActionMap;

/// Everything the editor and the console want to know about a running game.
struct PlayerStats {
    uint64_t frame = 0;
    double elapsedSeconds = 0.0;
    double fps = 0.0;
    double frameMs = 0.0;
    double simulateMs = 0.0;
    double renderMs = 0.0;
    double encodeMs = 0.0;
    int bodies = 0;
    int activeBodies = 0;
    int contacts = 0;
    int drawCalls = 0;
    int pixelsShaded = 0;
    int viewWidth = 0;
    int viewHeight = 0;
    /// True while the simulation is held by a pause command. A host uses this to
    /// stop re-encoding a picture that cannot have changed.
    bool paused = false;
};

/// What the runtime did with the frame it was asked for.
enum class RunResult { Running, Finished };

/// A single running game.
///
/// Threading: update/render/capture take an external lock, and every public
/// entry point takes it itself, so the editor can call render() from the render
/// thread while a websocket thread calls applyCommand(). Locking a frame is
/// cheap next to the frame it protects.
class Player {
public:
    struct Config {
        int width = 960;
        int height = 540;
        /// Fixed simulation step. Rendering happens once per call to update()
        /// regardless of how many sub-steps the catch-up took.
        float fixedDelta = 1.0f / 60.0f;
        /// Give up on catching up after this much real time in one frame, so a
        /// stalled process does not spend the next minute simulating.
        double maxFrameTime = 0.25;
        /// Bodies dropped onto the stack per second by the demo scene.
        int spawnRate = 2;
        bool paused = false;
        bool showGrid = true;
    };

    Player();
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    bool start(const Config& config, std::string* error = nullptr);
    void shutdown();
    bool running() const { return running_.load(); }

    /// Advances by [frameTime] seconds of wall clock. Runs zero or more fixed
    /// simulation steps, then renders. Returns Finished once the frame budget
    /// or an exit request is reached.
    /// Advances the simulation by [frameTime] and, unless [renderFrame] is
    /// false, draws the frame. A host that only streams at 15 fps passes false
    /// on the frames it will throw away: the software rasterizer is the most
    /// expensive thing in the process and an unseen frame is pure waste.
    RunResult update(double frameTime, bool renderFrame = true);

    /// Renders the current state without advancing it. Used by the editor's
    /// screenshot endpoint, which must not disturb the simulation.
    void render();

    /// The frame most recently rendered. Null until the first update().
    const Framebuffer* frame() const { return &framebuffer_; }

    /// The current frame as a PNG. [out] is replaced. This is the expensive
    /// call — roughly a millisecond for a 960x540 frame — so the stream only
    /// pays for it when somebody is actually watching.
    bool capturePng(std::vector<uint8_t>& out);

    // --- driving the game from outside ---------------------------------------
    /// Feeds one frame of input. The caller owns the input state; the runtime
    /// copies what it needs. [frameTime] is the same value passed to update().
    void submitInput(const InputState& state, double frameTime);

    /// A console/editor command: "pause", "resume", "spawn", "reset",
    /// "grid on|off", "width 1280", "step". Returns false for an unknown
    /// command or bad argument, with the reason in [error].
    bool applyCommand(const std::string& command, std::string* error = nullptr);

    /// A description of the running game, as JSON, for the editor UI and the
    /// console. Always valid JSON, even at frame zero.
    std::string describe() const;

    /// Per-frame JSON telemetry: frame, fps, timings, counts, and the body
    /// list the editor draws on top of the video.
    std::string telemetry() const;

    /// The action map, so the editor can show what the game listens for.
    const ActionMap& actions() const { return actions_; }
    const InputState& inputState() const { return input_; }
    const Config& config() const { return config_; }
    const Camera& camera() const { return camera_; }
    PlayerStats stats() const;
    const physics2d::World& physics() const { return *physics_; }
    const Scene& scene() const { return *scene_; }

private:
    struct Material;
    /// A piece of level geometry: drawn, never simulated.
    struct LevelPiece {
        Vec2 center{0, 0};
        Vec2 size{1, 1};
        Color tint{1, 1, 1, 1};
    };
    struct Visual {
        physics2d::BodyId body = physics2d::kInvalidBody;
        NodeId node = kInvalidNode;
        int material = 0;
        Entity entity{};
    };

    void buildScene();
    void resetScene();
    void addVisual(const std::string& name, physics2d::BodyId body, int material, Vec2 size,
                   NodeId parent);
    void removeVisual(Visual& v);
    void spawnStack();
    void simulate(float dt);
    void submitRender();
    void refreshCamera();
    void syncVisuals();

    mutable std::mutex mutex_;
    Config config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> exitRequested_{false};
    bool started_ = false;

    std::unique_ptr<Scene> scene_;
    std::unique_ptr<World> ecs_;
    std::unique_ptr<physics2d::World> physics_;
    InputState input_;
    ActionMap actions_;
    Clock clock_;
    FixedTimeStepper stepper_;
    Camera camera_;

    Framebuffer framebuffer_;
    Renderer2D renderer_;
    std::vector<Visual> visuals_;
    std::vector<LevelPiece> level_;
    std::vector<SpriteItem> sprites_;
    std::vector<Material> materials_;
    Image white_;
    Image grid_;
    Image crate_;
    Image ball_;
    Image sky_;
    Image slab_;
    Random rand_;

    PlayerStats stats_;
    double spawnAccumulator_ = 0.0;
    int frameBudget_ = 0;
};

}  // namespace app
}  // namespace nova

#endif  // NOVA_APP_PLAYER_H