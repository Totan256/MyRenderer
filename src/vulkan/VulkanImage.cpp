#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "VulkanDevice.hpp"
// #include "VulkanCommandList.hpp"
#include "VulkanImage.hpp"
namespace rhi::vk{
    VulkanImage::VulkanImage(VulkanDevice& device, uint32_t width, uint32_t height)
        : m_device(device) {

        m_desc.width = width;
        m_desc.height = height;
        
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // RGBA 8bit
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;  // GPU最適化配置
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        // USAGE: Storage(書き込み), TransferSrc(コピー元), Sampled(テクスチャとして読む)
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
        if (m_view != VK_NULL_HANDLE) {
            m_device.unregisterIndex(m_bindlessIndex);
            vkDestroyImageView(m_device.getDevice(), m_view, nullptr);
        }
        if (m_image != VK_NULL_HANDLE && m_allocation != VK_NULL_HANDLE) {
            vmaDestroyImage(m_device.getAllocator(), m_image, m_allocation);
        }
    }
}