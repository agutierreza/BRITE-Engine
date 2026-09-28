#include "Backends/GltfMaterials.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <string>

namespace BRITE {

namespace {

// The .glb container (glTF 2.0, section 4.4): a 12-byte header -- magic "glTF",
// version, total length -- then chunks, each an 8-byte header -- length, type --
// and its data. The first chunk is the JSON.
constexpr std::uint32_t GLB_MAGIC = 0x46546C67;      // "glTF", little-endian
constexpr std::uint32_t GLB_JSON_CHUNK = 0x4E4F534A; // "JSON", little-endian
constexpr std::size_t GLB_HEADER = 12;
constexpr std::size_t GLB_CHUNK_HEADER = 8;

std::uint32_t ReadU32(const std::vector<unsigned char>& bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (int i = 3; i >= 0; --i)
        value = (value << 8) | bytes[at + static_cast<std::size_t>(i)];
    return value;
}

// The JSON text of a .glb, or false when its header does not add up.
bool GlbJson(const std::vector<unsigned char>& bytes, std::string& json) {
    if (bytes.size() < GLB_HEADER + GLB_CHUNK_HEADER)
        return false;
    const std::uint32_t declared = ReadU32(bytes, 8);
    const std::uint32_t chunkLength = ReadU32(bytes, GLB_HEADER);
    const std::uint32_t chunkType = ReadU32(bytes, GLB_HEADER + 4);
    if (declared > bytes.size() || chunkType != GLB_JSON_CHUNK ||
        GLB_HEADER + GLB_CHUNK_HEADER + chunkLength > bytes.size())
        return false;
    const auto* start = reinterpret_cast<const char*>(bytes.data() + GLB_HEADER + GLB_CHUNK_HEADER);
    json.assign(start, chunkLength);
    return true;
}

GltfAlphaMode AlphaModeFrom(const std::string& name) {
    if (name == "MASK")
        return GltfAlphaMode::Mask;
    if (name == "BLEND")
        return GltfAlphaMode::Blend;
    return GltfAlphaMode::Opaque; // "OPAQUE", and anything glTF does not define
}

} // namespace

Color DrawColorFromLinear(const float rgba[4]) {
    auto byte = [](float value) {
        return static_cast<unsigned char>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    auto encode = [&](float linear) { return byte(std::pow(std::clamp(linear, 0.0f, 1.0f), 1.0f / 2.2f)); };
    return {encode(rgba[0]), encode(rgba[1]), encode(rgba[2]), byte(rgba[3])};
}

bool ReadGltfMaterials(const std::vector<unsigned char>& bytes, std::vector<GltfMaterialInfo>& materials) {
    materials.clear();
    std::string text;
    if (bytes.size() >= 4 && ReadU32(bytes, 0) == GLB_MAGIC) {
        if (!GlbJson(bytes, text))
            return false;
    } else {
        text.assign(bytes.begin(), bytes.end());
    }

    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return false;
    const auto list = root.find("materials");
    if (list == root.end() || !list->is_array())
        return true;

    for (const nlohmann::json& entry : *list) {
        GltfMaterialInfo info;
        if (entry.is_object()) {
            if (const auto mode = entry.find("alphaMode"); mode != entry.end() && mode->is_string())
                info.AlphaMode = AlphaModeFrom(mode->get<std::string>());
            if (const auto cutoff = entry.find("alphaCutoff"); cutoff != entry.end() && cutoff->is_number())
                info.AlphaCutoff = cutoff->get<float>();
            if (const auto sided = entry.find("doubleSided"); sided != entry.end() && sided->is_boolean())
                info.DoubleSided = sided->get<bool>();
            if (const auto pbr = entry.find("pbrMetallicRoughness"); pbr != entry.end() && pbr->is_object()) {
                if (const auto base = pbr->find("baseColorFactor");
                    base != pbr->end() && base->is_array() && base->size() == 4) {
                    for (std::size_t c = 0; c < 4; ++c)
                        if ((*base)[c].is_number())
                            info.BaseColorFactor[c] = (*base)[c].get<float>();
                }
                if (const auto metallic = pbr->find("metallicFactor");
                    metallic != pbr->end() && metallic->is_number()) {
                    info.MetallicFactor = metallic->get<float>();
                    info.HasMetallicFactor = true;
                }
                if (const auto rough = pbr->find("roughnessFactor"); rough != pbr->end() && rough->is_number()) {
                    info.RoughnessFactor = rough->get<float>();
                    info.HasRoughnessFactor = true;
                }
            }
            if (const auto normal = entry.find("normalTexture"); normal != entry.end() && normal->is_object()) {
                const auto scale = normal->find("scale");
                if (scale != normal->end() && scale->is_number())
                    info.NormalScale = scale->get<float>();
            }
            if (const auto occlusion = entry.find("occlusionTexture");
                occlusion != entry.end() && occlusion->is_object()) {
                const auto strength = occlusion->find("strength");
                if (strength != occlusion->end() && strength->is_number())
                    info.OcclusionStrength = strength->get<float>();
            }
            if (const auto emissive = entry.find("emissiveFactor");
                emissive != entry.end() && emissive->is_array() && emissive->size() == 3) {
                for (std::size_t c = 0; c < 3; ++c)
                    if ((*emissive)[c].is_number())
                        info.EmissiveFactor[c] = (*emissive)[c].get<float>();
            }
            if (const auto extensions = entry.find("extensions");
                extensions != entry.end() && extensions->is_object()) {
                // KHR_materials_unlit carries no properties: its presence is the flag.
                info.Unlit = extensions->contains("KHR_materials_unlit");
                const auto strength = extensions->find("KHR_materials_emissive_strength");
                if (strength != extensions->end() && strength->is_object()) {
                    const auto value = strength->find("emissiveStrength");
                    if (value != strength->end() && value->is_number())
                        info.EmissiveStrength = value->get<float>();
                }
            }
        }
        materials.push_back(info);
    }
    return true;
}

} // namespace BRITE
