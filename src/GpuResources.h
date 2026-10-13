#pragma once
#include "Device.h"

namespace fvk {

// Data-oriented GPU resource pools: each vertex attribute lives in its own
// device buffer (structure-of-arrays), indices, per-node world matrices and
// materials in flat arrays. The vertex shader fetches by index.
struct GpuResources {
    Device* dev{};

    Device::Buffer positions;      // vec3 stream
    Device::Buffer normals;        // vec3 stream
    Device::Buffer uvs;            // vec2 stream
    Device::Buffer indices;        // u32 stream
    Device::Buffer nodeMatrices;   // world matrix per scene node
    Device::Buffer materialParams; // Material per material id

    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkDescriptorSetLayout descSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool descPool{VK_NULL_HANDLE};
    VkDescriptorSet descSet{VK_NULL_HANDLE};

    void init(Device* device,
              const std::vector<float>& pos, const std::vector<float>& nrm,
              const std::vector<float>& uv, const std::vector<uint32_t>& idx,
              size_t maxNodes, size_t maxMaterials);
    void destroy();
    void uploadNodeMatrices(const glm::mat4* matrices, size_t count);
    void uploadMaterials(const Material* materials, size_t count);
};

}
