#include "app/Player.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "base/Json.h"
#include "base/Log.h"
#include "base/Random.h"

namespace nova {
namespace app {

// --- game components --------------------------------------------------------
//
// The runtime keeps its own state in the ECS rather than in parallel vectors,
// so systems can query it the same way game code would. These are the two
// pieces of state a sprite-backed 2D object needs.

/// Links an entity to the physics body that drives it.
struct BodyLink {
    physics2d::BodyId body = physics2d::kInvalidBody;
    uint32_t userData = 0;
};

/// What to draw for an entity.
struct Sprite {
    Vec2 size{1, 1};
    Color tint{1, 1, 1, 1};
    /// Index into the material table; -1 draws an untextured quad.
    int material = -1;
    float sortKey = 0.0f;
};

NOVA_COMPONENT(BodyLink);
NOVA_COMPONENT(Sprite);

namespace {

constexpr float kWorldHalfWidth = 10.0f;

/// The textures the demo scene draws with, generated at start-up so the engine
/// ships with no binary assets and still has something to look at.
void buildTextures(Image& white, Image& grid, Image& crate, Image& ball, Image& sky,
                   Image& slab) {
    white = Image(4, 4, Color(1, 1, 1, 1));

    sky = Image(64, 36);
    const Color skyTop(0.10f, 0.12f, 0.20f, 1.0f);
    const Color skyBottom(0.17f, 0.20f, 0.30f, 1.0f);
    for (int y = 0; y < sky.height; ++y) {
        const float t = static_cast<float>(y) / static_cast<float>(sky.height - 1);
        const Color c(skyTop.r + (skyBottom.r - skyTop.r) * t,
                      skyTop.g + (skyBottom.g - skyTop.g) * t,
                      skyTop.b + (skyBottom.b - skyTop.b) * t, 1.0f);
        for (int x = 0; x < sky.width; ++x) sky.set(x, y, c);
    }

    grid = Image(64, 64);
    grid.fill(Color(0.19f, 0.22f, 0.31f, 1.0f));
    for (int i = 0; i < 64; ++i) {
        grid.set(i, 0, Color(0.25f, 0.29f, 0.40f, 1.0f));
        grid.set(0, i, Color(0.25f, 0.29f, 0.40f, 1.0f));
    }

    crate = Image(64, 64);
    crate.fill(Color(0.80f, 0.56f, 0.29f, 1.0f));
    crate.drawRectOutline(0, 0, 64, 64, Color(0.54f, 0.35f, 0.16f, 1.0f), 4);
    crate.drawLine(Vec2(7, 7), Vec2(57, 57), Color(0.63f, 0.43f, 0.20f, 1.0f), 3);
    crate.drawLine(Vec2(57, 7), Vec2(7, 57), Color(0.63f, 0.43f, 0.20f, 1.0f), 3);

    slab = Image(32, 32);
    slab.fill(Color(0.30f, 0.34f, 0.45f, 1.0f));
    slab.fillRect(0, 0, 32, 5, Color(0.42f, 0.47f, 0.60f, 1.0f));
    slab.fillRect(0, 30, 32, 2, Color(0.19f, 0.22f, 0.30f, 1.0f));

    ball = Image(64, 64);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const float dx = static_cast<float>(x) - 31.5f;
            const float dy = static_cast<float>(y) - 31.5f;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float rim = std::min(1.0f, std::max(0.0f, (31.5f - dist) / 2.5f));
            const float shade = 0.55f + 0.45f * std::min(1.0f, 1.0f - dist / 38.0f);
            ball.set(x, y, Color(0.25f * shade + 0.55f * rim, 0.60f * shade + 0.2f * rim,
                                0.95f * shade, 1.0f));
        }
    }
}

}  // namespace

struct Player::Material {
    std::string name;
    Color tint{1, 1, 1, 1};
};

Player::Player() = default;

Player::~Player() { shutdown(); }

