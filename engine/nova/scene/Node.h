// Nova Engine — scene graph.
//
// A Node is one object in a scene: a transform plus a bag of components plus
// children. Nodes are stored in a flat vector with integer ids rather than as
// heap objects, so child lookups, iteration and serialization are all cache
// friendly, and reordering the hierarchy never moves memory around.
//
// Local transform * world transform caching means a deep hierarchy only
// recomputes the subtrees that actually changed.
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ecs/Entity.h"
#include "math/Math.h"

namespace nova {

using NodeId = uint32_t;
constexpr NodeId kInvalidNode = 0xFFFFFFFFu;

/// Local TRS with a dirty flag for parent-change propagation.
struct Transform {
    Vec3 position{0, 0, 0};
    Quat rotation = Quat::identity();
    Vec3 scale{1, 1, 1};

    static Transform fromPosition(const Vec3& p) {
        Transform t;
        t.position = p;
        return t;
    }
    /// 2D convenience: z rotation around the up axis.
    static Transform from2D(const Vec2& p, float rotationDeg, const Vec2& scale) {
        Transform t;
        t.position = {p.x, p.y, 0.0f};
        t.rotation = Quat::fromAngleAxis(rotationDeg, Vec3{0, 0, 1});
        t.scale = {scale.x, scale.y, 1.0f};
        return t;
    }

    Mat4 matrix() const { return Mat4::trs(position, rotation, scale); }

    /// Transforms a point from this transform's local space into the parent's.
    Vec3 transformPoint(const Vec3& p) const { return matrix().transformPoint(p); }
    /// Transforms a direction (ignores scale), e.g. for velocities.
    Vec3 transformDirection(const Vec3& d) const {
        return rotation.rotate(d);
    }
    /// Inverse-transforms a world-space point into local space.
    Vec3 inverseTransformPoint(const Vec3& p) const {
        return matrix().inverse().transformPoint(p);
    }
    Vec3 euler() const {
        Vec3 e;
        rotation.toEuler(e.x, e.y, e.z);
        return e;
    }
};

enum class NodeFlags : uint32_t {
    None = 0,
    Visible = 1u << 0,
    Active = 1u << 1,
    /// Excluded from raycasts and physics queries.
    Ghost = 1u << 2,
    /// Persisted to disk when the scene is saved.
    Persistent = 1u << 3,
};

inline NodeFlags operator|(NodeFlags a, NodeFlags b) {
    return static_cast<NodeFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline NodeFlags operator&(NodeFlags a, NodeFlags b) {
    return static_cast<NodeFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

class Scene {
public:
    Scene() = default;

    // --- tree ---
    NodeId createNode(const std::string& name = "Node", NodeId parent = kInvalidNode);
    /// Removes the node and, recursively, its whole subtree.
    void destroyNode(NodeId id);
    bool valid(NodeId id) const { return id < nodes_.size() && nodes_[id].alive; }
    size_t nodeCount() const { return liveCount_; }
    /// Live node ids in creation order.
    std::vector<NodeId> allNodes() const;
    /// Direct children of [id] in order.
    const std::vector<NodeId>& children(NodeId id) const;
    NodeId parent(NodeId id) const;
    /// Root nodes (those with no parent).
    const std::vector<NodeId>& roots() const { return roots_; }
    /// Depth-first walk over [id] and its descendants (pre-order).
    void traverse(NodeId id, const std::function<void(NodeId, int depth)>& fn) const;
    /// Moves [id] under [newParent], rejecting cycles. Returns false on failure.
    bool reparent(NodeId id, NodeId newParent, int index = -1);
    /// Next sibling, previous sibling, first child.
    NodeId nextSibling(NodeId id) const;
    NodeId firstChild(NodeId id) const;
    /// Depth from the root (roots are 0).
    int depth(NodeId id) const;
    /// Full path from the root, e.g. "Level/Enemies/Slime".
    std::string path(NodeId id) const;
    /// Root-most ancestor (self if it is a root).
    NodeId rootOf(NodeId id) const;
    /// Nearest ancestor (including self) whose name equals [name].
    NodeId findAncestorNamed(NodeId id, const std::string& name) const;
    /// Depth-first search for the first node named [name].
    NodeId findByName(const std::string& name) const;
    /// True when [candidate] is [ancestor] or lives under it.
    bool isAncestorOf(NodeId ancestor, NodeId candidate) const;

    // --- data ---
    const std::string& name(NodeId id) const;
    void setName(NodeId id, const std::string& name);
    const Transform& localTransform(NodeId id) const;
    void setLocalTransform(NodeId id, const Transform& t);
    /// Composed world transform (parent * local), cached until invalidated.
    const Mat4& worldTransform(NodeId id);
    Vec3 worldPosition(NodeId id);
    /// Sets position in world space, converting into the parent's local space.
    void setWorldPosition(NodeId id, const Vec3& p);
    float worldRotationZ(NodeId id);
    void setWorldRotationZ(NodeId id, float degrees);
    /// Uniform-or-axis scale in world space.
    Vec3 worldScale(NodeId id);
    void setWorldScale(NodeId id, const Vec3& s);

    bool flag(NodeId id, NodeFlags f) const;
    void setFlag(NodeId id, NodeFlags f, bool on = true);
    bool visible(NodeId id) const;
    /// Visible only if this node and every ancestor are visible.
    bool effectivelyVisible(NodeId id) const;
    bool active(NodeId id) const;
    /// Active only if this node and every ancestor are active.
    bool effectivelyActive(NodeId id) const;

    /// Free-form per-node string properties (tags, script vars, metadata).
    /// Kept generic so new editor features need no C++ change.
    void setProperty(NodeId id, const std::string& key, const std::string& value);
    std::string property(NodeId id, const std::string& key,
                         const std::string& def = "") const;
    bool hasProperty(NodeId id, const std::string& key) const;
    void removeProperty(NodeId id, const std::string& key);
    const std::unordered_map<std::string, std::string>& properties(NodeId id) const;

    /// Links a node to an ECS entity holding its gameplay components.
    Entity entityOf(NodeId id) const;
    void setEntity(NodeId id, Entity e);
    /// Clears every node -> entity link. Used when tearing a world down.
    void clearEntityLinks();

    /// Invalidates cached world transforms at [id] and below.
    void invalidateTransform(NodeId id);
    void invalidateAllTransforms();
    /// How many nodes have a dirty cached world matrix (diagnostics).
    size_t dirtyTransformCount() const;

    void clear();

private:
    struct Node {
        std::string name = "Node";
        NodeId parent = kInvalidNode;
        std::vector<NodeId> children;
        Transform local;
        Mat4 world = Mat4::identity();
        bool worldDirty = true;
        bool alive = false;
        NodeFlags flags = NodeFlags::Visible | NodeFlags::Active | NodeFlags::Persistent;
        std::unordered_map<std::string, std::string> properties;
        Entity entity;
    };

    static const std::vector<NodeId>& emptyChildren();
    const std::string& emptyString() const;
    void detachFromParent(NodeId id);
    void updateWorldRecursive(NodeId id);

    std::vector<Node> nodes_;
    std::vector<NodeId> roots_;
    std::vector<NodeId> freeList_;
    size_t liveCount_ = 0;
};

}  // namespace nova
