#include "Backends/MeshData.hpp"

namespace BRITE {

MeshDataProblem CheckMeshData(const MeshData& mesh, std::size_t maxVertices) {
    const std::size_t vertexCount = mesh.Positions.size();

    if (vertexCount == 0)
        return MeshDataProblem::NoVertices;
    if (mesh.Indices.empty())
        return MeshDataProblem::NoTriangles;
    if (mesh.Normals.size() != vertexCount)
        return MeshDataProblem::NormalCountMismatch;
    if (!mesh.Colors.empty() && mesh.Colors.size() != vertexCount)
        return MeshDataProblem::ColorCountMismatch;
    if (mesh.Indices.size() % 3 != 0)
        return MeshDataProblem::PartialTriangle;
    if (vertexCount > maxVertices)
        return MeshDataProblem::TooManyVertices;
    for (std::uint32_t index : mesh.Indices) {
        if (index >= vertexCount)
            return MeshDataProblem::IndexOutOfRange;
    }
    return MeshDataProblem::None;
}

const char* Describe(MeshDataProblem problem) {
    switch (problem) {
    case MeshDataProblem::None:
        return "no problem";
    case MeshDataProblem::NoVertices:
        return "it has no vertices";
    case MeshDataProblem::NoTriangles:
        return "it has no triangles";
    case MeshDataProblem::NormalCountMismatch:
        return "its normals do not match its positions";
    case MeshDataProblem::ColorCountMismatch:
        return "its colours do not match its positions";
    case MeshDataProblem::PartialTriangle:
        return "its indices are not a whole number of triangles";
    case MeshDataProblem::TooManyVertices:
        return "it has more vertices than one mesh can index";
    case MeshDataProblem::IndexOutOfRange:
        return "an index names a vertex that does not exist";
    }
    return "an unknown problem";
}

} // namespace BRITE
