// Nova Engine — the entity world.
//
// Storage model
// -------------
// * Entities live in a slot array. A free list recycles indices and bumps the
//   generation so destroyed handles are permanently invalidated.
// * Each component type owns one contiguous `std::vector<uint8_t>` "column"
//   addressed by entity *slot*, plus a bitset of which slots hold the
//   component. This is the classic SoA layout: systems that touch one
//   component walk linear memory, and unrelated components never share a
//   cache line.
// * Queries are built from a required/any/excluded mask and iterate the
//   smallest matching set, so the cost is proportional to real matches
//   rather than to total entity count.
#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

#include "ecs/Component.h"
#include "ecs/Entity.h"

namespace nova {

/// A sparse set of slot indices, used for "entities that have component X".
class EntitySet {
public:
    /// Grows the index to cover [n] slots. MUST be non-destructive: the world
    /// calls this every time it adds an entity slot, so wiping membership here
    /// would silently orphan every component already assigned.
    void resize(size_t n) {
        if (n <= sparse_.size()) return;
        sparse_.resize(n, kInvalidIndex);
        present_.resize(n, false);
        // dense_ is deliberately left alone: it holds the slots that are live.
    }
    void insert(EntityIndex slot) {
        if (slot >= sparse_.size()) return;
        if (present_[slot]) return;
        present_[slot] = true;
        sparse_[slot] = static_cast<uint32_t>(dense_.size());
        dense_.push_back(slot);
    }
    void erase(EntityIndex slot) {
        if (slot >= sparse_.size() || !present_[slot]) return;
        const uint32_t pos = sparse_[slot];
        const uint32_t last = static_cast<uint32_t>(dense_.size()) - 1;
        if (pos != last) {
            dense_[pos] = dense_[last];
            sparse_[dense_[pos]] = pos;
        }
        dense_.pop_back();
        sparse_[slot] = kInvalidIndex;
        present_[slot] = false;
    }
    bool contains(EntityIndex slot) const {
        return slot < present_.size() && present_[slot];
    }
    const std::vector<uint32_t>& dense() const { return dense_; }
    size_t size() const { return dense_.size(); }
    bool empty() const { return dense_.empty(); }
    void clear() {
        dense_.clear();
        std::fill(present_.begin(), present_.end(), false);
        std::fill(sparse_.begin(), sparse_.end(), kInvalidIndex);
    }
    /// Adds [slot] to [out] if absent. Used to intersect query masks.
    void collectInto(std::vector<uint32_t>& out) const {
        out.insert(out.end(), dense_.begin(), dense_.end());
    }

private:
    std::vector<uint32_t> dense_;
    std::vector<uint32_t> sparse_;
    std::vector<bool> present_;
};

class World;

/// Read/write access to one entity's components, handed to systems.
class EntityView {
public:
    EntityView() = default;
    EntityView(World* w, Entity e) : world_(w), entity_(e) {}

    Entity entity() const { return entity_; }
    bool valid() const;

    template <typename T>
    T* get() const;
    template <typename T>
    T& getRef() const;
    template <typename T>
    bool has() const;
    template <typename T>
    T& add();
    template <typename T>
    void remove() const;
    template <typename T, typename... Args>
    T& emplace(Args&&... args) const;

    World* world() const { return world_; }

private:
    World* world_ = nullptr;
    Entity entity_{};
};

class World {
public:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // --- entities ---
    Entity create();
    void destroy(Entity e);
    bool alive(Entity e) const;
    size_t entityCount() const { return liveCount_; }
    size_t slotCapacity() const { return slots_.size(); }
    /// Iterates live entities in slot order.
    const std::vector<uint32_t>& liveSlots() const { return liveList_; }

    // --- components (type-erased) ---
    bool has(Entity e, ComponentId type) const;
    void* getPtr(Entity e, ComponentId type) const;
    void addRaw(Entity e, ComponentId type);
    void removeRaw(Entity e, ComponentId type);
    void setRaw(Entity e, ComponentId type, const void* src);
    /// Bytes of a component, for serialization. Null when absent.
    const void* rawData(Entity e, ComponentId type, size_t& outSize) const;

