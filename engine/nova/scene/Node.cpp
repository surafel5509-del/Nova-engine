#include "scene/Node.h"

#include <algorithm>
#include <functional>

namespace nova {
namespace {
const std::vector<NodeId> kEmpty;
const std::string kEmptyString;
}  // namespace

const std::vector<NodeId>& Scene::emptyChildren() { return kEmpty; }
const std::string& Scene::emptyString() const { return kEmptyString; }

// ---------------------------------------------------------------------------
// tree
// ---------------------------------------------------------------------------

NodeId Scene::createNode(const std::string& name, NodeId parent) {
    NodeId id;
    if (!freeList_.empty()) {
        id = freeList_.back();
        freeList_.pop_back();
        nodes_[id] = Node{};  // reset fully on recycle
    } else {
        id = static_cast<NodeId>(nodes_.size());
        if (id == kInvalidNode) return kInvalidNode;
        nodes_.emplace_back();
    }
    Node& n = nodes_[id];
    n.name = name.empty() ? "Node" : name;
    n.alive = true;
    n.worldDirty = true;
    ++liveCount_;

    if (parent != kInvalidNode && valid(parent)) {
        n.parent = parent;
        nodes_[parent].children.push_back(id);
        invalidateTransform(id);
    } else {
        n.parent = kInvalidNode;
        roots_.push_back(id);
    }
    return id;
}

void Scene::destroyNode(NodeId id) {
    if (!valid(id)) return;
    // Snapshot the subtree first: destroyNode mutates nodes_[].children.
    std::vector<NodeId> doomed;
    traverse(id, [&](NodeId n, int) { doomed.push_back(n); });
    for (NodeId n : doomed) {
        if (!valid(n)) continue;
        detachFromParent(n);
        nodes_[n].alive = false;
        nodes_[n].properties.clear();
        freeList_.push_back(n);
        --liveCount_;
    }
}

void Scene::detachFromParent(NodeId id) {
    const NodeId p = nodes_[id].parent;
    if (p == kInvalidNode) {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), id), roots_.end());
    } else {
        std::vector<NodeId>& siblings = nodes_[p].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
    }
    nodes_[id].parent = kInvalidNode;
}

std::vector<NodeId> Scene::allNodes() const {
    std::vector<NodeId> out;
    out.reserve(liveCount_);
    for (NodeId i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].alive) out.push_back(i);
    }
    return out;
}

const std::vector<NodeId>& Scene::children(NodeId id) const {
    return valid(id) ? nodes_[id].children : kEmpty;
}

NodeId Scene::parent(NodeId id) const { return valid(id) ? nodes_[id].parent : kInvalidNode; }

void Scene::traverse(NodeId id, const std::function<void(NodeId, int)>& fn) const {
    if (!valid(id)) return;
    // Explicit stack: deep hierarchies must not blow the call stack.
    struct Frame { NodeId id; int depth; };
    std::vector<Frame> stack{{id, 0}};
    while (!stack.empty()) {
        const Frame f = stack.back();
        stack.pop_back();
        fn(f.id, f.depth);
        const std::vector<NodeId>& kids = nodes_[f.id].children;
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
            stack.push_back({*it, f.depth + 1});
        }
    }
}

bool Scene::reparent(NodeId id, NodeId newParent, int index) {
    if (!valid(id)) return false;
    if (newParent != kInvalidNode && !valid(newParent)) return false;
    // Refuse to reparent a node under itself or one of its own descendants.
    if (newParent != kInvalidNode && isAncestorOf(id, newParent)) return false;
    // Already a child of that parent at that spot: nothing to do.
    if (nodes_[id].parent == newParent) return true;

    detachFromParent(id);
    if (newParent == kInvalidNode) {
        roots_.push_back(id);
    } else {
        nodes_[id].parent = newParent;
        std::vector<NodeId>& sibs = nodes_[newParent].children;
        if (index < 0 || index >= static_cast<int>(sibs.size())) sibs.push_back(id);
        else sibs.insert(sibs.begin() + index, id);
    }
    invalidateTransform(id);
    return true;
}

NodeId Scene::nextSibling(NodeId id) const {
    if (!valid(id)) return kInvalidNode;
    const NodeId p = nodes_[id].parent;
    const std::vector<NodeId>& sibs = p == kInvalidNode ? roots_ : nodes_[p].children;
    for (size_t i = 0; i + 1 < sibs.size(); ++i) {
        if (sibs[i] == id) return sibs[i + 1];
    }
    return kInvalidNode;
}

NodeId Scene::firstChild(NodeId id) const {
    if (!valid(id) || nodes_[id].children.empty()) return kInvalidNode;
    return nodes_[id].children.front();
}

int Scene::depth(NodeId id) const {
    int d = 0;
    while (valid(id)) {
        const NodeId p = nodes_[id].parent;
        if (p == kInvalidNode) break;
        id = p;
        ++d;
        if (d > nodes_.size()) break;  // corrupt-cycle guard
    }
    return d;
}

