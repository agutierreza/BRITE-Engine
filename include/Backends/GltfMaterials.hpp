#pragma once

#include <vector>

namespace BRITE {

// How a glTF material's alpha is used.
enum class GltfAlphaMode {
    Opaque, // alpha ignored: the surface is solid
    Mask,   // alpha below the cutoff is not drawn, and the rest is solid
    Blend,  // alpha blends with what is behind it
};

// What a glTF file says about one of its materials, beyond the base colour and
// albedo texture a model loader reads for itself. Each field holds glTF's own
// default when the file leaves it out.
struct GltfMaterialInfo {
    GltfAlphaMode AlphaMode = GltfAlphaMode::Opaque;
    float AlphaCutoff = 0.5f; // read for Mask; glTF's default
    bool DoubleSided = false;
    // The light the surface gives off, linear RGB, times the emissive texture
    // when there is one: black, none, unless the file says otherwise.
    float EmissiveFactor[3] = {0.0f, 0.0f, 0.0f};
    // KHR_materials_emissive_strength: a multiplier on EmissiveFactor, so a
    // factor capped at 1 can still glow brighter. 1 when the file has none.
    float EmissiveStrength = 1.0f;
};

// Reads the materials of a glTF file, in the order the file lists them, from
// the whole file's bytes: a .gltf (JSON) or a .glb (the binary container, whose
// first chunk is that JSON). Pure: no GPU, no file access, no logging.
//
// Returns false, with `materials` empty, when the bytes are neither -- JSON that
// does not parse, a .glb header that does not add up. A file with no materials
// is true and empty.
bool ReadGltfMaterials(const std::vector<unsigned char>& bytes, std::vector<GltfMaterialInfo>& materials);

} // namespace BRITE
