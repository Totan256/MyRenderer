#pragma once
#include <vector>
#include <memory>
#include "VulkanBuffer.hpp"
#include "VulkanDevice.hpp"

namespace rhi::vk {
    struct FrameRingBuffer {
        std::unique_ptr<VulkanBuffer> buffer;
        uint8_t* mappedPtr = nullptr;
        uint32_t currentOffset = 0;
        uint32_t size = 0;
    };

    class ConstantBufferManager {
    public:
        ConstantBufferManager(VulkanDevice& device, uint32_t ringBufferSize, uint32_t frameCount);
        ~ConstantBufferManager() = default;

        // 次のフレームへ移行（オフセットのリセット）
        void nextFrame();

        // データを書き込み、そのバッファのBindlessIndexとオフセットを返す
        struct Allocation {
            uint32_t index;
            uint32_t offset;
        };
        Allocation allocateAndWrite(const void* data, uint32_t size);

        uint32_t getAlignment() const { return m_alignment; }

    private:
        VulkanDevice& m_device;
        uint32_t m_alignment;
        uint32_t m_ringBufferSize;
        uint32_t m_currentFrame = 0;
        std::vector<FrameRingBuffer> m_frames;

        uint32_t alignUp(uint32_t size) const {
            return (size + m_alignment - 1) & ~(m_alignment - 1);
        }
    };

}