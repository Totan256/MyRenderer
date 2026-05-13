#include "VulkanConstantBufferManager.hpp"
#include <stdexcept>

namespace rhi::vk {

    ConstantBufferManager::ConstantBufferManager(VulkanDevice& device, uint32_t ringBufferSize, uint32_t frameCount)
        : m_device(device), m_ringBufferSize(ringBufferSize) {
        
        m_alignment = device.getMinUniformBufferOffsetAlignment();

        for (uint32_t i = 0; i < frameCount; ++i) {
            FrameRingBuffer frame;
            frame.size = ringBufferSize;
            frame.buffer = std::make_unique<VulkanBuffer>(
                device, 
                device.getAllocator(), 
                ringBufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
                VMA_MEMORY_USAGE_AUTO_PREFER_HOST
            );
            // 永続マッピング
            frame.mappedPtr = static_cast<uint8_t*>(frame.buffer->map());
            m_frames.push_back(std::move(frame));
        }
    }

    void ConstantBufferManager::nextFrame() {
        m_currentFrame = (m_currentFrame + 1) % m_frames.size();
        m_frames[m_currentFrame].currentOffset = 0;
    }

    ConstantBufferManager::Allocation ConstantBufferManager::allocateAndWrite(const void* data, uint32_t size) {
        auto& frame = m_frames[m_currentFrame];
        
        uint32_t alignedOffset = alignUp(frame.currentOffset);
        if (alignedOffset + size > frame.size) {
            throw std::runtime_error("Constant buffer ring buffer overflow!");
        }

        std::memcpy(frame.mappedPtr + alignedOffset, data, size);
        
        VkMemoryPropertyFlags memFlags;
        vmaGetAllocationMemoryProperties(m_device.getAllocator(), frame.buffer->getAllocation(), &memFlags);
        // HOST_COHERENT（自動反映）のフラグが「含まれていない」場合だけFlushする
        if ((memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
            vmaFlushAllocation(m_device.getAllocator(), frame.buffer->getAllocation(), alignedOffset, size);
        }

        frame.currentOffset = alignedOffset + size;

        return { frame.buffer->getBindlessIndex(), alignedOffset };
    }
}