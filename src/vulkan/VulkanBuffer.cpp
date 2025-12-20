#include "VulkanBuffer.hpp"
#include <stdexcept>
#include <iostream>

VulkanBuffer::VulkanBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage)
    : m_allocator(allocator), m_size(size) {
    
    // 1. バッファ作成情報の定義
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = bufferUsage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // 2. VMAの割り当て情報の定義
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;
    
    // VMA_MEMORY_USAGE_AUTO はVulkanの推奨メモリタイプを自動選択します。
    // CPUから書き込みたい場合（CPU_TO_GPU）は、自動的にマッパブルなメモリを選んでくれます。
    if (memoryUsage == VMA_MEMORY_USAGE_AUTO_PREFER_HOST || 
        memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU) {
        // マップ可能にしておく（永続的にマップするフラグ）
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | 
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    // 3. バッファとメモリを同時に作成
    VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_buffer, &m_allocation, nullptr);
    
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }
}

VulkanBuffer::~VulkanBuffer() {
    // メモリとバッファを解放
    if (m_buffer != VK_NULL_HANDLE && m_allocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }
}

void VulkanBuffer::writeData(const void* data, size_t dataSize) {
    // サイズチェック
    if (dataSize > m_size) {
        throw std::runtime_error("Data size is larger than buffer size!");
    }

    // VMAを使ってメモリをマップ（CPUからアクセス可能なポインタを取得）
    void* mappedData;
    VkResult result = vmaMapMemory(m_allocator, m_allocation, &mappedData);
    
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to map buffer memory!");
    }

    // データをコピー
    std::memcpy(mappedData, data, dataSize);

    // アンマップ（書き込み終了）
    vmaUnmapMemory(m_allocator, m_allocation);
}

void* VulkanBuffer::map() {
    void* data;
    VkResult result = vmaMapMemory(m_allocator, m_allocation, &data);
    if (result != VK_SUCCESS) throw std::runtime_error("Failed to map memory");
    return data;
}

void VulkanBuffer::unmap() {
    vmaUnmapMemory(m_allocator, m_allocation);
}