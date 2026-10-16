// Nova Engine — entities.
//
// Entities are 32-bit handles, not pointers. A handle packs an index and a
// generation counter so that a stale handle from a destroyed entity can never
// silently address a different entity that later reused the slot. This is the
// single most important safety property of the whole ECS.
#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace nova {

using EntityIndex = uint32_t;
using EntityGeneration = uint16_t;

constexpr EntityIndex kInvalidIndex = std::numeric_limits<EntityIndex>::max();
constexpr EntityGeneration kMaxGeneration = std::numeric_limits<EntityGeneration>::max();

/// Packed handle: [ generation:16 | index:32 ].
struct Entity {
    EntityIndex index = kInvalidIndex;
    EntityGeneration generation = 0;

    Entity() = default;
    Entity(EntityIndex i, EntityGeneration g) : index(i), generation(g) {}

    bool valid() const { return index != kInvalidIndex; }
    explicit operator bool() const { return valid(); }

    /// Monotonic id, handy for logging and stable sorting.
    uint64_t id() const {
        return (static_cast<uint64_t>(generation) << 32) | static_cast<uint64_t>(index);
    }

    bool operator==(const Entity& o) const {
        return index == o.index && generation == o.generation;
    }
    bool operator!=(const Entity& o) const { return !(*this == o); }
    /// Strict weak ordering by id.
    bool operator<(const Entity& o) const { return id() < o.id(); }

    static Entity invalid() { return Entity(kInvalidIndex, 0); }
};

/// Human-readable "12:3" (index:generation) for logs and the editor.
std::string toString(const Entity& e);

}  // namespace nova

namespace std {
template <>
struct hash<nova::Entity> {
    size_t operator()(const nova::Entity& e) const noexcept {
        return std::hash<uint64_t>{}(e.id());
    }
};
}  // namespace std
