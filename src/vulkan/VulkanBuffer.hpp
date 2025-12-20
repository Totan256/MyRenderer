#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstring> // memcpy
#include "vulkan/VulkanDevice.hpp"

class VulkanBuffer {
public:
    VulkanBuffer(VulkanDevice& device, VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage);
    
    ~VulkanBuffer();

    // コピー禁止
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    // データをCPUからGPUへ書き込む便利関数
    void writeData(const void* data, size_t size);

    // Vulkanハンドル取得
    VkBuffer getNativeBuffer() const { return m_buffer; }
    VkDeviceSize getSize() const { return m_size; }
    VmaAllocation getAllocation() const { return m_allocation; } 

    void* map();
    void unmap();

    uint32_t getBindlessIndex() const { return m_bindlessIndex; }
private:
    VulkanDevice& m_device;
    VmaAllocator m_allocator; // メモリ管理者の参照
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE; // メモリの実体
    VkDeviceSize m_size;
    uint32_t m_bindlessIndex;
};