#pragma once
#include "RenderGraph.hpp"
#include "VulkanImage.hpp"
#include "VulkanBuffer.hpp"
#include "vulkan/VulkanCommandList.hpp"
#include "vulkan/VulkanComputePipeline.hpp"
#include "VulkanSync.hpp"
#include "VulkanResourceAllocator.hpp"
#include "VulkanConstantBufferManager.hpp"
#include "RHIcommon.hpp"
#include "RHIForward.hpp"
#include <map>
#include <deque>
namespace rhi::vk {
    struct PhysicalNode{
        std::vector<VkImageMemoryBarrier2> imageBarriers;
        std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    };

    class VulkanRenderGraph : public RenderGraph {
    public:
        VulkanRenderGraph(VulkanDevice& device): m_device(device), m_resourceAllocator(device){
            // Todo　漏れがないかチェック
        }    

        PassBuilder& addPass(const std::string& name, const std::string& shaderPath) override;
        ResourceHandle importResource(Resource* res) override;
        ResourceHandle createImage(const ImageDesc& desc) override;
        ResourceHandle createBuffer(const BufferDesc& desc) override;
        uint32_t getPhysicalIndex(ResourceHandle handle) override;

        // バリア決定アルゴリズム
        void compile() override;

        void execute(CommandList& cmd) override;

        DispatchObject& createDispatch(LogicalPass& node, uint32_t x, uint32_t y, uint32_t z);
    private:
        // std::vector<ResourceRegistration> m_resourceRegistry;
        // std::vector<ResourceLifetime> m_resourceLifetimes;
        // std::vector<VulkanCommandList> m_commandLists;

        std::vector<PhysicalNode> m_physicalNodes;
        std::vector<std::unique_ptr<PassBuilder>> m_builders;
        std::vector<std::unique_ptr<DispatchObject>> m_dispatchObjects;
        std::map<std::string, std::unique_ptr<VulkanComputePipeline>> m_pipelines;
        VulkanResourceAllocator m_resourceAllocator;
        VulkanDevice& m_device;
        std::vector<uint32_t> m_sortedIndices;
    };
    
}