#pragma once
#include "Scene.h"
#include "Common.h"

namespace fvk {

// Loads a glTF (.gltf with external .bin or GLB) into the data-oriented Scene.
// Vertex data is appended to the output streams; Scene holds all ranges and
// per-node data. Textures are skipped in this minimal version (albedo via
// material.baseColorFactor), but material flags are parsed.
struct GltfLoadResult {
    Scene scene;
    // CPU vertex streams (SoA) to upload
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<float> joints;
    std::vector<float> weights;
    std::vector<uint32_t> indices;
};

std::optional<GltfLoadResult> loadGltf(const std::string& path);

}
