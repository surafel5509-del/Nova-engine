#include "render/Mesh.h"

#include <algorithm>
#include <cmath>

namespace nova {
namespace render {

Aabb3 Mesh::bounds() const {
    if (boundsCacheValid_) return bounds_;
    bounds_ = Aabb3{};
    for (const MeshVertex& v : vertices) {
        if (bounds_.max.x < bounds_.min.x) {  // still the empty sentinel
            bounds_.min = v.position;
            bounds_.max = v.position;
        } else {
            bounds_.min = Vec3::min(bounds_.min, v.position);
            bounds_.max = Vec3::max(bounds_.max, v.position);
        }
    }
    boundsCacheValid_ = true;
    return bounds_;
}

std::vector<MeshVertex> Mesh::boundsWireframe(Color c) const {
    std::vector<MeshVertex> out;
    if (vertices.empty()) return out;
    const Aabb3 b = bounds();
    const Vec3 lo = b.min, hi = b.max;
    const Vec3 corners[8] = {
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
        {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
    };
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                              {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) {
        out.push_back(MeshVertex{corners[e[0]], Vec3{0, 1, 0}, Vec2{0, 0}, c});
        out.push_back(MeshVertex{corners[e[1]], Vec3{0, 1, 0}, Vec2{0, 0}, c});
    }
    return out;
}

void Mesh::recomputeNormals() {
    for (MeshVertex& v : vertices) v.normal = Vec3{0, 0, 0};
    const size_t tris = indices.size() / 3;
    for (size_t t = 0; t < tris; ++t) {
        const uint32_t i0 = indices[t * 3], i1 = indices[t * 3 + 1], i2 = indices[t * 3 + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;
        const Vec3& p0 = vertices[i0].position;
        const Vec3& p1 = vertices[i1].position;
        const Vec3& p2 = vertices[i2].position;
        const Vec3 face = (p1 - p0).cross(p2 - p0);
        vertices[i0].normal += face;
        vertices[i1].normal += face;
        vertices[i2].normal += face;
    }
    for (MeshVertex& v : vertices) {
        const float len = v.normal.length();
        v.normal = len > 1e-8f ? v.normal / len : Vec3{0, 1, 0};
    }
}

void Mesh::flipWinding() {
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        std::swap(indices[t + 1], indices[t + 2]);
    }
    for (MeshVertex& v : vertices) v.normal = -v.normal;
}

// --- MeshBuilder ------------------------------------------------------------

uint32_t MeshBuilder::addVertex(const MeshVertex& v) {
    vertices_.push_back(transform(v));
    return static_cast<uint32_t>(vertices_.size() - 1);
}

uint32_t MeshBuilder::addTriangle(const MeshVertex& a, const MeshVertex& b, const MeshVertex& c) {
    const uint32_t i0 = addVertex(a);
    const uint32_t i1 = addVertex(b);
    const uint32_t i2 = addVertex(c);
    indices_.push_back(i0);
    indices_.push_back(i1);
    indices_.push_back(i2);
    return i0;
}

void MeshBuilder::addQuad(const MeshVertex& a, const MeshVertex& b, const MeshVertex& c,
                          const MeshVertex& d) {
    addTriangle(a, b, c);
    addTriangle(a, c, d);
}

void MeshBuilder::pushTransform(const Mat4& m) {
    transforms_.push_back(transforms_.empty() ? m : transforms_.back() * m);
}

void MeshBuilder::popTransform() {
    if (!transforms_.empty()) transforms_.pop_back();
}

void MeshBuilder::resetTransform() { transforms_.clear(); }

MeshVertex MeshBuilder::transform(const MeshVertex& v) const {
    MeshVertex out = v;
    for (const Mat4& m : transforms_) {
        out.position = m.transformPoint(out.position);
        // Normals need the inverse transpose, but for the rigid transforms and
        // uniform scales a builder applies, transformDirection is correct and
        // far cheaper. Re-normalising keeps them unit length.
        const Vec3 n = m.transformDirection(out.normal);
        const float len = n.length();
        out.normal = len > 1e-8f ? n / len : Vec3{0, 1, 0};
    }
    return out;
}

Mesh MeshBuilder::build(const std::string& name) const {
    Mesh m;
    m.vertices = vertices_;
    m.indices = indices_;
    m.name = name;
    return m;
}

// --- primitives -------------------------------------------------------------

Mesh makePlane(float sizeX, float sizeZ, int segmentsX, int segmentsZ, bool doubleSided) {
    MeshBuilder b;
    const int nx = std::max(1, segmentsX);
    const int nz = std::max(1, segmentsZ);
    for (int z = 0; z < nz; ++z) {
        for (int x = 0; x < nx; ++x) {
            const float u0 = static_cast<float>(x) / nx;
            const float u1 = static_cast<float>(x + 1) / nx;
            const float v0 = static_cast<float>(z) / nz;
            const float v1 = static_cast<float>(z + 1) / nz;
            auto p = [&](float u, float v) {
                return Vec3{(u - 0.5f) * sizeX, 0.0f, (v - 0.5f) * sizeZ};
            };
            MeshVertex v00;
            v00.position = p(u0, v0);
            v00.normal = Vec3{0, 1, 0};
            v00.uv = {u0, v0};
            MeshVertex v10;
            v10.position = p(u1, v0);
            v10.normal = Vec3{0, 1, 0};
            v10.uv = {u1, v0};
            MeshVertex v11;
            v11.position = p(u1, v1);
            v11.normal = Vec3{0, 1, 0};
            v11.uv = {u1, v1};
            MeshVertex v01;
            v01.position = p(u0, v1);
            v01.normal = Vec3{0, 1, 0};
            v01.uv = {u0, v1};
            b.addQuad(v00, v01, v11, v10);
            if (doubleSided) {
                MeshVertex f00 = v00, f01 = v01, f11 = v11, f10 = v10;
                f00.normal = f01.normal = f11.normal = f10.normal = Vec3{0, -1, 0};
                b.addQuad(f00, f10, f11, f01);
            }
        }
    }
    return b.build("plane");
}

Mesh makeBox(Vec3 size) {
    MeshBuilder b;
    const Vec3 h = size * 0.5f;
    struct Face {
        Vec3 normal;
        Vec3 origin, u, v;  // centre, and the two in-plane half-extent vectors
    };
    const Face faces[6] = {
        {{0, 0, 1}, {0, 0, h.z}, {h.x, 0, 0}, {0, h.y, 0}},
        {{0, 0, -1}, {0, 0, -h.z}, {-h.x, 0, 0}, {0, h.y, 0}},
        {{1, 0, 0}, {h.x, 0, 0}, {0, 0, -h.z}, {0, h.y, 0}},
        {{-1, 0, 0}, {-h.x, 0, 0}, {0, 0, h.z}, {0, h.y, 0}},
        // Top and bottom need u x v == the face normal, which for X means the
        // in-plane vectors have to be Z and -Z, not X and Z.
        {{0, 1, 0}, {0, h.y, 0}, {h.x, 0, 0}, {0, 0, -h.z}},
        {{0, -1, 0}, {0, -h.y, 0}, {h.x, 0, 0}, {0, 0, h.z}},
    };
    for (const Face& f : faces) {
        MeshVertex corners[4];
        for (int i = 0; i < 4; ++i) {
            const float su = (i == 0 || i == 3) ? -1.0f : 1.0f;
            const float sv = (i < 2) ? -1.0f : 1.0f;
            corners[i].position = f.origin + f.u * su + f.v * sv;
            corners[i].normal = f.normal;
            corners[i].uv = {(su * 0.5f) + 0.5f, (sv * 0.5f) + 0.5f};
        }
        b.addQuad(corners[0], corners[1], corners[2], corners[3]);
    }
    return b.build("box");
}

Mesh makeSphere(float radius, int segments, int rings) {
    MeshBuilder b;
    const int seg = std::max(3, segments);
    const int rgs = std::max(2, rings);
    for (int r = 0; r < rgs; ++r) {
        const float v0 = static_cast<float>(r) / rgs;
        const float v1 = static_cast<float>(r + 1) / rgs;
        const float phi0 = v0 * kPi;
        const float phi1 = v1 * kPi;
        for (int s = 0; s < seg; ++s) {
            const float u0 = static_cast<float>(s) / seg;
            const float u1 = static_cast<float>(s + 1) / seg;
            const float theta0 = u0 * 2.0f * kPi;
            const float theta1 = u1 * 2.0f * kPi;
            auto at = [&](float theta, float phi) {
                MeshVertex v;
                v.position = Vec3{radius * std::sin(phi) * std::cos(theta),
                                  radius * std::cos(phi), radius * std::sin(phi) * std::sin(theta)};
                // The normal of a sphere is its position, normalised.
                v.normal = v.position / radius;
                v.uv = {theta / (2.0f * kPi), 1.0f - phi / kPi};
                return v;
            };
            const MeshVertex a = at(theta0, phi0);
            const MeshVertex c = at(theta1, phi0);
            const MeshVertex d = at(theta1, phi1);
            const MeshVertex e = at(theta0, phi1);
            b.addTriangle(a, c, d);
            // Poles collapse into degenerate triangles; skipping them keeps the
            // count honest and avoids NaN normals at the top and bottom.
            if (r > 0) b.addTriangle(a, d, e);
        }
    }
    return b.build("sphere");
}

Mesh makeCone(float radius, float height, int segments) {
    MeshBuilder b;
    const int seg = std::max(3, segments);
    for (int s = 0; s < seg; ++s) {
        const float u0 = static_cast<float>(s) / seg;
        const float u1 = static_cast<float>(s + 1) / seg;
        const float t0 = u0 * 2.0f * kPi;
        const float t1 = u1 * 2.0f * kPi;

        MeshVertex tip;
        tip.position = Vec3{0, height, 0};
        tip.normal = Vec3{0, 1, 0};
        tip.uv = {(u0 + u1) * 0.5f, 1.0f};

        auto side = [&](float t, float u) {
            MeshVertex v;
            v.position = Vec3{radius * std::cos(t), 0, radius * std::sin(t)};
            // The slant normal tilts out by the cone's half angle.
            const float slope = radius / std::max(1e-4f, height);
            v.normal = Vec3{std::cos(t), slope, std::sin(t)}.normalized();
            v.uv = {u, 0.0f};
            return v;
        };
        const MeshVertex l = side(t0, u0);
        const MeshVertex r = side(t1, u1);
        b.addTriangle(tip, l, r);

        // The cap, facing down.
        MeshVertex centre;
        centre.position = Vec3{0, 0, 0};
        centre.normal = Vec3{0, -1, 0};
        centre.uv = {0.5f, 0.5f};
        MeshVertex l2 = l, r2 = r;
        l2.normal = r2.normal = Vec3{0, -1, 0};
        b.addTriangle(centre, r2, l2);
    }
    return b.build("cone");
}

Mesh makeQuad(float size) {
    MeshBuilder b;
    const float h = size * 0.5f;
    MeshVertex v[4];
    const Vec3 positions[4] = {{-h, -h, 0}, {h, -h, 0}, {h, h, 0}, {-h, h, 0}};
    for (int i = 0; i < 4; ++i) {
        v[i].position = positions[i];
        v[i].normal = Vec3{0, 0, 1};
        v[i].uv = {static_cast<float>(i == 0 || i == 3 ? 0 : 1), static_cast<float>(i < 2 ? 0 : 1)};
    }
    b.addQuad(v[0], v[1], v[2], v[3]);
    return b.build("quad");
}

Mesh makeGrid(float size, int divisions) {
    const int d = std::max(1, divisions);
    MeshBuilder b;
    for (int z = 0; z < d; ++z) {
        for (int x = 0; x < d; ++x) {
            const float x0 = static_cast<float>(x) / d - 0.5f;
            const float x1 = static_cast<float>(x + 1) / d - 0.5f;
            const float z0 = static_cast<float>(z) / d - 0.5f;
            const float z1 = static_cast<float>(z + 1) / d - 0.5f;
            const float s = size;
            MeshVertex v00, v10, v11, v01;
            v00.position = {x0 * s, 0, z0 * s};
            v10.position = {x1 * s, 0, z0 * s};
            v11.position = {x1 * s, 0, z1 * s};
            v01.position = {x0 * s, 0, z1 * s};
            v00.normal = v10.normal = v11.normal = v01.normal = Vec3{0, 1, 0};
            v00.uv = {static_cast<float>(x) / d, static_cast<float>(z) / d};
            v10.uv = {static_cast<float>(x + 1) / d, static_cast<float>(z) / d};
            v11.uv = {static_cast<float>(x + 1) / d, static_cast<float>(z + 1) / d};
            v01.uv = {static_cast<float>(x) / d, static_cast<float>(z + 1) / d};
            b.addQuad(v00, v01, v11, v10);
        }
    }
    return b.build("grid");
}

}  // namespace render
}  // namespace nova