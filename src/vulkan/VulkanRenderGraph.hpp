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

        PassBuilder& addPass(const PassTemplate& proto, const std::vector<ResourceHandle>& resources) override;
        ResourceHandle importResource(Resource* res) override;
        ResourceHandle createImage(const ImageDesc& desc) override;
        ResourceHandle createBuffer(const BufferDesc& desc) override;
        uint32_t getPhysicalIndex(ResourceHandle handle) override;

        // バリア決定アルゴリズム
        void compile() override;

        void execute(CommandList& cmd) override;

    private:
        std::vector<ResourceRegistration> m_resourceRegistry;
        std::vector<ResourceLifetime> m_resourceLifetimes;
        
        std::vector<std::unique_ptr<PassBuilder>> m_builders;
        std::vector<VulkanCommandList> m_commandLists;
        std::vector<PhysicalNode> m_physicalNodes;

        VulkanResourceAllocator m_resourceAllocator;

        VulkanDevice& m_device;
        
        std::vector<uint32_t> m_sortedIndices;
    };
    
}