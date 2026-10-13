#include "Renderer.h"
#include <cstring>

#ifndef SPIRV_DIR
#define SPIRV_DIR "./shaders/"
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace fvk {

static std::vector<char> loadSpirv(const char* name) {
    std::string p = std::string(SPIRV_DIR) + name;
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "Cannot open SPIR-V %s\n", p.c_str()); std::abort(); }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<char> out(static_cast<size_t>(n));
    size_t rd = std::fread(out.data(), 1, size_t(n), f);
    (void)rd;
    std::fclose(f);
    return out;
}

void Renderer::init(Device* device, VkExtent2D ext) {
    dev = device;
    extent = ext;
    colorImage = dev->createImage2D(extent, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_TILING_OPTIMAL, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    depthImage = dev->createImage2D(extent, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_TILING_OPTIMAL, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // transition images once via a one-shot command buffer
    VkCommandBuffer cmd = dev->beginOneShot();
    Device::transitionImage(cmd, colorImage.image, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    b.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    b.image = depthImage.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
    dev->endOneShot(cmd);
}

void Renderer::destroy() {
    if (!dev) return;
    dev->destroy(colorImage);
    dev->destroy(depthImage);
}

void Renderer::render(const Scene& scene, const std::vector<glm::mat4>& instanceMatrices,
                      const glm::mat4& viewProj, const glm::vec3& camPos,
                      VkCommandBuffer cmd) {
    gpu.uploadNodeMatrices(instanceMatrices.data(), instanceMatrices.size());
    gpu.uploadMaterials(scene.materials.data(), scene.materials.size());

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = colorImage.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.03f, 0.05f, 0.09f, 1.0f}};

    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = depthImage.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo rinfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rinfo.renderArea = {{0, 0}, extent};
    rinfo.layerCount = 1;
    rinfo.colorAttachmentCount = 1;
    rinfo.pColorAttachments = &color;
    rinfo.pDepthAttachment = &depth;

    vkCmdBeginRendering(cmd, &rinfo);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu.pipelineLayout,
                            0, 1, &gpu.descSet, 0, nullptr);

    VkViewport vp{0, 0.0f, float(extent.width), float(extent.height), 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    struct PushData {
        glm::mat4 viewProj;
        glm::vec3 camPos;
        uint32_t nodeId;
        uint32_t lodFirst;
        uint32_t lodCount;
        uint32_t materialId;
    };
    PushData push{};
    push.viewProj = viewProj;
    push.camPos = camPos;
    push.lodCount = 1;

    for (size_t n = 0; n < scene.nodeCount(); n++) {
        if (scene.nodeMesh[n] == 0) continue;
        uint32_t mesh = scene.nodeMesh[n] - 1;
        if (mesh >= scene.meshFirstLod.size()) continue;
        push.nodeId = uint32_t(n);
        uint32_t lodCount = scene.lodCount[mesh];
        for (uint32_t l = 0; l < lodCount; l++) {
            const auto& r = scene.lodRanges[scene.meshFirstLod[mesh] + l];
            push.materialId = r.materialId < 0 ? 0u : uint32_t(r.materialId);
            push.lodFirst = r.firstIndex;
            vkCmdPushConstants(cmd, gpu.pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushData), &push);
            vkCmdDraw(cmd, r.indexCount, 1, 0, 0);
        }
    }

    vkCmdEndRendering(cmd);

    // transition color to transfer src for readback
    Device::transitionImage(cmd, colorImage.image, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
}

void Renderer::saveColorPNG(const std::string& path) const {
    // read back via staging buffer
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = VkDeviceSize(extent.width) * extent.height * 4;
    ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buf;
    VKCHECK(vkCreateBuffer(dev->device, &ci, nullptr, &buf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev->device, buf, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = dev->findMemory(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory mem;
    VKCHECK(vkAllocateMemory(dev->device, &ai, nullptr, &mem));
    VKCHECK(vkBindBufferMemory(dev->device, buf, mem, 0));

    VkCommandBuffer cmd = dev->beginOneShot();
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, colorImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &copy);
    dev->endOneShot(cmd);

    void* mapped = nullptr;
    VKCHECK(vkMapMemory(dev->device, mem, 0, ci.size, 0, &mapped));
    stbi_flip_vertically_on_write(1);
    stbi_write_png(path.c_str(), int(extent.width), int(extent.height), 4, mapped, int(extent.width) * 4);
    vkUnmapMemory(dev->device, mem);
    vkFreeMemory(dev->device, mem, nullptr);
    vkDestroyBuffer(dev->device, buf, nullptr);
}

}
