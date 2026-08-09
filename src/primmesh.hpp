#pragma once

#include <vector>

// Shared, GL-agnostic unit-primitive tessellation. The single host-side source
// for the box/sphere/cylinder/cone/plane geometry: the editor viewport shades
// these into its draw meshes, and the decal projector (decalproj) uses them as
// receiver geometry so a projected decal conforms to exactly the surface the
// viewport draws. The generated PS2 runtime keeps its own copies (addBox/... in
// templates.cpp) - those are device code; keep the formulas in sync, as the
// project has always required (see project.hpp primTriangleCount notes).
//
// Every generator returns a flat triangle list, 8 floats per vertex:
//   pos(3) + normal(3) + uv(2)
// Unit primitives fit a 1x1x1 cube centered at the origin (matching the
// SceneObject transform: scale -> rotation -> translation).
namespace primmesh {

// detail meaning is type-dependent (see project.hpp clampPrimDetail): Box =
// subdivisions per edge; Sphere/Cylinder/Cone = radial segments.
std::vector<float> unitBox(int detail);
std::vector<float> unitSphere(int detail);
// rings = SceneObject::primRings: also subdivide the side along the axis
// (primCylinderStacks). false reproduces the classic one-quad-tall side
// vertex for vertex.
std::vector<float> unitCylinder(int detail, bool rings);
std::vector<float> unitCone(int detail);
std::vector<float> unitPlane();

}  // namespace primmesh
