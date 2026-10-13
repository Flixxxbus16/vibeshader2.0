#include "Device.h"
#include <cstring>
#include <cstdlib>

namespace fvk {

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::fprintf(stderr, "[validation] %s\n", data->pMessage);
    return VK_FALSE;
}

std::unique_ptr<Device> Device::create(bool enableValidation) {
    std::unique_ptr<Device> d(new Device());
    if (volkInitialize() != VK_SUCCESS) { std::fprintf(stderr, "volkInitialize failed\n"); return nullptr; }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "forward-vulkan";
    app.pEngineName = "fvk";
    app.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> layers;
    if (enableValidation) layers.push_back("VK_LAYER_KHRONOS_validation");
    std::vector<const char*> extensions;

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledLayerCount = uint32_t(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    ci.enabledExtensionCount = uint32_t(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    VkResult r = vkCreateInstance(&ci, nullptr, &d->instance);
    if (r != VK_SUCCESS && enableValidation) {
        ci.enabledLayerCount = 0;
        layers.clear();
        r = vkCreateInstance(&ci, nullptr, &d->instance);
    }
    if (r != VK_SUCCESS) { std::fprintf(stderr, "vkCreateInstance failed: %d\n", int(r)); return nullptr; }
    volkLoadInstance(d->instance);

    if (enableValidation) {
        auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            d->instance, "vkCreateDebugUtilsMessengerEXT");
        if (create) {
            VkDebugUtilsMessengerCreateInfoEXT dbg{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dbg.pfnUserCallback = debugCallback;
            create(d->instance, &dbg, nullptr, &d->debugMessenger);
        }
    }

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(d->instance, &n, nullptr);
    if (n == 0) { std::fprintf(stderr, "No Vulkan devices found\n"); return nullptr; }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(d->instance, &n, devs.data());
    d->physical = devs[0];

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.pNext = &f13;
    f12.bufferDeviceAddress = VK_TRUE;
    f12.descriptorBindingPartiallyBound = VK_TRUE;
    f12.runtimeDescriptorArray = VK_TRUE;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = 0;
    q.queueCount = 1;
    q.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f12;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &q;
    VKCHECK(vkCreateDevice(d->physical, &dci, nullptr, &d->device));

    volkLoadDevice(d->device);
    vkGetDeviceQueue(d->device, 0, 0, &d->queue);
    d->queueFamily = 0;

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = 0;
    VKCHECK(vkCreateCommandPool(d->device, &pci, nullptr, &d->cmdPool));

    d->vkGetBufferDeviceAddress =
        (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(d->device, "vkGetBufferDeviceAddress");
    return d;
}

Device::~Device() {
    if (!device) return;
    vkDeviceWaitIdle(device);
    if (debugMessenger) {
        auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy) destroy(instance, debugMessenger, nullptr);
    }
    if (cmdPool) vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyDevice(device, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
}

uint32_t Device::findMemory(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(physical, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    std::fprintf(stderr, "findMemory: no compatible memory type\n");
    std::abort();
}

VkCommandBuffer Device::beginOneShot() const {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VKCHECK(vkAllocateCommandBuffers(device, &ai, &cmd));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VKCHECK(vkBeginCommandBuffer(cmd, &bi));
    return cmd;
}

void Device::endOneShot(VkCommandBuffer cmd) const {
    VKCHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VKCHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
}

Device::Buffer Device::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags props, bool map) const {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VKCHECK(vkCreateBuffer(device, &ci, nullptr, &b.buffer));

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, b.buffer, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemory(mr.memoryTypeBits, props);
    VKCHECK(vkAllocateMemory(device, &ai, nullptr, &b.memory));
    VKCHECK(vkBindBufferMemory(device, b.buffer, b.memory, 0));
    if (map) VKCHECK(vkMapMemory(device, b.memory, 0, size, 0, &b.mapped));
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        ai.buffer = b.buffer;
        b.address = vkGetBufferDeviceAddress(device, &ai);
    }
    return b;
}

void Device::upload(const Buffer& dst, const void* data, VkDeviceSize size, VkDeviceSize offset) const {
    if (dst.mapped) {
        std::memcpy((char*)dst.mapped + offset, data, size);
        return;
    }
    Buffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true);
    std::memcpy(staging.mapped, data, size);
    VkCommandBuffer cmd = beginOneShot();
    VkBufferCopy copy{offset, 0, size};
    vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &copy);
    endOneShot(cmd);
    destroy(staging);
}

void Device::destroy(Buffer& b) const {
    if (!device) return;
    if (b.mapped) vkUnmapMemory(device, b.memory);
    if (b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
    if (b.memory) vkFreeMemory(device, b.memory, nullptr);
    b = {};
}

Device::Image Device::createImage2D(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                                    VkImageTiling tiling, VkMemoryPropertyFlags props) const {
    Image im;
    im.extent = extent;
    im.format = format;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {extent.width, extent.height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = tiling;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKCHECK(vkCreateImage(device, &ci, nullptr, &im.image));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, im.image, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemory(mr.memoryTypeBits, props);
    VKCHECK(vkAllocateMemory(device, &ai, nullptr, &im.memory));
    VKCHECK(vkBindImageMemory(device, im.image, im.memory, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VKCHECK(vkCreateImageView(device, &vi, nullptr, &im.view));
    return im;
}

void Device::transitionImage(VkCommandBuffer cmd, VkImage image, VkFormat,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             uint32_t queueFamily) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
        ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = queueFamily;
    b.dstQueueFamilyIndex = queueFamily;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void Device::destroy(Image& im) const {
    if (!device) return;
    if (im.view) vkDestroyImageView(device, im.view, nullptr);
    if (im.image) vkDestroyImage(device, im.image, nullptr);
    if (im.memory) vkFreeMemory(device, im.memory, nullptr);
    im = {};
}

}
