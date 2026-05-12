
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "VulkanDevice.hpp"
// #include "VulkanCommandList.hpp"
#include "VulkanImage.hpp"
namespace rhi::vk{
    VulkanImage::VulkanImage(VulkanDevice& device, const ImageDesc& desc, VkImageUsageFlags usage)
        : m_device(device), m_desc(desc) {
        
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_desc.width;
        imageInfo.extent.height = m_desc.height;
        imageInfo.extent.depth = m_desc.depth;
        imageInfo.mipLevels = m_desc.mipLevels;
        imageInfo.arrayLayers = m_desc.arrayLayers;
        // ToDo: Format も mapFormat 関数等を作って Vulkan フラグに変換
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // RGBA 8bit
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;  // GPU最適化配置
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        // USAGE: Storage(書き込み), TransferSrc(コピー元), Sampled(テクスチャとして読む)
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        // VMA割り当て
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE; // VRAMに配置
        if(vmaCreateImage(device.getAllocator(), &imageInfo, &allocInfo,
            &m_image, &m_allocation, nullptr) != VK_SUCCESS){
            throw std::runtime_error("failed to create image");
        }

        // image view　shaderから見た設定
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if(vkCreateImageView(device.getDevice(), &viewInfo,
            nullptr, &m_view) != VK_SUCCESS){
            throw std::runtime_error("failed to create image view");
        }

        m_bindlessIndex = device.registerImage(m_view);
    }

    VulkanImage::~VulkanImage(){
        VkImage image = m_image;
        VkImageView view = m_view;
        VmaAllocation alloc = m_allocation;
        uint32_t bindlessIdx = m_bindlessIndex;
        
        // デバイスの参照をキャプチャし、遅延破棄を登録
        m_device.enqueueDeletion([&device = m_device, image, view, alloc, bindlessIdx]() {
            if (view != VK_NULL_HANDLE) {
                device.unregisterIndex(bindlessIdx);
                vkDestroyImageView(device.getDevice(), view, nullptr);
            }
            if (image != VK_NULL_HANDLE) {
                vmaDestroyImage(device.getAllocator(), image, alloc);
            }
        });
    }
}