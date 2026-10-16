#include "ecs/World.h"

#include <algorithm>
#include <cstring>

#include "base/Log.h"

namespace nova {

// ---------------------------------------------------------------------------
// Entity
// ---------------------------------------------------------------------------

std::string toString(const Entity& e) {
    if (!e.valid()) return "Entity(null)";
    return std::to_string(e.index) + ":" + std::to_string(e.generation);
}

// ---------------------------------------------------------------------------
// ComponentRegistry
// ---------------------------------------------------------------------------

ComponentRegistry& ComponentRegistry::instance() {
    static ComponentRegistry registry;
    return registry;
}

bool ComponentRegistry::hasName(const std::string& name) const {
    return byName_.find(name) != byName_.end();
}

ComponentId ComponentRegistry::idFor(const std::string& name) const {
    const auto it = byName_.find(name);
    return it == byName_.end() ? kInvalidComponent : it->second;
}

// ---------------------------------------------------------------------------
// EntityView
// ---------------------------------------------------------------------------

bool EntityView::valid() const { return world_ && world_->alive(entity_); }

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------

World::World() = default;

World::~World() {
    // Run destructors for every live component instance so types holding
    // owned resources (strings, vectors) do not leak at shutdown.
    for (Storage& s : storages_) {
        if (s.elementSize == 0 || s.present.empty()) continue;
        const ComponentType& info = ComponentRegistry::instance().info(s.type);
        if (info.trivial) continue;
        for (uint32_t slot : s.present.dense()) {
            info.destruct(s.bytes.data() + static_cast<size_t>(slot) * s.elementSize);
        }
    }
}

Entity World::create() {
    uint32_t slot;
    if (!freeList_.empty()) {
        // Reuse the most recently freed slot: keeps hot entities contiguous.
        slot = freeList_.back();
        freeList_.pop_back();
    } else {
        slot = static_cast<uint32_t>(slots_.size());
        if (slot == kInvalidIndex) {
            NOVA_ERROR("ecs", "entity space exhausted");
            return Entity::invalid();
        }
        slots_.emplace_back();
        // Growing the world grows every component column so that slot indices
        // are always in range; adding a component never reallocates per-entity.
        for (Storage& s : storages_) s.present.resize(slots_.size());
    }
    Slot& s = slots_[slot];
    s.alive = true;
    ++liveCount_;
    liveList_.push_back(slot);
    return Entity(slot, s.generation);
}

void World::destroy(Entity e) {
    if (!alive(e)) return;
    const uint32_t slot = e.index;
    for (Storage& s : storages_) removeRaw(e, s.type);
    slots_[slot].alive = false;
    // Bumping the generation is what makes every previously handed-out handle
    // for this slot permanently dead, even after the slot is recycled.
    ++slots_[slot].generation;
    if (slots_[slot].generation == 0) slots_[slot].generation = 1;  // 0 means "null"
    freeList_.push_back(slot);
    liveList_.erase(std::remove(liveList_.begin(), liveList_.end(), slot), liveList_.end());
    --liveCount_;
}

bool World::alive(Entity e) const {
    return e.valid() && e.index < slots_.size() && slots_[e.index].alive &&
           slots_[e.index].generation == e.generation;
}

World::Storage& World::storage(ComponentId type) {
    const auto it = storageIndex_.find(type);
    if (it != storageIndex_.end()) return storages_[it->second];
    Storage s;
    s.type = type;
    s.elementSize = ComponentRegistry::instance().info(type).size;
    s.present.resize(slots_.size());
    storages_.push_back(std::move(s));
    storageIndex_[type] = storages_.size() - 1;
    return storages_.back();
}

const World::Storage* World::findStorage(ComponentId type) const {
    const auto it = storageIndex_.find(type);
    return it == storageIndex_.end() ? nullptr : &storages_[it->second];
}

bool World::has(Entity e, ComponentId type) const {
    const Storage* s = findStorage(type);
    return s && s->present.contains(e.index);
}

void* World::getPtr(Entity e, ComponentId type) const {
    // getPtr is const so it can be reached from const systems and from
    // EntityView, but it hands out a mutable pointer: the component storage
    // is owned by the world, not by the accessor.
    const Storage* s = findStorage(type);
    if (!s || !s->present.contains(e.index)) return nullptr;
    return const_cast<uint8_t*>(s->bytes.data()) + static_cast<size_t>(e.index) * s->elementSize;
}

const void* World::rawData(Entity e, ComponentId type, size_t& outSize) const {
    const Storage* s = findStorage(type);
    outSize = s ? s->elementSize : 0;
    return getPtr(e, type);
}

void World::addRaw(Entity e, ComponentId type) {
    if (!alive(e)) return;
    Storage& s = storage(type);
    if (s.present.contains(e.index)) return;

    const size_t needed = (static_cast<size_t>(e.index) + 1) * s.elementSize;
    if (s.bytes.size() < needed) s.bytes.resize(needed);

    const ComponentType& info = ComponentRegistry::instance().info(type);
    void* mem = s.bytes.data() + static_cast<size_t>(e.index) * s.elementSize;
    if (info.trivial) std::memset(mem, 0, info.size);
    else info.construct(mem);
    s.present.insert(e.index);
}

void World::removeRaw(Entity e, ComponentId type) {
    if (!alive(e)) return;
    Storage* s = nullptr;
    const auto it = storageIndex_.find(type);
    if (it == storageIndex_.end()) return;
    s = &storages_[it->second];
    if (!s->present.contains(e.index)) return;
    const ComponentType& info = ComponentRegistry::instance().info(type);
    if (!info.trivial) {
        info.destruct(s->bytes.data() + static_cast<size_t>(e.index) * s->elementSize);
    }
    s->present.erase(e.index);
}

void World::setRaw(Entity e, ComponentId type, const void* src) {
    if (!alive(e) || !src) return;
    void* dst = getPtr(e, type);
    if (dst) {
        std::memcpy(dst, src, ComponentRegistry::instance().info(type).size);
    } else {
        addRaw(e, type);
        dst = getPtr(e, type);
        if (dst) std::memcpy(dst, src, ComponentRegistry::instance().info(type).size);
    }
}

std::vector<Entity> World::findWithComponent(const std::string& componentName) const {
    std::vector<Entity> out;
    const ComponentId id = ComponentRegistry::instance().idFor(componentName);
    if (id == kInvalidComponent) return out;
    const Storage* s = findStorage(id);
    if (!s) return out;
    out.reserve(s->present.size());
    for (uint32_t slot : s->present.dense()) {
        const Entity e = entityAt(slot);
        if (alive(e)) out.push_back(e);
    }
    std::sort(out.begin(), out.end());
    return out;
}

size_t World::componentMemoryBytes() const {
    size_t total = 0;
    for (const Storage& s : storages_) total += s.bytes.size();
    return total;
}

World::StorageStats World::stats() const {
    StorageStats st;
    st.entities = liveCount_;
    st.componentTypes = storages_.size();
    for (const Storage& s : storages_) {
        st.totalComponents += s.present.size();
        st.bytes += s.bytes.capacity();
    }
    return st;
}

}  // namespace nova
