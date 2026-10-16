// Nova Engine — system scheduling.
//
// A system is a named update function with an explicit execution stage. The
// scheduler runs stages in a fixed order (input → gameplay → physics →
// animation → audio → render) so behaviour does not depend on registration
// order, and records per-system timings for the profiler.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ecs/World.h"

namespace nova {

/// Fixed pipeline stages, in execution order. Systems run stage by stage so
/// physics always sees this frame's input and rendering always sees the
/// post-physics transforms.
enum class Stage : int {
    Input = 0,
    Gameplay,
    Physics,
    PostPhysics,
    Animation,
    Audio,
    Late,       ///< camera follow, UI, anything after simulation
    Count
};

const char* toString(Stage stage);

class System {
public:
    using UpdateFn = std::function<void(World& world, float dt)>;

    System(std::string name, Stage stage, UpdateFn fn)
        : name_(std::move(name)), stage_(stage), fn_(std::move(fn)) {}

    const std::string& name() const { return name_; }
    Stage stage() const { return stage_; }
    bool enabled() const { return enabled_; }
    void setEnabled(bool on) { enabled_ = on; }
    /// Lower runs earlier within a stage.
    void setOrder(int order) { order_ = order; }
    int order() const { return order_; }

    void update(World& world, float dt) const {
        if (enabled_ && fn_) fn_(world, dt);
    }

    double totalMs = 0.0;
    double lastMs = 0.0;
    uint64_t calls = 0;

private:
    std::string name_;
    Stage stage_;
    UpdateFn fn_;
    bool enabled_ = true;
    int order_ = 0;
};

class SystemScheduler {
public:
    /// Registers a system. Re-registering the same name replaces it, which
    /// keeps hot-reload simple.
    void add(std::unique_ptr<System> system);
    void add(const std::string& name, Stage stage, System::UpdateFn fn);

    /// Sorts by (stage, order) — call after adding.
    void sort();
    /// Removes every system (used when reloading a scene or hot-reloading).
    void clear();
    size_t size() const { return systems_.size(); }
    bool empty() const { return systems_.empty(); }
    void setEnabled(const std::string& name, bool on);
    System* find(const std::string& name);
    std::vector<System*> all() const;

    /// Runs every enabled system in stage order. [filter], when set, runs
    /// only those stages (the editor's "physics only" play mode step).
    void update(World& world, float dt, Stage from = Stage::Input, Stage to = Stage::Late);

    struct Entry {
        std::string name;
        Stage stage;
        double lastMs;
        double totalMs;
        uint64_t calls;
    };
    std::vector<Entry> profile() const;
    void resetProfile();

private:
    std::vector<std::unique_ptr<System>> systems_;
    bool sorted_ = true;
};

}  // namespace nova
