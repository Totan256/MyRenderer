#pragma once
#include "RenderGraph.hpp"
#include "VulkanImage.hpp"
#include "VulkanBuffer.hpp"
#include "vulkan/VulkanCommandList.hpp"
#include "vulkan/VulkanComputePipeline.hpp"
#include "VulkanSync.hpp"
#include "VulkanResourceAllocator.hpp"
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
        

        PassBuilder& addPass(const PassTemplate& proto, const std::vector<ResourceHandle>& resources) override;

        // バリア決定アルゴリズム
        void compile() override;

        void execute(CommandList& cmd) override;

    private:
        std::vector<ResourceRegistration> m_resourceRegistry;
        std::vector<ResourceLifetime> m_resourceLifetimes;
        
        std::vector<std::unique_ptr<PassBuilder>> m_builders;
        std::vector<VulkanCommandList> m_commandLists;
        std::vector<PhysicalNode> m_physiaclNodes;

        VulkanResourceAllocator m_resourceAllocator;

        
        std::vector<uint32_t> m_sortedIndices;
    };
    
}