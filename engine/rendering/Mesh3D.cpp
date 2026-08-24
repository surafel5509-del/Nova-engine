#include "rendering/Mesh3D.h"

#include <cmath>

namespace nova {

namespace {
void addQuad(std::vector<MeshVertex3D>& out,
             float ax, float ay, float az,
             float bx, float by, float bz,
             float cx, float cy, float cz,
             float dx, float dy, float dz,
             float nx, float ny, float nz) {
    const MeshVertex3D a{ax, ay, az, nx, ny, nz};
    const MeshVertex3D b{bx, by, bz, nx, ny, nz};
    const MeshVertex3D c{cx, cy, cz, nx, ny, nz};
    const MeshVertex3D d{dx, dy, dz, nx, ny, nz};
    out.push_back(a); out.push_back(b); out.push_back(c);
    out.push_back(a); out.push_back(c); out.push_back(d);
}
} // namespace

std::vector<MeshVertex3D> MeshBuilder::cube() {
    std::vector<MeshVertex3D> v;
    v.reserve(36);
    const float h = 1.0f;
    // +Y / -Y
    addQuad(v, -h, h, -h, h, h, -h, h, h, h, -h, h, h, 0, 1, 0);
    addQuad(v, -h, -h, -h, -h, -h, h, h, -h, h, h, -h, -h, 0, -1, 0);
    // +Z / -Z
    addQuad(v, -h, -h, h, -h, h, h, h, h, h, h, -h, h, 0, 0, 1);
    addQuad(v, -h, -h, -h, h, -h, -h, h, h, -h, -h, h, -h, 0, 0, -1);
    // +X / -X
    addQuad(v, h, -h, -h, h, -h, h, h, h, h, h, h, -h, 1, 0, 0);
    addQuad(v, -h, -h, -h, -h, h, -h, -h, h, h, -h, -h, h, -1, 0, 0);
    return v;
}

std::vector<MeshVertex3D> MeshBuilder::cylinder(int segments) {
    std::vector<MeshVertex3D> v;
    if (segments < 3) segments = 3;
    const float h = 1.0f;
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * 3.14159265f * i / segments;
        const float a1 = 2.0f * 3.14159265f * (i + 1) / segments;
        const float x0 = std::cos(a0), z0 = std::sin(a0);
        const float x1 = std::cos(a1), z1 = std::sin(a1);
        // Side quad (two triangles).
        v.push_back({x0, -h, z0, x0, 0, z0});
        v.push_back({x1, -h, z1, x1, 0, z1});
        v.push_back({x1, h, z1, x1, 0, z1});
        v.push_back({x0, -h, z0, x0, 0, z0});
        v.push_back({x1, h, z1, x1, 0, z1});
        v.push_back({x0, h, z0, x0, 0, z0});
        // Top cap.
        v.push_back({0, h, 0, 0, 1, 0});
        v.push_back({x0, h, z0, 0, 1, 0});
        v.push_back({x1, h, z1, 0, 1, 0});
        // Bottom cap.
        v.push_back({0, -h, 0, 0, -1, 0});
        v.push_back({x1, -h, z1, 0, -1, 0});
        v.push_back({x0, -h, z0, 0, -1, 0});
    }
    return v;
}

std::vector<MeshVertex3D> MeshBuilder::plane(float size) {
    std::vector<MeshVertex3D> v;
    v.reserve(6);
    const float h = size / 2.0f;
    addQuad(v, -h, 0, -h, h, 0, -h, h, 0, h, -h, 0, h, 0, 1, 0);
    return v;
}

std::vector<MeshVertex3D> MeshBuilder::forShape(const std::string& shape) {
    if (shape == "cylinder") return cylinder();
    if (shape == "ground" || shape == "plane") return plane();
    return cube();
}

} // namespace nova
