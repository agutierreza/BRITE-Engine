#pragma once

#include "Math/BriteMath.hpp"
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
    // pbrMetallicRoughness's two scalars, and whether the file wrote each. glTF
    // defaults both to 1 -- fully metallic, fully rough -- and a fully metallic
    // surface has no diffuse light at all, so a file that says nothing is not
    // taken to mean metal: a reader uses its own fallback where Has... is false.
    float MetallicFactor = 1.0f;
    float RoughnessFactor = 1.0f;
    bool HasMetallicFactor = false;
    bool HasRoughnessFactor = false;
    // KHR_materials_unlit: the material is drawn as its base colour, with no
    // lighting -- a sky, a backdrop, a painted card.
    bool Unlit = false;
    // occlusionTexture.strength: how much of the occlusion map applies, 0 to 1.
    float OcclusionStrength = 1.0f;
    // normalTexture.scale: how far the normal map tilts the surface, 1 as authored.
    float NormalScale = 1.0f;
    // pbrMetallicRoughness.baseColorFactor, as glTF defines it: LINEAR RGB, and
    // alpha. White when the file says nothing.
    float BaseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

// The draw colour a glTF base colour factor means. glTF's factor is linear; the
// engine's draw colours -- a tint, the vertex colours of a mesh built in code --
// are sRGB bytes, which the lit shader decodes with a 2.2 power. So each colour
// channel is encoded with that same 2.2: the one curve under which the shader
// gets back the linear value the file gave, to a byte's rounding. (The standard
// sRGB curve would not: its byte for 0.5, 188, decodes to 0.511 here, and a dark
// green comes back some 13% darker than the file said.) Alpha is coverage, not
// colour, and is only scaled. Each channel rounds to the nearest byte.
Color DrawColorFromLinear(const float rgba[4]);

// Reads the materials of a glTF file, in the order the file lists them, from
// the whole file's bytes: a .gltf (JSON) or a .glb (the binary container, whose
// first chunk is that JSON). Pure: no GPU, no file access, no logging.
//
// Returns false, with `materials` empty, when the bytes are neither -- JSON that
// does not parse, a .glb header that does not add up. A file with no materials
// is true and empty.
bool ReadGltfMaterials(const std::vector<unsigned char>& bytes, std::vector<GltfMaterialInfo>& materials);

} // namespace BRITE