std::string Scene::path(NodeId id) const {
    if (!valid(id)) return "";
    std::vector<std::string> parts;
    int guard = 0;
    while (valid(id) && guard++ <= static_cast<int>(nodes_.size())) {
        parts.push_back(nodes_[id].name);
        const NodeId p = nodes_[id].parent;
        if (p == kInvalidNode) break;
        id = p;
    }
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!out.empty()) out += '/';
        out += *it;
    }
    return out;
}

NodeId Scene::rootOf(NodeId id) const {
    int guard = 0;
    while (valid(id) && nodes_[id].parent != kInvalidNode && guard++ <= static_cast<int>(nodes_.size())) {
        id = nodes_[id].parent;
    }
    return valid(id) ? id : kInvalidNode;
}

NodeId Scene::findAncestorNamed(NodeId id, const std::string& name) const {
    int guard = 0;
    while (valid(id) && guard++ <= static_cast<int>(nodes_.size())) {
        if (nodes_[id].name == name) return id;
        const NodeId p = nodes_[id].parent;
        if (p == kInvalidNode) break;
        id = p;
    }
    return kInvalidNode;
}

NodeId Scene::findByName(const std::string& name) const {
    for (NodeId i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].alive && nodes_[i].name == name) return i;
    }
    return kInvalidNode;
}

bool Scene::isAncestorOf(NodeId ancestor, NodeId candidate) const {
    if (!valid(ancestor) || !valid(candidate)) return false;
    int guard = 0;
    NodeId c = candidate;
    while (valid(c) && guard++ <= static_cast<int>(nodes_.size())) {
        if (c == ancestor) return true;
        const NodeId p = nodes_[c].parent;
        if (p == kInvalidNode) return false;
        c = p;
    }
    return false;
}

// ---------------------------------------------------------------------------
// data
// ---------------------------------------------------------------------------

const std::string& Scene::name(NodeId id) const {
    return valid(id) ? nodes_[id].name : kEmptyString;
}

void Scene::setName(NodeId id, const std::string& n) {
    if (valid(id)) nodes_[id].name = n;
}

const Transform& Scene::localTransform(NodeId id) const {
    static const Transform kIdentity;
    return valid(id) ? nodes_[id].local : kIdentity;
}

void Scene::setLocalTransform(NodeId id, const Transform& t) {
    if (!valid(id)) return;
    nodes_[id].local = t;
    invalidateTransform(id);
}

const Mat4& Scene::worldTransform(NodeId id) {
    static Mat4 kIdentity = Mat4::identity();
    if (!valid(id)) return kIdentity;
    if (nodes_[id].worldDirty) {
        // Resolve from the *highest* dirty ancestor. Starting the update at
        // [id] alone would compute children against a stale parent matrix.
        NodeId top = id;
        NodeId p = nodes_[top].parent;
        while (p != kInvalidNode && nodes_[p].worldDirty) {
            top = p;
            p = nodes_[p].parent;
        }
        updateWorldRecursive(top);
    }
    return nodes_[id].world;
}

void Scene::updateWorldRecursive(NodeId id) {
    // Pre-order, iteratively: a node's world matrix must be resolved before
    // any of its children are visited, and a deep hierarchy must not recurse.
    std::vector<NodeId> stack{id};
    while (!stack.empty()) {
        const NodeId cur = stack.back();
        stack.pop_back();
        if (!valid(cur)) continue;
        Node& n = nodes_[cur];
        if (n.worldDirty) {
            n.world = (n.parent == kInvalidNode)
                          ? n.local.matrix()
                          : nodes_[n.parent].world * n.local.matrix();
            n.worldDirty = false;
        }
        for (NodeId c : nodes_[cur].children) stack.push_back(c);
    }
}

Vec3 Scene::worldPosition(NodeId id) {
    const Mat4& m = worldTransform(id);
    return {m.m[12], m.m[13], m.m[14]};
}

void Scene::setWorldPosition(NodeId id, const Vec3& p) {
    if (!valid(id)) return;
    const NodeId par = nodes_[id].parent;
    if (par == kInvalidNode) {
        nodes_[id].local.position = p;
    } else {
        // worldTransform(par) resolves the parent chain if it is stale;
        // reading nodes_[par].world directly would use a stale matrix.
        const Mat4 inv = worldTransform(par).inverse();
        const Vec4 local = inv.project(p);
        nodes_[id].local.position = {local.x, local.y, local.z};
    }
    invalidateTransform(id);
}

float Scene::worldRotationZ(NodeId id) {
    if (!valid(id)) return 0.0f;
    // Accumulate Z rotation up the chain: the Z axis commutes with itself, so
    // a decomposed sum is exact and far cheaper than a matrix decomposition.
    float total = 0.0f;
    NodeId cur = id;
    int guard = 0;
    while (valid(cur) && guard++ <= static_cast<int>(nodes_.size())) {
        total += nodes_[cur].local.euler().z;
        const NodeId p = nodes_[cur].parent;
        if (p == kInvalidNode) break;
        cur = p;
    }
    return wrapAngle(total);
}

