#pragma once
#include <volk.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <cstdio>


#define VKCHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { std::fprintf(stderr, "Vulkan error %d at %s:%d\n", int(r_), __FILE__, __LINE__); std::abort(); } } while (0)

namespace fvk {

struct Device;
struct GpuResources;
struct Scene;
struct Renderer;

struct Material {
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor{1.0f};
    float roughnessFactor{1.0f};
    float emissiveStrength{1.0f};
    uint32_t baseColorTex{0};
    uint32_t metallicRoughnessTex{0};
    uint32_t normalTex{0};
    uint32_t flags{0};   // bit0: has baseColorTex, bit1: has MR tex, bit2: has normal tex, bit3: alpha blend
    float alphaCutoff{0.5f};
    uint32_t pad0{0};
    uint32_t pad1{0};
    uint32_t pad2{0};
    uint32_t pad3{0};
};
static_assert(sizeof(Material) == 64);

struct VertexFormats {
    static VkVertexInputBindingDescription binding() {
        return {0, sizeof(float) * 14, VK_VERTEX_INPUT_RATE_VERTEX};
    }
    static std::array<VkVertexInputAttributeDescription, 5> attributes() {
        return {{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},   // position
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},  // normal
            {2, 0, VK_FORMAT_R32G32_SFLOAT, 24},     // uv
            {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32},// joints
            {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 48},// weights
        }};
    }
};

}
