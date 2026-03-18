#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "VulkanDevice.hpp"
#include "VulkanCommandList.hpp"

class VulkanImage{
public:
    VulkanImage(VulkanDevice& device, uint32_t width, uint32_t height);
    ~VulkanImage();
    // コピー禁止
    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;

    // バリアを張ってレイアウトを変更
    void transitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout);
    // バッファに変換
    void copyToBuffer(VkCommandBuffer cmd, VkBuffer buffer);

    VkImage getImage() const {return m_image;}
    VkImageView getView() const {return m_view;}
    uint32_t getBindlessIndex() const { return m_bindlessIndex; }
    rhi::ResourceUsage getCurrentUsage() const { return m_usage; }
    void setCurrentUsage(rhi::ResourceUsage usage) { m_usage = usage; }
private:
    VulkanDevice& m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;

    uint32_t m_width;
    uint32_t m_height;
    VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;// 後で消す
    rhi::ResourceUsage m_usage = rhi::ResourceUsage::Undefined;

    uint32_t m_bindlessIndex;
};

