#pragma once
#include "Device.h"
#include "GpuResources.h"
#include "Scene.h"

namespace fvk {

// Forward renderer using Vulkan 1.3 dynamic rendering. Renders each mesh
// instance as an indexed draw; per-instance transform and material ids are
// read from GPU arrays via shader push constants + storage buffers.
struct Renderer {
    Device* dev{};
    GpuResources gpu{};
    VkExtent2D extent{1280, 720};

    Device::Image colorImage{};
    Device::Image depthImage{};

    // push constants
    struct Push {
        glm::mat4 viewProj;
        glm::vec3 camPos;
        uint32_t instanceId;
        uint32_t lodIndex;
        uint32_t materialId;
    };

    void init(Device* device, VkExtent2D ext);
    void destroy();
    void resize(VkExtent2D ext);

    // Uploads scene data to GPU (materials + instance matrices) then renders.
    void render(const Scene& scene, const std::vector<glm::mat4>& instanceMatrices,
                const glm::mat4& viewProj, const glm::vec3& camPos,
                VkCommandBuffer cmd);

    void saveColorPNG(const std::string& path) const;
};

}
