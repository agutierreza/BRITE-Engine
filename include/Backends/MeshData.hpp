#pragma once

#include "Math/BriteMath.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace BRITE {

// Geometry generated in code, ready for IRenderBackend::LoadModelFromMesh.
//
// One normal per vertex, so flat shading is done by giving each face its own
// vertices -- the builder decides how the surface is shaded, the backend only
// uploads what it is given. Indices are triangles, three per face, wound
// counter-clockwise seen from outside so backface culling keeps the outside.
// Colours are optional; an empty vector means white, which lets the draw's
// AlbedoTint colour the whole model.
struct MeshData {
    std::vector<Vector3> Positions;
    std::vector<Vector3> Normals;       // same length as Positions
    std::vector<Color> Colors;          // same length as Positions, or empty
    std::vector<std::uint32_t> Indices; // three per triangle, each < Positions.size()
};

// What is wrong with a MeshData, if anything. One value per refusal, so a test
// can tell WHICH check caught a mesh: a mesh refused for the wrong reason is a
// check that has quietly stopped doing its job.
enum class MeshDataProblem {
    None,
    NoVertices,          // Positions is empty
    NoTriangles,         // Indices is empty
    NormalCountMismatch, // Normals is not the length of Positions
    ColorCountMismatch,  // Colors is neither empty nor the length of Positions
    PartialTriangle,     // Indices is not a whole number of triangles
    TooManyVertices,     // more vertices than the backend can index in one mesh
    IndexOutOfRange,     // an index names a vertex that does not exist
};

// Check a mesh against the rules above and against a backend's limit on
// vertices per mesh. Pure: no GPU, no logging, so it can be tested anywhere.
// Returns the FIRST problem found, in the order the enum lists them.
MeshDataProblem CheckMeshData(const MeshData& mesh, std::size_t maxVertices);

// A short phrase for a log line.
const char* Describe(MeshDataProblem problem);

} // namespace BRITE
