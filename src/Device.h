#pragma once
#include "Common.h"
#include <deque>

namespace fvk {

struct Device {
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physical{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    uint32_t queueFamily{};
    VkCommandPool cmdPool{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddress{};

    static std::unique_ptr<Device> create(bool enableValidation);
    ~Device();

    uint32_t findMemory(uint32_t typeBits, VkMemoryPropertyFlags props) const;
    VkCommandBuffer beginOneShot() const;
    void endOneShot(VkCommandBuffer cmd) const;

    struct Buffer {
        VkBuffer buffer{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkDeviceSize size{};
        VkDeviceAddress address{};
        void* mapped{nullptr};
    };

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags props, bool map = false) const;
    void upload(const Buffer& dst, const void* data, VkDeviceSize size, VkDeviceSize offset = 0) const;
    void destroy(Buffer& b) const;

    struct Image {
        VkImage image{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkExtent2D extent{};
        VkFormat format{};
    };

    Image createImage2D(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                        VkImageTiling tiling, VkMemoryPropertyFlags props) const;
    static void transitionImage(VkCommandBuffer cmd, VkImage image, VkFormat format,
                                VkImageLayout oldLayout, VkImageLayout newLayout,
                                uint32_t queueFamily = VK_QUEUE_FAMILY_IGNORED);
    void destroy(Image& im) const;
};

}
