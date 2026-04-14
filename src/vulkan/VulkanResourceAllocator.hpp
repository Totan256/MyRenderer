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
        VulkanResourceAllocator(VulkanDevice& device) : m_device(device) {}

        // compile() から呼ばれるメインの割り当て関数
        void allocate(
            const std::vector<ResourceRegistration>& registry,
            const std::vector<ResourceLifetime>& lifetimes);

        // ハンドルから物理リソースを取得
        VulkanImage* getPhysicalImage(ResourceHandle h) { return m_imageMap[h]; }
        VulkanBuffer* getPhysicalBuffer(ResourceHandle h) { return m_bufferMap[h]; }

    private:
        VulkanDevice& m_device;
        
        // ハンドルと物理リソースの対応表
        std::map<ResourceHandle, VulkanImage*> m_imageMap;
        std::map<ResourceHandle, VulkanBuffer*> m_bufferMap;

        // 再利用のためのプール
        std::vector<ReusableImage> m_imagePool;
        std::vector<ReusableBuffer> m_bufferPool;

    };
}