// Meshes: indexed triangle geometry with normals, plus the primitives a game
// actually needs.
//
// Vertices are stored interleaved so a backend can upload them as-is, and every
// primitive carries correct normals and UVs. That matters here: the renderer
// shades with vertex normals, so a flat-shaded cube would look like a blob if
// the normals were faked per face.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math/Math.h"

namespace nova {
namespace render {

/// Layout of one vertex, matching [MeshVertex::stride] so a GPU upload is a
/// pointer cast rather than a repack.
struct MeshVertex {
    Vec3 position{0, 0, 0};
    Vec3 normal{0, 1, 0};
    Vec2 uv{0, 0};
    Color color{1, 1, 1, 1};

    static constexpr size_t stride() { return sizeof(MeshVertex); }
};

/// Indexed geometry. A mesh is copyable and immutable once built; draw code
/// takes a const reference, so sharing a mesh between objects is free.
struct Mesh {
    std::vector<MeshVertex> vertices;
    /// Triangle list: three indices per triangle.
    std::vector<uint32_t> indices;
    std::string name;

    size_t triangleCount() const { return indices.size() / 3; }
    bool empty() const { return indices.empty() || vertices.empty(); }

    /// Local-space bounds. Recomputed on demand; meshes are small enough that
    /// caching this saves nothing but a bug.
    Aabb3 bounds() const;

    /// The twelve edges of [bounds] as line segments (24 vertices), for debug
    /// drawing without depending on the rasterizer's vertex type.
    std::vector<MeshVertex> boundsWireframe(Color c) const;

    void clear() {
        vertices.clear();
        indices.clear();
        boundsCacheValid_ = false;
    }
    void recomputeBounds() const { boundsCacheValid_ = false; }

    /// Recomputes every normal from face geometry and averages at shared
    /// vertices. Call after editing positions of a smooth mesh.
    void recomputeNormals();

    /// Flips triangle winding, and the sign of every normal with it, so the
    /// mesh faces the other way. A cube built inside-out is the usual cause.
    void flipWinding();

private:
    mutable Aabb3 bounds_{};
    mutable bool boundsCacheValid_ = false;
};

/// Accumulates vertices and indices, then hands back a Mesh.
class MeshBuilder {
public:
    /// Appends a triangle with an explicit normal per vertex.
    uint32_t addTriangle(const MeshVertex& a, const MeshVertex& b, const MeshVertex& c);
    /// Appends a quad as two triangles, wound a-b-c-d.
    void addQuad(const MeshVertex& a, const MeshVertex& b, const MeshVertex& c,
                 const MeshVertex& d);
    uint32_t addVertex(const MeshVertex& v);

    /// Transform applied to everything appended after this call.
    void setTransform(const Mat4& m);
    void pushTransform(const Mat4& m);
    void popTransform();
    void resetTransform();

    Mesh build(const std::string& name = "") const;

private:
    MeshVertex transform(const MeshVertex& v) const;

    std::vector<MeshVertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<Mat4> transforms_;
};

/// Built-in primitives. Every one is centred on the origin.
Mesh makePlane(float sizeX = 1.0f, float sizeZ = 1.0f, int segmentsX = 1, int segmentsZ = 1,
               bool doubleSided = false);
/// A box of the given full extents. Each face gets its own vertices so the
/// normals are flat, which is what a box should look like.
Mesh makeBox(Vec3 size = {1, 1, 1});
Mesh makeSphere(float radius = 0.5f, int segments = 16, int rings = 12);
/// Y-axis cone with its base at y = 0 and its tip at y = [height].
Mesh makeCone(float radius = 0.5f, float height = 1.0f, int segments = 16);
Mesh makeQuad(float size = 1.0f);
/// A flat grid on the XZ plane with UVs, the default surface for 2D-in-3D.
Mesh makeGrid(float size = 10.0f, int divisions = 10);

}  // namespace render
}  // namespace nova