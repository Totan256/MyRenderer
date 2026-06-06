#pragma once
#include "RenderGraph.hpp"
#include "VulkanImage.hpp"
#include "VulkanBuffer.hpp"
#include "vulkan/VulkanCommandList.hpp"
#include "VulkanSync.hpp"
#include "rhi/Swapchain.hpp"
#include "VulkanResourceAllocator.hpp"
#include <map>

namespace rhi::vk {
    struct RenderBatch {
        QueueType queueType;
        std::vector<uint32_t> passIndices;
        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags> waitStages;
        std::vector<VkSemaphore> signalSemaphores;
        std::unique_ptr<VulkanCommandList> cmdList;
        
        std::vector<VkImageMemoryBarrier2> imageBarriers;
        std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    };

    struct SwapchainSync {
        rhi::Swapchain* swapchain;
        uint32_t firstBatchIdx;
        uint32_t lastBatchIdx;
    };

    class VulkanRenderGraph : public RenderGraph {
    public:
        VulkanRenderGraph(VulkanDevice& device) : m_device(device), m_resourceAllocator(device) {}
        ~VulkanRenderGraph() override;

        ComputePass& addComputePass(const std::string& name, const std::string& shaderPath, QueueType queueType = QueueType::Compute) override;
        GraphicsPass& addGraphicsPass(const std::string& name, const std::string& vertShaderPath, const std::string& fragShaderPath) override;
        CopyPass& addCopyPass(const std::string& name, ResourceHandle srcBuffer, ResourceHandle dstBuffer, size_t size, QueueType queueType = QueueType::Transfer) override;

        ResourceHandle importResource(Resource* res, StringHash nameHash = {0}) override;
        ResourceHandle createImage(const ImageDesc& desc, StringHash nameHash = {0}) override;
        ResourceHandle createBuffer(const BufferDesc& desc, StringHash nameHash = {0}) override;
        
        const ResourceRegistration& getRegistration(ResourceHandle handle) const override { return m_resourceRegistry[handle]; }
        uint32_t getPhysicalIndex(ResourceHandle handle) override;
        Device& getDevice() override { return m_device; }
        VulkanResourceAllocator& getAllocator() { return m_resourceAllocator; }

        void compile() override;
        void execute(const std::vector<SemaphoreHandle>& waitSemaphores) override;

    private:
        struct SwapchainSync {
            rhi::Swapchain* swapchain;
            uint32_t firstBatchIdx;
            uint32_t lastBatchIdx;
        };
        VulkanDevice& m_device;
        VulkanResourceAllocator m_resourceAllocator;
        std::vector<uint32_t> m_sortedIndices;
        std::map<Resource*, ResourceHandle> m_physicalToHandle;

        std::vector<RenderBatch> m_batches;
        std::vector<VkSemaphore> m_batchSemaphores;
        std::vector<SwapchainSync> m_swapchainSyncs;

        void clearBatchSemaphores();
    };
}