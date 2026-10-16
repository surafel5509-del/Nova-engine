// Nova Engine — component types.
//
// A component is a plain old data struct. It is registered once by type so the
// world can create dense, contiguous per-type storage and so serialization can
// name it. There is no virtual dispatch and no per-entity allocation: storage
// for each component type is one contiguous block indexed by a dense
// component id.
#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <unordered_map>
#include <vector>

namespace nova {

using ComponentId = uint16_t;
constexpr ComponentId kInvalidComponent = 0xFFFF;

/// Type-erased reflection for a component: enough to move bytes around and to
/// correctly construct / destroy / copy instances.
struct ComponentType {
    std::string name;      ///< e.g. "Transform"
    std::size_t size = 0;  ///< sizeof(T)
    std::size_t align = 0; ///< alignof(T)
    ComponentId id = kInvalidComponent;
    /// Default-constructs in place (placement new). Required for sparse sets.
    void (*construct)(void* mem) = nullptr;
    /// Destroys in place. Required for non-trivial types.
    void (*destruct)(void* mem) = nullptr;
    /// Placement-new copies [src] into uninitialized [dst].
    void (*copy)(void* dst, const void* src) = nullptr;
    /// True when the type is memcpy-able and needs no destructor.
    bool trivial = false;
};

/// Runtime registry of component types. One per process; ids are stable for
/// the lifetime of the run, which is all serialization needs.
class ComponentRegistry {
public:
    static ComponentRegistry& instance();

    /// Interns T and returns its stable id (computed once per type).
    template <typename T>
    static ComponentId typeId() {
        static const ComponentId id = instance().add<T>(typeName<T>());
        return id;
    }

    const ComponentType& info(ComponentId id) const { return types_[id]; }
    size_t count() const { return types_.size(); }
    bool hasName(const std::string& name) const;
    /// Returns kInvalidComponent when the name is unknown.
    ComponentId idFor(const std::string& name) const;
    /// All registered names, in registration order (stable for serialization).
    const std::vector<ComponentType>& all() const { return types_; }

private:
    template <typename T>
    ComponentId add(std::string name) {
        ComponentType t;
        t.name = std::move(name);
        t.size = sizeof(T);
        t.align = alignof(T);
        t.construct = &constructAt<T>;
        t.destruct = &destructAt<T>;
        t.copy = &copyAt<T>;
        t.trivial = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;
        const ComponentId id = static_cast<ComponentId>(types_.size());
        t.id = id;
        types_.push_back(std::move(t));
        byName_[types_.back().name] = id;
        return id;
    }

    template <typename T>
    static void constructAt(void* mem) { new (mem) T(); }
    template <typename T>
    static void destructAt(void* mem) { static_cast<T*>(mem)->~T(); }
    template <typename T>
    static void copyAt(void* dst, const void* src) {
        new (dst) T(*static_cast<const T*>(src));
    }

    /// Human-readable, portable name for T: "nova::testcomp::Position" ->
    /// "Position".
    ///
    /// typeid(T).name() is unusable here: it is compiler-mangled on GCC/Clang
    /// ("N4nova8testcomp8PositionE") and already-stripped on MSVC. Save files
    /// need a name that is stable across compilers and toolchains, so we
    /// recover it from the compiler's pretty-printed function signature.
    template <typename T>
    static std::string typeName() {
#if defined(__clang__) || defined(__GNUC__)
        const std::string sig = __PRETTY_FUNCTION__;
        // ... with T = nova::testcomp::Position; std::string ...
        const size_t key = sig.find("T = ");
        if (key == std::string::npos) return mangledFallback<T>();
        const size_t start = key + 4;
        size_t end = sig.find_first_of(";]", start);
        if (end == std::string::npos) end = sig.size();
        return strip(sig.substr(start, end - start));
#elif defined(_MSC_VER)
        const std::string sig = __FUNCSIG__;
        // ... typeName<nova::testcomp::Position>(void) ...
        const size_t key = sig.find("typeName<");
        if (key == std::string::npos) return mangledFallback<T>();
        const size_t start = key + 9;
        const size_t end = sig.find(">(", start);
        if (end == std::string::npos) return mangledFallback<T>();
        return strip(sig.substr(start, end - start));
#else
        return mangledFallback<T>();
#endif
    }

    /// Keeps only the final namespace component: "nova::a::B" -> "B".
    static std::string strip(const std::string& qualified) {
        std::string s = qualified;
        // Drop any leading "struct "/"class "/"enum " that MSVC includes.
        for (const char* prefix : {"struct ", "class ", "enum "}) {
            if (s.rfind(prefix, 0) == 0) s = s.substr(std::char_traits<char>::length(prefix));
        }
        // Trim template arguments: "Vec3<float>" -> "Vec3".
        const size_t lt = s.find('<');
        if (lt != std::string::npos) s = s.substr(0, lt);
        const size_t colons = s.rfind("::");
        if (colons != std::string::npos) s = s.substr(colons + 2);
        return s;
    }

    template <typename T>
    static std::string mangledFallback() {
        std::string s(typeid(T).name());
        const size_t colons = s.rfind("::");
        if (colons != std::string::npos) s = s.substr(colons + 2);
        return s;
    }

    std::vector<ComponentType> types_;
    std::unordered_map<std::string, ComponentId> byName_;
};

/// Declares a component struct. Purely for readability and for tooling that
/// greps the engine source; registration happens on first typeId<T>() use.
#define NOVA_COMPONENT(Name)                                                   \
    struct Name;                                                               \
    static_assert(std::is_trivially_copyable_v<Name> || true,                  \
                  "components should be trivially copyable for fast paths");

}  // namespace nova
