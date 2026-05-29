#include "VulkanBuffer.hpp"
#include <stdexcept>
#include <iostream>
namespace rhi::vk {
    
    VulkanBuffer::VulkanBuffer(VulkanDevice& device, VmaAllocator allocator,
        VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage)
        : m_device(device), m_allocator(allocator) {
        
        if (bufferUsage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
            VkDeviceSize align = m_device.getMinUniformBufferOffsetAlignment();
            m_desc.size = (size + align - 1) & ~(align - 1);
        } else {
            m_desc.size = size;
        }
        
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = m_desc.size;
        bufferInfo.usage = bufferUsage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = memoryUsage;
        VkResult result;
        
        if (memoryUsage == VMA_MEMORY_USAGE_AUTO_PREFER_HOST || 
            memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU) {
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | 
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;
            m_isPersistentlyMapped = true;
            VmaAllocationInfo resInfo;
            result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_buffer, &m_allocation, &resInfo);
            m_mappedPtr = resInfo.pMappedData;
            
        } else {
            result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_buffer, &m_allocation, nullptr);
        }

        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create buffer!");
        }

        if (bufferUsage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
            m_bindlessIndex = device.registerUniformBuffer(m_buffer, m_desc.size);
            m_bindlessBinding = 2; // Binding 2: Uniform Buffer
        } else {
            m_bindlessIndex = device.registerBuffer(m_buffer,  m_desc.size);
            m_bindlessBinding = 0; // Binding 0: Storage Buffer
        }
    }

    VulkanBuffer::~VulkanBuffer() {
        VkBuffer buffer = m_buffer;
        VmaAllocation alloc = m_allocation;
        VmaAllocator allocator = m_allocator;
        uint32_t bindlessIdx = m_bindlessIndex;
        uint32_t bindlessBinding = m_bindlessBinding;
        auto& device = m_device;

        m_device.enqueueDeletion([buffer, alloc, allocator, bindlessIdx, bindlessBinding, &device]() {
            if (buffer != VK_NULL_HANDLE) {
                device.unregisterIndex(bindlessIdx, bindlessBinding);
                vmaDestroyBuffer(allocator, buffer, alloc);
            }
        });
    }

    void VulkanBuffer::writeData(const void* data, size_t dataSize) {
        if (dataSize > m_desc.size) {
            throw std::runtime_error("Data size is larger than buffer size!");
        }

        void* mappedData;
        if(m_isPersistentlyMapped){
            mappedData = m_mappedPtr;
            std::memcpy(mappedData, data, dataSize);
        }else{
            VkResult result = vmaMapMemory(m_allocator, m_allocation, &mappedData);
            if (result != VK_SUCCESS) {
                throw std::runtime_error("Failed to map buffer memory!");
            }
            std::memcpy(mappedData, data, dataSize);
            vmaUnmapMemory(m_allocator, m_allocation);
        }
    }

    void* VulkanBuffer::map() {
        if (m_isPersistentlyMapped)  return m_mappedPtr;
        
        void* data;
        VkResult result = vmaMapMemory(m_allocator, m_allocation, &data);
        if (result != VK_SUCCESS) throw std::runtime_error("Failed to map memory");
        return data;
    }

    void VulkanBuffer::unmap() {
        if(m_isPersistentlyMapped) return;
        vmaUnmapMemory(m_allocator, m_allocation);
    }

    void VulkanBuffer::invalidate(size_t offset, size_t size) {
        VkMemoryPropertyFlags memFlags;
        vmaGetAllocationMemoryProperties(m_allocator, m_allocation, &memFlags);
        // HOST_COHERENT（自動同期）でない場合のみ、明示的なInvalidateが必要
        if ((memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
            vmaInvalidateAllocation(m_allocator, m_allocation, offset, size);
        }
    }
}