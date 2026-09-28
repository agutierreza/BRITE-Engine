// ReadGltfMaterials: what a glTF file says of its materials beyond the base
// colour, read from the file's bytes with no GPU. Each case feeds it a file
// written out in full above the assertion, so every expected value is the one
// the text on the page says -- or glTF's own default where the text is silent.
//
// Beside each case is the mutation that turns it red, and each was run once.

#include <Backends/GltfMaterials.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using BRITE::GltfAlphaMode;
using BRITE::GltfMaterialInfo;
using BRITE::ReadGltfMaterials;

namespace {

std::vector<unsigned char> Bytes(const std::string& text) {
    return std::vector<unsigned char>(text.begin(), text.end());
}

void AppendU32(std::vector<unsigned char>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<unsigned char>(value >> (8 * i)));
}

// A .glb around `json`: the 12-byte header -- "glTF", version 2, total length --
// then one JSON chunk, its length a multiple of 4 padded with spaces as glTF
// requires, then an empty BIN chunk, which a reader of materials must skip.
std::vector<unsigned char> Glb(std::string json) {
    while (json.size() % 4)
        json += ' ';
    std::vector<unsigned char> out;
    AppendU32(out, 0x46546C67); // "glTF"
    AppendU32(out, 2);
    AppendU32(out, 0); // total length, filled in below
    AppendU32(out, static_cast<std::uint32_t>(json.size()));
    AppendU32(out, 0x4E4F534A); // "JSON"
    out.insert(out.end(), json.begin(), json.end());
    AppendU32(out, 0);
    AppendU32(out, 0x004E4942); // "BIN\0"
    const auto total = static_cast<std::uint32_t>(out.size());
    for (int i = 0; i < 4; ++i)
        out[8 + static_cast<std::size_t>(i)] = static_cast<unsigned char>(total >> (8 * i));
    return out;
}

// Three materials: the first says nothing but a colour, so it is glTF's
// defaults (OPAQUE, cutoff 0.5, one-sided); the second is a double-sided MASK
// cut at 0.3; the third is BLEND.
const std::string THREE_MATERIALS = R"({
  "asset": {"version": "2.0"},
  "materials": [
    {"pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]}},
    {"alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true},
    {"alphaMode": "BLEND"}
  ]
})";

void ExpectThree(const std::vector<GltfMaterialInfo>& materials) {
    ASSERT_EQ(materials.size(), 3u);
    EXPECT_EQ(materials[0].AlphaMode, GltfAlphaMode::Opaque);
    EXPECT_FLOAT_EQ(materials[0].AlphaCutoff, 0.5f) << "glTF's default cutoff";
    EXPECT_FALSE(materials[0].DoubleSided);
    EXPECT_EQ(materials[1].AlphaMode, GltfAlphaMode::Mask);
    EXPECT_FLOAT_EQ(materials[1].AlphaCutoff, 0.3f);
    EXPECT_TRUE(materials[1].DoubleSided);
    EXPECT_EQ(materials[2].AlphaMode, GltfAlphaMode::Blend);
}

} // namespace

// The three materials above, from a .gltf's text.
//
// Mutations: "MASK" not recognised -> material 1 reads Opaque; the default
// cutoff 0 instead of 0.5 -> material 0 reads 0; doubleSided not read -> false;
// the materials read in reverse -> every line fails.
TEST(GltfMaterials, AGltfFilesMaterialsReadAsWritten) {
    std::vector<GltfMaterialInfo> materials;
    ASSERT_TRUE(ReadGltfMaterials(Bytes(THREE_MATERIALS), materials));
    ExpectThree(materials);
}

// The same JSON inside a .glb container reads the same.
//
// Mutation: the JSON taken from byte 12 instead of 20 (the chunk header not
// skipped) -> it does not parse, false.
TEST(GltfMaterials, AGlbFilesMaterialsReadAsWritten) {
    std::vector<GltfMaterialInfo> materials;
    ASSERT_TRUE(ReadGltfMaterials(Glb(THREE_MATERIALS), materials));
    ExpectThree(materials);
}

// Bytes that are no glTF are refused, and the caller's list is emptied: text
// that is not JSON; a .glb whose declared length runs past its bytes; a .glb
// whose first chunk is not a JSON chunk.
//
// Mutations: the declared length not checked -> the truncated .glb still holds
// its whole JSON chunk and reads true; the chunk type not checked -> the chunk
// renamed "BSON" still holds valid JSON and reads true; the list not cleared ->
// a placeholder survives.
TEST(GltfMaterials, BytesThatAreNoGltfAreRefused) {
    std::vector<GltfMaterialInfo> materials = {GltfMaterialInfo{}};
    EXPECT_FALSE(ReadGltfMaterials(Bytes("this is not json"), materials));
    EXPECT_TRUE(materials.empty());

    std::vector<unsigned char> truncated = Glb(THREE_MATERIALS);
    truncated.resize(truncated.size() -
                     8); // the empty BIN chunk's header: the JSON chunk stays whole, the declared total overruns
    materials = {GltfMaterialInfo{}};
    EXPECT_FALSE(ReadGltfMaterials(truncated, materials));
    EXPECT_TRUE(materials.empty());

    std::vector<unsigned char> binFirst = Glb(THREE_MATERIALS);
    binFirst[16] = 'B'; // the first chunk's type, "JSON" -> "BSON"
    EXPECT_FALSE(ReadGltfMaterials(binFirst, materials));
}