bool Player::start(const Config& config, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) {
        if (error) *error = "already started";
        return false;
    }
    if (config.width < 16 || config.height < 16 || config.width > 4096 ||
        config.height > 4096) {
        if (error) *error = "resolution must be between 16 and 4096";
        return false;
    }
    if (!(config.fixedDelta > 0.0f)) {
        if (error) *error = "fixedDelta must be positive";
        return false;
    }
    config_ = config;
    frameBudget_ = 0;
    exitRequested_ = false;
    stats_ = PlayerStats();

    buildTextures(white_, grid_, crate_, ball_, sky_, slab_);
    materials_.clear();
    materials_.push_back(Material{"player", Color(0.55f, 0.74f, 1.0f, 1.0f)});
    materials_.push_back(Material{"ball", Color(1, 1, 1, 1)});
    materials_.push_back(Material{"crate", Color(1, 1, 1, 1)});
    materials_.push_back(Material{"ground", Color(0.35f, 0.40f, 0.52f, 1.0f)});

    scene_.reset(new Scene());
    ecs_.reset(new World());
    physics_.reset(new physics2d::World(Vec2(0.0f, -9.81f)));
    // A ball that lands and starts rolling is, by the laws of frictionless
    // rolling, supposed to coast forever. On a real floor it does not, and
    // without this the stack never stops moving.
    physics_->rollingFriction = 0.35f;
    rand_.reseed(0x4e6f7661ULL);

    actions_.clear();
    actions_.bind("left", {input::Binding(input::Key::A), input::Binding(input::Key::Left)},
                  -1.0f, 1.0f);
    actions_.bind("right", {input::Binding(input::Key::D), input::Binding(input::Key::Right)},
                  -1.0f, 1.0f);
    actions_.bind("push", {input::Binding(input::Key::Space)});
    actions_.bind("reset", {input::Binding(input::Key::R)});

    buildScene();

    framebuffer_.resize(config_.width, config_.height);
    refreshCamera();
    stepper_.setFixedDelta(config_.fixedDelta);
    stepper_.setMaxFrameTime(config_.maxFrameTime);
    stepper_.reset();
    clock_.reset();
    spawnAccumulator_ = 0.0;
    started_ = true;
    running_ = true;
    render();
    return true;
}

void Player::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    started_ = false;
    visuals_.clear();
    sprites_.clear();
    scene_.reset();
    ecs_.reset();
    physics_.reset();
    framebuffer_ = Framebuffer();
}

void Player::buildScene() {
    const NodeId world = scene_->createNode("World");

    auto addStatic = [&](const std::string& name, Vec2 half, Vec2 pos, float friction) {
        physics2d::Body b;
        b.type = physics2d::BodyType::Static;
        b.position = pos;
        b.shape = physics2d::ColliderShape::Box;
        b.polygon = physics2d::ConvexPolygon::box(half.x, half.y);
        b.friction = friction;
        b.restitution = 0.05f;
        b.tag = name;
        b.userData = 1;
        return physics_->createBody(b);
    };

    // Level geometry is simulated and drawn separately: a static body has no
    // entity, so it never appears in the ECS sprite pass.
    level_.clear();
    level_.push_back(LevelPiece{Vec2(0.0f, -0.5f), Vec2((kWorldHalfWidth + 2.0f) * 2.0f, 1.0f),
                               Color(0.40f, 0.45f, 0.58f, 1.0f)});
    addStatic("Ground", Vec2(kWorldHalfWidth + 2.0f, 0.5f), Vec2(0.0f, -0.5f), 0.7f);
    level_.push_back(LevelPiece{Vec2(-kWorldHalfWidth - 0.5f, 7.5f), Vec2(1.0f, 16.0f),
                               Color(0.27f, 0.30f, 0.40f, 1.0f)});
    addStatic("WallLeft", Vec2(0.5f, 8.0f), Vec2(-kWorldHalfWidth - 0.5f, 7.5f), 0.2f);
    level_.push_back(LevelPiece{Vec2(kWorldHalfWidth + 0.5f, 7.5f), Vec2(1.0f, 16.0f),
                               Color(0.27f, 0.30f, 0.40f, 1.0f)});
    addStatic("WallRight", Vec2(0.5f, 8.0f), Vec2(kWorldHalfWidth + 0.5f, 7.5f), 0.2f);

    // The pusher is a heavy, strongly damped box the player drives with input.
    physics2d::Body pusher;
    pusher.type = physics2d::BodyType::Dynamic;
    pusher.shape = physics2d::ColliderShape::Box;
    pusher.polygon = physics2d::ConvexPolygon::box(0.6f, 1.6f);
    pusher.position = Vec2(0.0f, 1.6f);
    pusher.mass.mass = 40.0f;
    pusher.friction = 0.6f;
    pusher.restitution = 0.0f;
    pusher.linearDamping = 6.0f;
    pusher.tag = "Pusher";
    pusher.userData = 2;
    const physics2d::BodyId pusherId = physics_->createBody(pusher);
    physics_->updateMassFromShape(pusherId);

    addVisual("Pusher", pusherId, 0, Vec2(1.2f, 3.2f), world);
    for (int i = 0; i < 6; ++i) spawnStack();
}