void Scene::setWorldRotationZ(NodeId id, float degrees) {
    if (!valid(id)) return;
    const NodeId par = nodes_[id].parent;
    const float parentZ = par == kInvalidNode ? 0.0f : worldRotationZ(par);
    nodes_[id].local.rotation = Quat::fromAngleAxis(degrees - parentZ, Vec3{0, 0, 1});
    invalidateTransform(id);
}

Vec3 Scene::worldScale(NodeId id) {
    if (!valid(id)) return {1, 1, 1};
    Vec3 s = {1, 1, 1};
    NodeId cur = id;
    int guard = 0;
    while (valid(cur) && guard++ <= static_cast<int>(nodes_.size())) {
        s.x *= nodes_[cur].local.scale.x;
        s.y *= nodes_[cur].local.scale.y;
        s.z *= nodes_[cur].local.scale.z;
        const NodeId p = nodes_[cur].parent;
        if (p == kInvalidNode) break;
        cur = p;
    }
    return s;
}

void Scene::setWorldScale(NodeId id, const Vec3& s) {
    if (!valid(id)) return;
    Vec3 parentScale{1, 1, 1};
    const NodeId par = nodes_[id].parent;
    if (par != kInvalidNode) parentScale = worldScale(par);
    // Guard against a zero parent scale turning this into infinity.
    auto safe = [](float p) { return std::fabs(p) < kEpsilon ? 1.0f : p; };
    nodes_[id].local.scale = {s.x / safe(parentScale.x), s.y / safe(parentScale.y),
                              s.z / safe(parentScale.z)};
    invalidateTransform(id);
}

bool Scene::flag(NodeId id, NodeFlags f) const {
    if (!valid(id)) return false;
    return (nodes_[id].flags & f) == f;
}

void Scene::setFlag(NodeId id, NodeFlags f, bool on) {
    if (!valid(id)) return;
    if (on) nodes_[id].flags = nodes_[id].flags | f;
    else nodes_[id].flags = static_cast<NodeFlags>(static_cast<uint32_t>(nodes_[id].flags) &
                                                  ~static_cast<uint32_t>(f));
}

bool Scene::visible(NodeId id) const { return flag(id, NodeFlags::Visible); }
bool Scene::active(NodeId id) const { return flag(id, NodeFlags::Active); }

bool Scene::effectivelyVisible(NodeId id) const {
    int guard = 0;
    while (valid(id) && guard++ <= static_cast<int>(nodes_.size())) {
        if (!flag(id, NodeFlags::Visible)) return false;
        const NodeId p = nodes_[id].parent;
        if (p == kInvalidNode) return true;
        id = p;
    }
    return false;
}

bool Scene::effectivelyActive(NodeId id) const {
    int guard = 0;
    while (valid(id) && guard++ <= static_cast<int>(nodes_.size())) {
        if (!flag(id, NodeFlags::Active)) return false;
        const NodeId p = nodes_[id].parent;
        if (p == kInvalidNode) return true;
        id = p;
    }
    return false;
}

void Scene::setProperty(NodeId id, const std::string& key, const std::string& value) {
    if (valid(id)) nodes_[id].properties[key] = value;
}

std::string Scene::property(NodeId id, const std::string& key, const std::string& def) const {
    if (!valid(id)) return def;
    const auto it = nodes_[id].properties.find(key);
    return it == nodes_[id].properties.end() ? def : it->second;
}

bool Scene::hasProperty(NodeId id, const std::string& key) const {
    return valid(id) && nodes_[id].properties.find(key) != nodes_[id].properties.end();
}

void Scene::removeProperty(NodeId id, const std::string& key) {
    if (valid(id)) nodes_[id].properties.erase(key);
}

const std::unordered_map<std::string, std::string>& Scene::properties(NodeId id) const {
    if (!valid(id)) {
        static const std::unordered_map<std::string, std::string> kNone;
        return kNone;
    }
    return nodes_[id].properties;
}

Entity Scene::entityOf(NodeId id) const { return valid(id) ? nodes_[id].entity : Entity::invalid(); }

void Scene::setEntity(NodeId id, Entity e) {
    if (valid(id)) nodes_[id].entity = e;
}

void Scene::clearEntityLinks() {
    for (Node& n : nodes_) n.entity = Entity::invalid();
}

void Scene::invalidateTransform(NodeId id) {
    if (!valid(id)) return;
    std::vector<NodeId> stack{id};
    while (!stack.empty()) {
        const NodeId cur = stack.back();
        stack.pop_back();
        if (!valid(cur)) continue;
        nodes_[cur].worldDirty = true;
        for (NodeId c : nodes_[cur].children) stack.push_back(c);
    }
}

void Scene::invalidateAllTransforms() {
    for (Node& n : nodes_) n.worldDirty = true;
}

size_t Scene::dirtyTransformCount() const {
    size_t n = 0;
    for (const Node& node : nodes_) {
        if (node.alive && node.worldDirty) ++n;
    }
    return n;
}

void Scene::clear() {
    nodes_.clear();
    roots_.clear();
    freeList_.clear();
    liveCount_ = 0;
}

}  // namespace nova
