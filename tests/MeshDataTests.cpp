// The refusals of IRenderBackend::LoadModelFromMesh, tested without a GPU.
//
// Each case below starts from one well-formed triangle and breaks it in exactly
// one way, so each has exactly one problem to find -- and asserts WHICH problem
// was found, not merely that the mesh was refused. A check that stops working
// usually does not let a bad mesh through; the next check down catches it for
// the wrong reason, and a test that only asked "refused?" stays green.
//
// Every expected value is worked by hand in the comment above it. Beside each
// case is the mutation that turns it red, and each was run once to prove it.

#include <Backends/MeshData.hpp>
#include <Backends/Raylib/RaylibRenderBackend.hpp>
#include <gtest/gtest.h>

#include <cstddef>

using BRITE::CheckMeshData;
using BRITE::MeshData;
using BRITE::MeshDataProblem;

namespace {

// One triangle, well formed in every respect.
MeshData Triangle() {
    MeshData mesh;
    mesh.Positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    mesh.Normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    mesh.Colors = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}};
    mesh.Indices = {0, 1, 2};
    return mesh;
}

// A vertex limit far above anything these small meshes use, for every case that
// is not about the limit.
constexpr std::size_t GENEROUS_LIMIT = 1000;

// `count` vertices, each with a normal, and one triangle over the first three.
MeshData ManyVertices(std::size_t count) {
    MeshData mesh;
    mesh.Positions.assign(count, {0.0f, 0.0f, 0.0f});
    mesh.Normals.assign(count, {0.0f, 0.0f, 1.0f});
    mesh.Indices = {0, 1, 2};
    return mesh;
}

} // namespace

TEST(MeshDataCheck, AWellFormedTriangleIsAccepted) {
    // Three positions, three normals, three colours, and indices 0, 1, 2 -- each
    // below 3 -- make one whole triangle: nothing to refuse. Without colours it
    // is still well formed, because an empty colour list means white.
    //
    // MUTATION: dropping the "Colors is not empty" guard from the colour check
    // refuses the colourless mesh as ColorCountMismatch (0 colours, 3 positions).
    EXPECT_EQ(CheckMeshData(Triangle(), GENEROUS_LIMIT), MeshDataProblem::None);

    MeshData colourless = Triangle();
    colourless.Colors.clear();
    EXPECT_EQ(CheckMeshData(colourless, GENEROUS_LIMIT), MeshDataProblem::None);
}

TEST(MeshDataCheck, AMeshWithNoPositionsHasNoVertices) {
    // Zero positions, zero normals, and the indices 0, 1, 2. Every later check
    // passes except the index check -- 0 is not below a vertex count of 0 -- so
    // this is refused as NoVertices only if that check comes first and works.
    //
    // MUTATION: removing the NoVertices check lets the mesh fall through to
    // IndexOutOfRange.
    MeshData mesh = Triangle();
    mesh.Positions.clear();
    mesh.Normals.clear();
    mesh.Colors.clear();
    EXPECT_EQ(CheckMeshData(mesh, GENEROUS_LIMIT), MeshDataProblem::NoVertices);
}

TEST(MeshDataCheck, AMeshWithNoIndicesHasNoTriangles) {
    // Three good vertices and no indices. Zero indices is zero triangles, and
    // 0 % 3 == 0 passes the whole-triangle check, so nothing after this one
    // would notice: it is the only thing standing between an empty mesh and the
    // GPU.
    //
    // MUTATION: removing the NoTriangles check accepts the mesh (None).
    MeshData mesh = Triangle();
    mesh.Indices.clear();
    EXPECT_EQ(CheckMeshData(mesh, GENEROUS_LIMIT), MeshDataProblem::NoTriangles);
}

TEST(MeshDataCheck, ANormalShortIsRefused) {
    // Three positions and two normals: 2 != 3.
    //
    // MUTATION: removing the normal-count check accepts the mesh (None).
    MeshData mesh = Triangle();
    mesh.Normals.pop_back();
    EXPECT_EQ(CheckMeshData(mesh, GENEROUS_LIMIT), MeshDataProblem::NormalCountMismatch);
}

TEST(MeshDataCheck, AColourShortIsRefused) {
    // Three positions and two colours: 2 is neither 0 nor 3.
    //
    // MUTATION: removing the colour-count check accepts the mesh (None).
    MeshData mesh = Triangle();
    mesh.Colors.pop_back();
    EXPECT_EQ(CheckMeshData(mesh, GENEROUS_LIMIT), MeshDataProblem::ColorCountMismatch);
}

TEST(MeshDataCheck, AnIncompleteTriangleIsRefused) {
    // Indices 0, 1, 2, 0: four of them, and 4 % 3 == 1, so the last triangle has
    // one corner. Every index is below 3, so nothing else is wrong.
    //
    // MUTATION: removing the whole-triangle check accepts the mesh (None).
    MeshData mesh = Triangle();
    mesh.Indices.push_back(0);
    EXPECT_EQ(CheckMeshData(mesh, GENEROUS_LIMIT), MeshDataProblem::PartialTriangle);
}

TEST(MeshDataCheck, MoreVerticesThanTheBackendCanIndexIsRefused) {
    // The raylib backend's limit, worked by hand: raylib draws a mesh with
    // 16-bit indices, which hold 0 to 65535. 65535 (0xFFFF) is the
    // primitive-restart index WebGL 2 always honours, so the highest usable index
    // is 65534, and indices 0 to 65534 address 65535 vertices.
    //     65535 vertices: accepted, the limit itself
    //     65536 vertices: refused, one over
    // The two counts are written out rather than computed from the constant, so
    // a wrong constant fails here instead of moving the test with it.
    //
    // MUTATIONS: the comparison as >= instead of > refuses 65535; a limit of
    // 65536 accepts 65536.
    constexpr std::size_t limit = BRITE::Backends::Raylib::RaylibRenderBackend::MaxVerticesPerMesh;
    EXPECT_EQ(CheckMeshData(ManyVertices(65535), limit), MeshDataProblem::None);
    EXPECT_EQ(CheckMeshData(ManyVertices(65536), limit), MeshDataProblem::TooManyVertices);
}

TEST(MeshDataCheck, AnIndexOnePastTheLastVertexIsRefused) {
    // Three vertices are numbered 0, 1 and 2; index 3 is one past the last. It is
    // the off-by-one a builder makes, so it is the value tested.
    //
    // MUTATIONS: comparing with > instead of >= accepts index 3; removing the
    // index loop accepts it too.
    MeshData mesh = Triangle();
    mesh.Indices = {0, 1, 3};
    EXPECT_EQ(CheckMeshData(mesh, GENEROUS_LIMIT), MeshDataProblem::IndexOutOfRange);
}