void Player::addVisual(const std::string& name, physics2d::BodyId body, int material, Vec2 size,
                       NodeId parent) {
    const NodeId node = scene_->createNode(name, parent);
    const Entity e = ecs_->create();
    scene_->setEntity(node, e);
    ecs_->set<BodyLink>(e, BodyLink{body, static_cast<uint32_t>(visuals_.size())});
    ecs_->set<Sprite>(e, Sprite{size, materials_[material].tint, material,
                                static_cast<float>(visuals_.size())});
    visuals_.push_back(Visual{body, node, material, e});
}

void Player::removeVisual(Visual& v) {
    if (ecs_ && ecs_->alive(v.entity)) ecs_->destroy(v.entity);
    if (scene_->valid(v.node)) scene_->destroyNode(v.node);
}

void Player::spawnStack() {
    if (!physics_ || !ecs_) return;
    const int index = static_cast<int>(visuals_.size());
    const bool isBall = (index % 3) == 2;

    physics2d::Body b;
    b.type = physics2d::BodyType::Dynamic;
    b.shape = isBall ? physics2d::ColliderShape::Circle : physics2d::ColliderShape::Box;
    if (isBall) {
        b.radius = 0.45f;
    } else {
        b.polygon = physics2d::ConvexPolygon::box(0.45f, 0.45f);
    }
    b.position = Vec2(rand_.range(-3.5f, 3.5f), 5.0f + static_cast<float>(index) * 1.02f);
    b.rotationDeg = rand_.range(-8.0f, 8.0f);
    b.mass.mass = isBall ? 1.6f : 2.2f;
    b.friction = 0.55f;
    b.restitution = isBall ? 0.45f : 0.05f;
    b.tag = isBall ? "Ball" : "Crate";
    b.userData = 10 + index;

    const physics2d::BodyId id = physics_->createBody(b);
    physics_->updateMassFromShape(id);
    addVisual(b.tag, id, isBall ? 1 : 2, Vec2(0.9f, 0.9f), kInvalidNode);
}

RunResult Player::update(double frameTime, bool renderFrame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_) return RunResult::Finished;
    if (exitRequested_) {
        running_ = false;
        return RunResult::Finished;
    }
    if (!(frameTime > 0.0) || frameTime > config_.maxFrameTime * 4.0) frameTime = config_.fixedDelta;

    if (!config_.paused) {
        const int steps = stepper_.advance(frameTime);
        const double s0 = nowSeconds();
        for (int i = 0; i < steps; ++i) simulate(stepper_.fixedDelta());
        stats_.simulateMs = nowSeconds() - s0;
        if (steps == 0) stats_.simulateMs = 0.0;
    } else {
        stats_.simulateMs = 0.0;
    }

    if (renderFrame) render();

    clock_.tick();
    clock_.sampleFps(frameTime);
    stats_.frame += 1;
    stats_.elapsedSeconds = clock_.elapsed();
    stats_.fps = clock_.fps();
    stats_.frameMs = frameTime * 1000.0;
    stats_.viewWidth = config_.width;
    stats_.viewHeight = config_.height;
    stats_.paused = config_.paused;

    if (frameBudget_ > 0 && stats_.frame >= static_cast<uint64_t>(frameBudget_)) {
        exitRequested_ = true;
    }
    return exitRequested_ ? RunResult::Finished : RunResult::Running;
}

