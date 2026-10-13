#include "GpuResources.h"
#include <cstring>

#ifndef SPIRV_DIR
#define SPIRV_DIR "./shaders/"
#endif

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

void GpuResources::init(Device* device,
                        const std::vector<float>& pos, const std::vector<float>& nrm,
                        const std::vector<float>& uv, const std::vector<uint32_t>& idx,
                        size_t maxNodes, size_t maxMaterials) {
    dev = device;

    constexpr VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    constexpr VkMemoryPropertyFlags devMem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    // pad vec3 streams to vec4 (std430 array stride is 16 bytes)
    std::vector<float> pos4, nrm4;
    pos4.reserve(pos.size() / 3 * 4);
    nrm4.reserve(nrm.size() / 3 * 4);
    for (size_t i = 0; i + 2 < pos.size(); i += 3) {
        pos4.insert(pos4.end(), {pos[i], pos[i+1], pos[i+2], 0.0f});
        nrm4.insert(nrm4.end(), {nrm[i], nrm[i+1], nrm[i+2], 0.0f});
    }

    positions = dev->createBuffer(pos4.size() * sizeof(float), storage, devMem);
    dev->upload(positions, pos4.data(), pos4.size() * sizeof(float));
    normals = dev->createBuffer(nrm4.size() * sizeof(float), storage, devMem);
    dev->upload(normals, nrm4.data(), nrm4.size() * sizeof(float));
    uvs = dev->createBuffer(uv.size() * sizeof(float), storage, devMem);
    dev->upload(uvs, uv.data(), uv.size() * sizeof(float));
    indices = dev->createBuffer(idx.size() * sizeof(uint32_t), storage, devMem);
    dev->upload(indices, idx.data(), idx.size() * sizeof(uint32_t));

    nodeMatrices = dev->createBuffer(maxNodes * sizeof(glm::mat4),
        storage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, devMem);
    materialParams = dev->createBuffer(maxMaterials * sizeof(Material),
        storage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, devMem);

    // descriptor set: 0 positions, 1 normals, 2 uvs, 3 indices,
    // 4 nodeMatrices, 5 materialParams
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    for (uint32_t i = 0; i < 6; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = (i <= 3) ? VK_SHADER_STAGE_VERTEX_BIT
                                          : (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    VkDescriptorSetLayoutCreateInfo dsl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dsl.bindingCount = 6;
    dsl.pBindings = bindings.data();
    VKCHECK(vkCreateDescriptorSetLayout(dev->device, &dsl, nullptr, &descSetLayout));

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &poolSize;
    VKCHECK(vkCreateDescriptorPool(dev->device, &dpi, nullptr, &descPool));

    VkDescriptorSetAllocateInfo dsi{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsi.descriptorPool = descPool;
    dsi.descriptorSetCount = 1;
    dsi.pSetLayouts = &descSetLayout;
    VKCHECK(vkAllocateDescriptorSets(dev->device, &dsi, &descSet));

    const Device::Buffer* bufs[6] = {&positions, &normals, &uvs, &indices, &nodeMatrices, &materialParams};
    std::array<VkWriteDescriptorSet, 6> writes{};
    for (uint32_t i = 0; i < 6; i++) {
        VkDescriptorBufferInfo info{bufs[i]->buffer, 0, VK_WHOLE_SIZE};
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descSet;
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &info;
        // note: info must stay alive until update; copy into persistent storage
    }
    // use per-write persistent infos
    static std::array<VkDescriptorBufferInfo, 6> infos;
    for (uint32_t i = 0; i < 6; i++)
        infos[i] = {bufs[i]->buffer, 0, VK_WHOLE_SIZE};
    for (uint32_t i = 0; i < 6; i++) writes[i].pBufferInfo = &infos[i];
    vkUpdateDescriptorSets(dev->device, 6, writes.data(), 0, nullptr);

    // pipeline layout: set 0 + push constants
    VkPushConstantRange pc{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 96};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &descSetLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pc;
    VKCHECK(vkCreatePipelineLayout(dev->device, &pl, nullptr, &pipelineLayout));

    // graphics pipeline (Vulkan 1.3 dynamic rendering)
    auto vert = loadSpirv("forward.vert.spv");
    auto frag = loadSpirv("forward.frag.spv");

    VkShaderModuleCreateInfo vci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    vci.codeSize = vert.size();
    vci.pCode = (uint32_t*)vert.data();
    VkShaderModule vmod{VK_NULL_HANDLE};
    VKCHECK(vkCreateShaderModule(dev->device, &vci, nullptr, &vmod));

    VkShaderModuleCreateInfo fci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    fci.codeSize = frag.size();
    fci.pCode = (uint32_t*)frag.data();
    VkShaderModule fmod{VK_NULL_HANDLE};
    VKCHECK(vkCreateShaderModule(dev->device, &fci, nullptr, &fmod));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vmod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fmod;
    stages[1].pName = "main";

    VkPipelineDynamicStateCreateInfo dynState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    const VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dynStates;

    // no vertex input: fetch by index in the shader
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = 1;
    vs.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;

    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = depthFormat;
    rendering.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo gci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gci.pNext = &rendering;
    gci.stageCount = 2;
    gci.pStages = stages;
    gci.pVertexInputState = &vi;
    gci.pInputAssemblyState = &ia;
    gci.pViewportState = &vs;
    gci.pRasterizationState = &rs;
    gci.pMultisampleState = &ms;
    gci.pDepthStencilState = &ds;
    gci.pColorBlendState = &cb;
    gci.pDynamicState = &dynState;
    gci.layout = pipelineLayout;
    VKCHECK(vkCreateGraphicsPipelines(dev->device, VK_NULL_HANDLE, 1, &gci, nullptr, &pipeline));

    vkDestroyShaderModule(dev->device, vmod, nullptr);
    vkDestroyShaderModule(dev->device, fmod, nullptr);
}

void GpuResources::uploadNodeMatrices(const glm::mat4* matrices, size_t count) {
    dev->upload(nodeMatrices, matrices, count * sizeof(glm::mat4));
}

void GpuResources::uploadMaterials(const Material* materials, size_t count) {
    dev->upload(materialParams, materials, count * sizeof(Material));
}

void GpuResources::destroy() {
    if (!dev || !dev->device) return;
    vkDeviceWaitIdle(dev->device);
    if (pipeline) vkDestroyPipeline(dev->device, pipeline, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(dev->device, pipelineLayout, nullptr);
    if (descPool) vkDestroyDescriptorPool(dev->device, descPool, nullptr);
    if (descSetLayout) vkDestroyDescriptorSetLayout(dev->device, descSetLayout, nullptr);
    dev->destroy(positions);
    dev->destroy(normals);
    dev->destroy(uvs);
    dev->destroy(indices);
    dev->destroy(nodeMatrices);
    dev->destroy(materialParams);
}

}
