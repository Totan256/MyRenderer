#pragma once
#include <memory>
#include "RHIForward.hpp"
#include "RenderGraph.hpp"
#include <vector>
#include <map>

namespace rhi::vk {
    // 物理リソースとその「最後に解放された時間」をセットで管理
    struct ReusableImage {
        std::unique_ptr<VulkanImage> image;
        uint32_t lastUsedPass = 0xFFFFFFFF;
    };

    struct ReusableBuffer {
        std::unique_ptr<VulkanBuffer> buffer;
        uint32_t lastUsedPass = 0xFFFFFFFF;
    };

    class VulkanResourceAllocator {
    public:
        VulkanResourceAllocator(VulkanDevice& device, uint32_t framesInFlight);

        // compile() から呼ばれるメインの割り当て関数
        void allocate(
            uint64_t currentFrameIndex,
            const std::vector<ResourceRegistration>& registry,
            const std::vector<ResourceLifetime>& lifetimes);

        // ハンドルから物理リソースを取得
        VulkanImage* getPhysicalImage(ResourceHandle h) { return m_imageMap[h]; }
        VulkanBuffer* getPhysicalBuffer(ResourceHandle h) { return m_bufferMap[h]; }

    private:
        VulkanDevice& m_device;
        uint32_t m_framesInFlight;
        // ハンドルと物理リソースの対応表
        std::map<ResourceHandle, VulkanImage*> m_imageMap;
        std::map<ResourceHandle, VulkanBuffer*> m_bufferMap;

        // 再利用のためのプール
        std::vector<std::vector<ReusableImage>> m_imagePools;
        std::vector<std::vector<ReusableBuffer>> m_bufferPools;

    };
}