void Player::simulate(float dt) {
    input_.beginFrame(dt);
    actions_.update(input_, dt);

    if (actions_.pressed("reset")) resetScene();
    if (actions_.pressed("push")) spawnStack();

    // The pusher is driven by velocity rather than force: a character
    // controller wants to answer "move at 7 m/s now", not "accumulate until
    // the mass happens to work out".
    ecs_->each<BodyLink>([&](Entity, BodyLink& link) {
        const physics2d::Body* b = physics_->body(link.body);
        if (!b || b->tag != "Pusher") return;
        float target = 0.0f;
        if (actions_.down("left")) target -= 7.0f;
        if (actions_.down("right")) target += 7.0f;
        // Writing the same velocity every frame would reset the sleep timer
        // forever, so an idle pusher would never settle. Only touch it when
        // something actually changed.
        if (target == 0.0f && b->sleeping && b->linearVelocity.x == 0.0f) return;
        if (b->linearVelocity.x == target && b->angularVelocity == 0.0f) return;
        physics_->setVelocity(link.body, Vec2(target, b->linearVelocity.y));
    });

    physics_->step(dt);

    spawnAccumulator_ += dt * static_cast<double>(std::max(0, config_.spawnRate));
    while (spawnAccumulator_ >= 1.0) {
        spawnAccumulator_ -= 1.0;
        if (static_cast<int>(visuals_.size()) < 48) spawnStack();
    }
    if (spawnAccumulator_ > 8.0) spawnAccumulator_ = 8.0;

    input_.endFrame();
}

void Player::resetScene() {
    for (size_t i = visuals_.size(); i-- > 0;) {
        Visual& v = visuals_[i];
        if (v.material == 0) continue;
        const physics2d::Body* b = physics_->body(v.body);
        if (b && b->tag != "Pusher") physics_->destroyBody(v.body);
        removeVisual(v);
        visuals_.erase(visuals_.begin() + static_cast<long>(i));
    }
    for (int i = 0; i < 6; ++i) spawnStack();
}

void Player::refreshCamera() {
    camera_ = Camera();
    camera_.mode = ProjectionMode::Orthographic;
    camera_.aspect = static_cast<float>(config_.width) /
                     static_cast<float>(std::max(1, config_.height));
    camera_.orthoSize = 7.0f;
    camera_.position = Vec3(0.0f, 4.2f, 20.0f);
    camera_.target = Vec3(0.0f, 4.2f, 0.0f);
    camera_.nearPlane = 0.1f;
    camera_.farPlane = 100.0f;
    camera_.clearColor = Color(0.07f, 0.08f, 0.12f, 1.0f);
}

void Player::syncVisuals() {
    for (const Visual& v : visuals_) {
        const physics2d::Body* b = physics_->body(v.body);
        if (!b || !scene_->valid(v.node)) continue;
        scene_->setWorldPosition(v.node, Vec3(b->position.x, b->position.y, 0.0f));
        scene_->setWorldRotationZ(v.node, b->rotationDeg);
    }
}

