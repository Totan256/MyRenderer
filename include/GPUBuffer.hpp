#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstring> // memcpy

class GpuBuffer {
public:
    GpuBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage);
    
    ~GpuBuffer();

    // コピー禁止
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    // データをCPUからGPUへ書き込む便利関数
    void writeData(const void* data, size_t size);

    // Vulkanハンドル取得
    VkBuffer getBuffer() const { return m_buffer; }
    VkDeviceSize getSize() const { return m_size; }
    VmaAllocation getAllocation() const { return m_allocation; } 

    void* map();
    void unmap();

private:
    VmaAllocator m_allocator; // メモリ管理者の参照
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE; // メモリの実体
    VkDeviceSize m_size;
};