// A file with no "materials" at all is a file whose meshes use the default:
// true, and empty.
//
// Mutation: a missing list treated as an error -> false.
TEST(GltfMaterials, AFileWithoutMaterialsReadsEmpty) {
    std::vector<GltfMaterialInfo> materials = {GltfMaterialInfo{}};
    EXPECT_TRUE(ReadGltfMaterials(Bytes(R"({"asset": {"version": "2.0"}})"), materials));
    EXPECT_TRUE(materials.empty());
}

// Emission: a material that says nothing gives none -- a black factor and a
// strength of 1; one with an emissiveFactor gives that, linear, as written; one
// with KHR_materials_emissive_strength gives that strength too.
//
// Mutations: emissiveFactor not read -> material 1 reads black; its channels
// read out of order -> (0.5, 0.25, 1) not (1, 0.5, 0.25); the extension not read
// -> material 2's strength 1; a default strength of 0 -> material 0 reads 0.
TEST(GltfMaterials, EmissionReadsAsWritten) {
    const std::string file = R"({
      "asset": {"version": "2.0"},
      "materials": [
        {},
        {"emissiveFactor": [1, 0.5, 0.25]},
        {"emissiveFactor": [1, 1, 1],
         "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 4}}}
      ]
    })";
    std::vector<GltfMaterialInfo> materials;
    ASSERT_TRUE(ReadGltfMaterials(Bytes(file), materials));
    ASSERT_EQ(materials.size(), 3u);
    for (int c = 0; c < 3; ++c)
        EXPECT_FLOAT_EQ(materials[0].EmissiveFactor[c], 0.0f) << "channel " << c;
    EXPECT_FLOAT_EQ(materials[0].EmissiveStrength, 1.0f);
    EXPECT_FLOAT_EQ(materials[1].EmissiveFactor[0], 1.0f);
    EXPECT_FLOAT_EQ(materials[1].EmissiveFactor[1], 0.5f);
    EXPECT_FLOAT_EQ(materials[1].EmissiveFactor[2], 0.25f);
    EXPECT_FLOAT_EQ(materials[1].EmissiveStrength, 1.0f);
    EXPECT_FLOAT_EQ(materials[2].EmissiveStrength, 4.0f);
}

// Metallic and roughness: a material that writes neither reads glTF's 1 and 1,
// marked as NOT written, so a reader can fall back; one that writes both reads
// them, marked written -- including a written 1, which is not the same as none.
//
// Mutations: metallicFactor not read -> material 1 reads 1 and not written;
// HasMetallicFactor set whether written or not -> material 0 reads written;
// roughnessFactor read from the metallic key -> material 1 reads 0.25, not 0.6.
TEST(GltfMaterials, MetallicAndRoughnessReadWithWhetherTheyWereWritten) {
    const std::string file = R"({
      "asset": {"version": "2.0"},
      "materials": [
        {"pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]}},
        {"pbrMetallicRoughness": {"metallicFactor": 0.25, "roughnessFactor": 0.6}},
        {"pbrMetallicRoughness": {"metallicFactor": 1}}
      ]
    })";
    std::vector<GltfMaterialInfo> materials;
    ASSERT_TRUE(ReadGltfMaterials(Bytes(file), materials));
    ASSERT_EQ(materials.size(), 3u);
    EXPECT_FALSE(materials[0].HasMetallicFactor);
    EXPECT_FALSE(materials[0].HasRoughnessFactor);
    EXPECT_FLOAT_EQ(materials[0].MetallicFactor, 1.0f) << "glTF's default, kept for a reader who wants it";
    EXPECT_TRUE(materials[1].HasMetallicFactor);
    EXPECT_TRUE(materials[1].HasRoughnessFactor);
    EXPECT_FLOAT_EQ(materials[1].MetallicFactor, 0.25f);
    EXPECT_FLOAT_EQ(materials[1].RoughnessFactor, 0.6f);
    EXPECT_TRUE(materials[2].HasMetallicFactor) << "a written 1 is written";
    EXPECT_FLOAT_EQ(materials[2].MetallicFactor, 1.0f);
    EXPECT_FALSE(materials[2].HasRoughnessFactor);
}

// KHR_materials_unlit is a flag by presence: an empty object under extensions.
// A material without it, or with only another extension, is lit.
//
// Mutations: the extension not read -> material 1 reads lit; read as present
// whenever there are extensions -> material 2 reads unlit.
TEST(GltfMaterials, UnlitReadsFromItsExtension) {
    const std::string file = R"({
      "asset": {"version": "2.0"},
      "materials": [
        {},
        {"extensions": {"KHR_materials_unlit": {}}},
        {"extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 2}}}
      ]
    })";
    std::vector<GltfMaterialInfo> materials;
    ASSERT_TRUE(ReadGltfMaterials(Bytes(file), materials));
    ASSERT_EQ(materials.size(), 3u);
    EXPECT_FALSE(materials[0].Unlit);
    EXPECT_TRUE(materials[1].Unlit);
    EXPECT_FALSE(materials[2].Unlit);
}