void Player::submitRender() {
    sprites_.clear();
    sprites_.reserve(visuals_.size() + 4);

    SpriteItem bg;
    bg.center = Vec2(0.0f, 4.2f);
    bg.size = Vec2(kWorldHalfWidth * 2.4f, kWorldHalfWidth * 2.4f);
    bg.texture = &sky_;
    bg.color = Color(1, 1, 1, 1);
    bg.sortKey = -1000.0f;
    sprites_.push_back(bg);

    if (config_.showGrid) {
        SpriteItem g;
        g.center = Vec2(0.0f, 4.2f);
        g.size = Vec2(kWorldHalfWidth * 2.0f, kWorldHalfWidth * 2.0f);
        g.texture = &grid_;
        g.color = Color(1, 1, 1, 1);
        g.sortKey = -900.0f;
        // The grid is a tile; without Repeat it would stretch across the world
        // and the lines would end up in the wrong places.
        g.wrap = render::SpriteWrap::Repeat;
        // One grid cell per world unit, so the lines land on round coordinates.
        g.tile = Vec2(kWorldHalfWidth * 2.0f, kWorldHalfWidth * 2.0f);
        sprites_.push_back(g);
    }

    for (const LevelPiece& piece : level_) {
        SpriteItem s;
        s.center = piece.center;
        s.size = piece.size;
        s.color = piece.tint;
        s.texture = &slab_;
        s.sortKey = -800.0f;
        s.wrap = render::SpriteWrap::Repeat;
        sprites_.push_back(s);
    }

    // Walk the ECS rather than the parallel vector: this is the same path game
    // code takes, so a bug here is a bug in the engine, not in the demo.
    ecs_->each<Sprite, BodyLink>(
        [&](Entity e, Sprite& sprite, const BodyLink& link) {
            (void)e;
            const physics2d::Body* b = physics_->body(link.body);
            if (!b) return;
            SpriteItem s;
            s.center = Vec2(b->position.x, b->position.y);
            s.size = sprite.size;
            s.rotationDeg = b->rotationDeg;
            s.color = sprite.tint;
            if (sprite.material == 1) {
                s.texture = &ball_;
            } else if (sprite.material == 2) {
                s.texture = &crate_;
            } else if (sprite.material == 3) {
                s.texture = &crate_;
            } else {
                s.texture = &white_;
            }
            s.sortKey = b->position.y;
            sprites_.push_back(s);
        });
}

void Player::render() {
    if (!started_) return;
    const double r0 = nowSeconds();
    syncVisuals();
    submitRender();

    RenderSettings settings;
    settings.depthTest = false;  // a 2D stack is painter's-algorithm, not depth
    renderer_.render(framebuffer_, camera_, settings, sprites_);

    const physics2d::WorldStats ps = physics_->stats();
    stats_.bodies = ps.bodies;
    stats_.activeBodies = ps.activeBodies;
    stats_.contacts = ps.contacts;
    stats_.drawCalls = renderer_.stats().itemsDrawn;
    stats_.renderMs = nowSeconds() - r0;
}

bool Player::capturePng(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_) return false;
    const double e0 = nowSeconds();
    out = encodePng(framebuffer_.color);
    stats_.encodeMs = nowSeconds() - e0;
    return !out.empty();
}

void Player::submitInput(const InputState& state, double frameTime) {
    std::lock_guard<std::mutex> lock(mutex_);
    // The whole state is replaced rather than merged: a lost or dropped event
    // then shows up as a stuck-up key the next frame instead of sticking
    // forever.
    input_ = state;
    (void)frameTime;
}

