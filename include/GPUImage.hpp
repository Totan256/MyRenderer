#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "GraphicsDevice.hpp"
#include "CommandList.hpp"

class GpuImage{
public:
    GpuImage(GraphicsDevice& device, uint32_t width, uint32_t height);
    ~GpuImage();
    // コピー禁止
    GpuImage(const GpuImage&) = delete;
    GpuImage& operator=(const GpuImage&) = delete;

    // バリアを張ってレイアウトを変更
    void transitionLayout(VkCommandBuffer cmd, VkImageLayout oldLayout, VkImageLayout newLayout);
    // バッファに変換
    void copyToBuffer(VkCommandBuffer cmd, VkBuffer buffer);

    VkImage getImage() const {return m_image;}
    VkImageView getView() const {return m_view;}

private:
    GraphicsDevice& m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;

    uint32_t m_width;
    uint32_t m_height;
    VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