    // --- typed sugar ---
    template <typename T>
    bool has(Entity e) const { return has(e, ComponentRegistry::typeId<T>()); }
    template <typename T>
    T* get(Entity e) const {
        return static_cast<T*>(getPtr(e, ComponentRegistry::typeId<T>()));
    }
    template <typename T>
    T& add(Entity e) {
        const ComponentId id = ComponentRegistry::typeId<T>();
        addRaw(e, id);
        return *static_cast<T*>(getPtr(e, id));
    }
    template <typename T, typename... Args>
    T& emplace(Entity e, Args&&... args) {
        remove<T>(e);
        T fresh{std::forward<Args>(args)...};
        return set(e, fresh);
    }
    template <typename T>
    T& set(Entity e, const T& value) {
        const ComponentId id = ComponentRegistry::typeId<T>();
        if (!has(e, id)) addRaw(e, id);
        *static_cast<T*>(getPtr(e, id)) = value;
        return *static_cast<T*>(getPtr(e, id));
    }
    template <typename T>
    void remove(Entity e) { removeRaw(e, ComponentRegistry::typeId<T>()); }

    EntityView view(Entity e) const { return EntityView(const_cast<World*>(this), e); }

    // --- queries ---
    // The callable is a deduced template parameter rather than a
    // `const std::function<...>&`: a lambda cannot be implicitly converted to
    // std::function while the compiler is still substituting the Ts... pack.
    /// Iterates every entity holding all of [T...] (read-write).
    template <typename... Ts, typename Fn>
    void each(Fn&& fn) {
        eachImpl<Ts...>(std::function<void(Entity, Ts&...)>(std::forward<Fn>(fn)));
    }
    /// Iterates every entity holding all of [T...] (const access).
    template <typename... Ts, typename Fn>
    void eachConst(Fn&& fn) {
        eachConstImpl<Ts...>(std::function<void(Entity, const Ts&...)>(std::forward<Fn>(fn)));
    }
    /// Iterates entities holding at least one of [T...].
    template <typename... Ts, typename Fn>
    void eachAny(Fn&& fn) {
        forEachAny<Ts...>(std::function<void(Entity)>(std::forward<Fn>(fn)));
    }
    /// Counts matching entities without touching component payloads.
    template <typename... Ts>
    size_t count() const {
        std::vector<uint32_t> hits;
        intersectSets<Ts...>(hits);
        return hits.size();
    }

    /// Entity ids matching the given component names. Backs the editor's
    /// "find entities with X" and save-file filtering.
    std::vector<Entity> findWithComponent(const std::string& componentName) const;

    // --- diagnostics ---
    /// Bytes of component payload currently allocated (excludes capacity slack).
    size_t componentMemoryBytes() const;
    struct StorageStats {
        size_t entities = 0;
        size_t componentTypes = 0;
        size_t totalComponents = 0;
        size_t bytes = 0;
    };
    StorageStats stats() const;

    /// Per-system timing, filled by SystemScheduler.
    struct ProfileEntry {
        std::string name;
        double totalMs = 0.0;
        double lastMs = 0.0;
        uint64_t calls = 0;
    };
    std::vector<ProfileEntry> profiles;

private:
    friend class EntityView;

    // --- one column per component type ---
    struct Storage {
        ComponentId type = kInvalidComponent;
        std::vector<uint8_t> bytes;  ///< slot * size
        EntitySet present;
        size_t elementSize = 0;
    };

    Storage& storage(ComponentId type);
    const Storage* findStorage(ComponentId type) const;

    /// Collects the slots holding *all* of the given component types. Starts
    /// from the smallest candidate set, then filters by the rest, so the cost
    /// is proportional to the rarest component rather than to entity count.
    template <typename... Ts>
    void intersectSets(std::vector<uint32_t>& out) const {
        const ComponentId ids[] = {ComponentRegistry::typeId<Ts>()...};
        const Storage* smallest = nullptr;
        for (ComponentId id : ids) {
            const Storage* s = findStorage(id);
            if (!s) { out.clear(); return; }
            if (!smallest || s->present.size() < smallest->present.size()) smallest = s;
        }
        out = smallest->present.dense();
        for (ComponentId id : ids) {
            const Storage* s = findStorage(id);
            if (s == smallest) continue;
            uint32_t w = 0;
            for (uint32_t slot : out) {
                if (s->present.contains(slot)) out[w++] = slot;
            }
            out.resize(w);
        }
        std::sort(out.begin(), out.end());
    }

