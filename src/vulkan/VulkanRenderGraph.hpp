#pragma once
#include "RenderGraph.hpp"
#include "VulkanImage.hpp"
#include "VulkanBuffer.hpp"
#include "vulkan/VulkanCommandList.hpp"
#include "vulkan/VulkanComputePipeline.hpp"
#include "vulkan/VulkanGraphicsPipeline.hpp"
#include "VulkanSync.hpp"
#include "VulkanResourceAllocator.hpp"
#include "VulkanConstantBufferManager.hpp"
#include "RHIcommon.hpp"
#include "RHIForward.hpp"
#include "rhi/CommandList.hpp"
#include <map>
#include <deque>
namespace rhi::vk {
    struct PhysicalNode{
        std::vector<VkImageMemoryBarrier2> imageBarriers;
        std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    };

    struct RenderBatch {
        QueueType queueType;
        std::vector<uint32_t> passIndices;
        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags> waitStages;
        std::vector<VkSemaphore> signalSemaphores;
        std::unique_ptr<VulkanCommandList> cmdList;
    };

    class VulkanRenderGraph : public RenderGraph {
    public:
        VulkanRenderGraph(VulkanDevice& device): m_device(device), m_resourceAllocator(device){
        }
        ~VulkanRenderGraph() {
            clearBatchSemaphores();
        }

        PassBuilder& addPass(const std::string& name, const std::string& shaderPath, QueueType queueType = QueueType::Compute) override;
        PassBuilder& addGraphicsPass(const std::string& name, const std::string& vertShaderPath, const std::string& fragShaderPath) override;
        ResourceHandle importResource(Resource* res, StringHash nameHash = 0) override;
        ResourceHandle createImage(const ImageDesc& desc, StringHash nameHash = 0) override;
        ResourceHandle createBuffer(const BufferDesc& desc, StringHash nameHash = 0) override;
        uint32_t getPhysicalIndex(ResourceHandle handle) override;
        const ResourceRegistration& getRegistration(ResourceHandle handle) const { return m_resourceRegistry[handle];}

        void addCopyPass(const std::string& name, ResourceHandle srcBuffer, ResourceHandle dstBuffer, size_t size, QueueType queueType) override;

        // バリア決定アルゴリズム
        void compile() override;

        void execute(const std::vector<SemaphoreHandle>& waitSemaphores) override;

        DispatchObject& createDispatch(LogicalPass& node, uint32_t x, uint32_t y, uint32_t z);
    private:
        std::vector<PhysicalNode> m_physicalNodes;
        std::vector<std::unique_ptr<PassBuilder>> m_builders;
        std::vector<std::unique_ptr<DispatchObject>> m_dispatchObjects;
        std::map<std::string, std::unique_ptr<VulkanComputePipeline>> m_pipelines;
        std::map<std::string, std::unique_ptr<VulkanGraphicsPipeline>> m_graphicsPipelines;
        VulkanResourceAllocator m_resourceAllocator;
        VulkanDevice& m_device;
        std::vector<uint32_t> m_sortedIndices;
        // ハンドル逆引き用
        std::map<Resource*, ResourceHandle> m_physicalToHandle;

        std::vector<RenderBatch> m_batches;
        std::vector<VkSemaphore> m_batchSemaphores;

        void clearBatchSemaphores() {
            for (VkSemaphore sem : m_batchSemaphores) {
                m_device.destroySemaphore(sem);
            }
            m_batchSemaphores.clear();
        }
    };
    
}