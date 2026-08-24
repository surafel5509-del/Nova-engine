#pragma once

#include <string>
#include <vector>

namespace nova {

/** One 3D vertex: position + normal. */
struct MeshVertex3D {
    float x, y, z;
    float nx, ny, nz;
};

/**
 * Primitive mesh generation for the 3D engine. CPU-side, GL-free and
 * host-testable: returns interleaved position+normal triangles.
 */
class MeshBuilder {
public:
    /** Unit cube centered at origin (2 units per side). 36 vertices. */
    static std::vector<MeshVertex3D> cube();

    /** Cylinder along the Y axis (radius 1, height 2, centered). */
    static std::vector<MeshVertex3D> cylinder(int segments = 24);

    /** Flat ground plane in XZ (size x size, normal +Y). 6 vertices. */
    static std::vector<MeshVertex3D> plane(float size = 20.0f);

    /** Builds the mesh for a named shape; falls back to cube. */
    static std::vector<MeshVertex3D> forShape(const std::string& shape);
};

} // namespace nova