bool Player::applyCommand(const std::string& command, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_) {
        if (error) *error = "not running";
        return false;
    }
    std::istringstream in(command);
    std::string verb;
    in >> verb;
    for (char& c : verb) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    std::string rest;
    std::getline(in, rest);
    while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);

    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };

    if (verb == "pause") {
        config_.paused = true;
    } else if (verb == "resume") {
        config_.paused = false;
    } else if (verb == "step") {
        simulate(stepper_.fixedDelta());
        render();
    } else if (verb == "spawn") {
        spawnStack();
    } else if (verb == "reset") {
        resetScene();
        render();
    } else if (verb == "grid") {
        if (rest == "on") {
            config_.showGrid = true;
        } else if (rest == "off") {
            config_.showGrid = false;
        } else {
            return fail("usage: grid on|off");
        }
        render();
    } else if (verb == "gravity") {
        physics_->gravity.y = static_cast<float>(std::strtod(rest.c_str(), nullptr));
    } else if (verb == "width" || verb == "height") {
        const long v = std::strtol(rest.c_str(), nullptr, 10);
        if (v < 16 || v > 4096) return fail("size must be between 16 and 4096");
        if (verb == "width") {
            config_.width = static_cast<int>(v);
        } else {
            config_.height = static_cast<int>(v);
        }
        framebuffer_.resize(config_.width, config_.height);
        refreshCamera();
        render();
    } else if (verb == "exit" || verb == "quit") {
        exitRequested_ = true;
    } else if (verb == "help") {
        if (error) {
            *error = "pause|resume|step|spawn|reset|grid on|off|gravity N|width N|height N|exit";
        }
        return false;
    } else {
        return fail("unknown command");
    }
    return true;
}

std::string Player::describe() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Json root = Json::object();
    root["engine"] = std::string("nova");
    root["version"] = std::string("0.9.0");
    root["running"] = running_.load();
    root["paused"] = config_.paused;
    root["grid"] = config_.showGrid;
    root["width"] = config_.width;
    root["height"] = config_.height;
    root["fixedDelta"] = static_cast<double>(config_.fixedDelta);
    root["spawnRate"] = config_.spawnRate;
    root["elapsed"] = stats_.elapsedSeconds;
    root["frame"] = static_cast<double>(stats_.frame);
    root["fps"] = stats_.fps;

    Json actions = Json::array();
    for (const std::string& name : actions_.names()) actions.push(name);
    root["actions"] = actions;

    Json keys = Json::array();
    for (int i = 0; i < static_cast<int>(input::Key::Count); ++i) {
        const input::Key k = static_cast<input::Key>(i);
        if (input_.keyDown(k)) keys.push(input::toString(k));
    }
    root["keysDown"] = keys;
    return root.dump(2);
}

std::string Player::telemetry() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Json root = Json::object();
    root["frame"] = static_cast<double>(stats_.frame);
    root["elapsed"] = stats_.elapsedSeconds;
    root["fps"] = stats_.fps;
    root["frameMs"] = stats_.frameMs;
    root["simulateMs"] = stats_.simulateMs;
    root["renderMs"] = stats_.renderMs;
    root["encodeMs"] = stats_.encodeMs;
    root["bodies"] = stats_.bodies;
    root["activeBodies"] = stats_.activeBodies;
    root["contacts"] = stats_.contacts;
    root["drawCalls"] = stats_.drawCalls;
    root["paused"] = config_.paused;

    Json bodies = Json::array();
    for (const Visual& v : visuals_) {
        const physics2d::Body* b = physics_->body(v.body);
        if (!b) continue;
        Json e = Json::object();
        e["tag"] = b->tag;
        e["x"] = static_cast<double>(b->position.x);
        e["y"] = static_cast<double>(b->position.y);
        e["angle"] = static_cast<double>(b->rotationDeg);
        e["sleeping"] = b->sleeping;
        bodies.push(e);
    }
    root["bodies"] = bodies;
    return root.dump(0);
}

PlayerStats Player::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    PlayerStats s = stats_;
    s.viewWidth = config_.width;
    s.viewHeight = config_.height;
    // The body counts are read live rather than from the last rendered frame:
    // a command that adds a body has to be visible before the next render.
    if (physics_) {
        // bodyCount() is live; World::stats() only reflects the last step, so a
        // body added by a command between steps would not show up there.
        s.bodies = static_cast<int>(physics_->bodyCount());
        s.activeBodies = physics_->stats().activeBodies;
        s.contacts = physics_->stats().contacts;
    }
    return s;
}

}  // namespace app
}  // namespace nova