#include "ecs/System.h"

#include <algorithm>

#include "base/Time.h"

namespace nova {

const char* toString(Stage stage) {
    switch (stage) {
        case Stage::Input:      return "Input";
        case Stage::Gameplay:   return "Gameplay";
        case Stage::Physics:    return "Physics";
        case Stage::PostPhysics: return "PostPhysics";
        case Stage::Animation:  return "Animation";
        case Stage::Audio:      return "Audio";
        case Stage::Late:       return "Late";
        default:                return "?";
    }
}

void SystemScheduler::add(std::unique_ptr<System> system) {
    const std::string name = system->name();
    for (auto& s : systems_) {
        if (s->name() == name) {
            s = std::move(system);  // hot-reload replacement
            sorted_ = false;
            return;
        }
    }
    systems_.push_back(std::move(system));
    sorted_ = false;
}

void SystemScheduler::add(const std::string& name, Stage stage, System::UpdateFn fn) {
    add(std::make_unique<System>(name, stage, std::move(fn)));
}

void SystemScheduler::sort() {
    std::stable_sort(systems_.begin(), systems_.end(),
                     [](const std::unique_ptr<System>& a, const std::unique_ptr<System>& b) {
                         if (a->stage() != b->stage()) {
                             return static_cast<int>(a->stage()) < static_cast<int>(b->stage());
                         }
                         return a->order() < b->order();
                     });
    sorted_ = true;
}

void SystemScheduler::clear() { systems_.clear(); }

void SystemScheduler::setEnabled(const std::string& name, bool on) {
    if (System* s = find(name)) s->setEnabled(on);
}

System* SystemScheduler::find(const std::string& name) {
    for (auto& s : systems_) {
        if (s->name() == name) return s.get();
    }
    return nullptr;
}

std::vector<System*> SystemScheduler::all() const {
    std::vector<System*> out;
    out.reserve(systems_.size());
    for (const auto& s : systems_) out.push_back(s.get());
    return out;
}

void SystemScheduler::update(World& world, float dt, Stage from, Stage to) {
    if (!sorted_) sort();
    for (auto& s : systems_) {
        if (s->stage() < from || s->stage() > to) continue;
        if (!s->enabled()) continue;
        const double t0 = nowSeconds();
        s->update(world, dt);
        const double ms = (nowSeconds() - t0) * 1000.0;
        s->lastMs = ms;
        s->totalMs += ms;
        ++s->calls;
    }
}

std::vector<SystemScheduler::Entry> SystemScheduler::profile() const {
    std::vector<Entry> out;
    out.reserve(systems_.size());
    for (const auto& s : systems_) {
        out.push_back({s->name(), s->stage(), s->lastMs, s->totalMs, s->calls});
    }
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return a.totalMs > b.totalMs; });
    return out;
}

void SystemScheduler::resetProfile() {
    for (auto& s : systems_) {
        s->totalMs = 0.0;
        s->lastMs = 0.0;
        s->calls = 0;
    }
}

}  // namespace nova
