#pragma once
#include "Common.h"

namespace fvk {

// Data-oriented scene storage: node transforms, materials and mesh ranges are
// stored in flat arrays (structure of arrays) and can be modified at runtime.
struct Scene {
    // --- per node ---
    std::vector<glm::vec3> nodeTranslation;
    std::vector<glm::quat> nodeRotation;
    std::vector<glm::vec3> nodeScale;
    std::vector<glm::mat4> nodeLocalMatrix;   // cached local matrices
    std::vector<int32_t> nodeParent;          // -1 root
    std::vector<uint32_t> nodeMesh;           // glTF mesh index + 1, 0 = none
    std::vector<bool> nodeDirty;

    // --- per mesh (primitive ranges) ---
    std::vector<uint32_t> meshFirstLod;       // index into lodRanges
    std::vector<uint32_t> lodCount;
    struct LodRange {
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t firstVertex;                 // offset in the shared vertex streams
        int32_t materialId;                   // -1 = default
    };
    std::vector<LodRange> lodRanges;

    // --- materials (SoA, GPU-mirrored) ---
    std::vector<Material> materials;
    std::vector<std::string> materialNames;

    void reserve(size_t nodes, size_t meshes, size_t materials);
    uint32_t addNode(uint32_t mesh = 0, int32_t parent = -1);
    void markDirty(uint32_t node) { if (node < nodeDirty.size()) nodeDirty[node] = true; }

    // Recompute local matrices from TRS for dirty nodes; call before rendering.
    void updateMatrices(std::vector<glm::mat4>& outInstanceMatrices);

    size_t nodeCount() const { return nodeTranslation.size(); }

    // Runtime material editing
    Material& material(uint32_t id) { return materials[id]; }
    void setMaterialBaseColor(uint32_t id, const glm::vec4& c) { materials[id].baseColorFactor = c; }
};

}
