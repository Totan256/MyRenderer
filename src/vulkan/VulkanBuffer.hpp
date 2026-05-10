#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstring> // memcpy
#include "vulkan/VulkanDevice.hpp"
#include "RHIcommon.hpp"

namespace rhi::vk{
    class VulkanBuffer : public rhi::Buffer {
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
        VkDeviceSize getSize() const { return m_desc.size; }
        VmaAllocation getAllocation() const { return m_allocation; } 

        void* map() override;
        void unmap() override;

        uint32_t getBindlessIndex() const { return m_bindlessIndex; }

        rhi::ResourceState getCurrentState() const override { return m_state; }
        rhi::ShaderStage   getCurrentStage() const override { return m_stage; }
        
        void setState(rhi::ResourceState state, rhi::ShaderStage stage) override {
            m_state = state;
            m_stage = stage;
        }
        bool isImage() const override { return false; }
        BufferDesc getDesc() const { return m_desc; }
    private:
        VulkanDevice& m_device;
        VmaAllocator m_allocator; // メモリ管理者の参照
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VmaAllocation m_allocation = VK_NULL_HANDLE; // メモリの実体
        BufferDesc m_desc;

        rhi::ResourceState m_state = rhi::ResourceState::Undefined;
        rhi::ShaderStage m_stage = rhi::ShaderStage::None;
        void* m_mappedPtr;
        bool m_isPersistentlyMapped = false;
        uint32_t m_bindlessIndex;
    };

}