    /// Turns a homogeneous vector of component pointers back into a typed call.
    template <typename... Ts, size_t... I>
    static void callPacked(const std::function<void(Entity, Ts&...)>& fn, Entity e,
                           const std::vector<void*>& ptrs, std::index_sequence<I...>) {
        fn(e, *static_cast<Ts*>(ptrs[I])...);
    }
    template <typename... Ts, size_t... I>
    static void callPackedConst(const std::function<void(Entity, const Ts&...)>& fn, Entity e,
                                const std::vector<void*>& ptrs, std::index_sequence<I...>) {
        fn(e, *static_cast<const Ts*>(ptrs[I])...);
    }

    template <typename... Ts>
    void eachImpl(const std::function<void(Entity, Ts&...)>& fn) {
        std::vector<uint32_t> hits;
        intersectSets<Ts...>(hits);
        std::vector<void*> ptrs(sizeof...(Ts));
        for (uint32_t slot : hits) {
            const Entity e = entityAt(slot);
            if (!alive(e)) continue;  // destroyed earlier in this same loop
            size_t i = 0;
            ((ptrs[i++] = static_cast<void*>(getPtr(e, ComponentRegistry::typeId<Ts>()))), ...);
            callPacked<Ts...>(fn, e, ptrs, std::index_sequence_for<Ts...>{});
        }
    }

    template <typename... Ts>
    void eachConstImpl(const std::function<void(Entity, const Ts&...)>& fn) {
        std::vector<uint32_t> hits;
        intersectSets<Ts...>(hits);
        std::vector<void*> ptrs(sizeof...(Ts));
        for (uint32_t slot : hits) {
            const Entity e = entityAt(slot);
            if (!alive(e)) continue;
            size_t i = 0;
            ((ptrs[i++] = const_cast<void*>(static_cast<const void*>(
                 getPtr(e, ComponentRegistry::typeId<Ts>())))), ...);
            callPackedConst<Ts...>(fn, e, ptrs, std::index_sequence_for<Ts...>{});
        }
    }

    template <typename... Ts>
    void forEachAny(const std::function<void(Entity)>& fn) {
        std::vector<uint32_t> out;
        const ComponentId ids[] = {ComponentRegistry::typeId<Ts>()...};
        bool any = false;
        for (ComponentId id : ids) {
            const Storage* s = findStorage(id);
            if (!s) continue;
            any = true;
            s->present.collectInto(out);
        }
        if (!any) return;
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        for (uint32_t slot : out) {
            const Entity e = entityAt(slot);
            if (alive(e)) fn(e);
        }
    }

    /// Reconstructs the public handle for a live slot.
    Entity entityAt(uint32_t slot) const {
        return slot < slots_.size() ? Entity(slot, slots_[slot].generation) : Entity::invalid();
    }

    struct Slot {
        EntityGeneration generation = 1;
        bool alive = false;
    };
    std::vector<Slot> slots_;
    std::vector<uint32_t> freeList_;
    std::vector<uint32_t> liveList_;
    size_t liveCount_ = 0;

    std::vector<Storage> storages_;
    std::unordered_map<ComponentId, size_t> storageIndex_;
};

// --- EntityView implementations -------------------------------------------

template <typename T>
bool EntityView::has() const { return world_ && world_->template has<T>(entity_); }

template <typename T>
T* EntityView::get() const { return world_ ? world_->template get<T>(entity_) : nullptr; }

template <typename T>
T& EntityView::getRef() const {
    T* p = get<T>();
    if (!p) throw std::runtime_error("EntityView::getRef on missing component");
    return *p;
}

template <typename T>
T& EntityView::add() { return world_->template add<T>(entity_); }

template <typename T>
void EntityView::remove() const {
    if (world_) world_->template remove<T>(entity_);
}

template <typename T, typename... Args>
T& EntityView::emplace(Args&&... args) const {
    return world_->template emplace<T>(entity_, std::forward<Args>(args)...);
}

}  // namespace nova
