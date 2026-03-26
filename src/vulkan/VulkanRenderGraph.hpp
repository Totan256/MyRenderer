#pragma once
#include "RenderGraph.hpp"
#include "VulkanImage.hpp"
#include "VulkanBuffer.hpp"
#include "vulkan/VulkanCommandList.hpp"
#include "vulkan/VulkanComputePipeline.hpp"
#include "VulkanSync.hpp"
#include "RHIcommon.hpp"
#include "RHIForward.hpp"
#include <map>
#include <deque>

class VulkanRenderGraph : public RenderGraph {
public:
    struct PassNode {
        std::string name;
        std::vector<rhi::Resource*> resources;
        std::vector<rhi::ResourceRequirement> requirements;
        // compile() で計算されたバリアを保持
        std::vector<VkImageMemoryBarrier2> imageBarriers;
        std::vector<VkBufferMemoryBarrier2> bufferBarriers;
        // 実行すべきコマンド（ラムダ等で保存）
        std::vector<std::function<void(VulkanCommandList&)>> commands;
    };

    PassBuilder& addPass(const PassTemplate& proto, const std::vector<rhi::Resource*>& resources) override;

    // バリア決定アルゴリズム
    void compile() override;

    void execute(rhi::CommandList& cmd) override;

private:
    std::deque<PassNode> m_nodes; 
    std::vector<std::unique_ptr<PassBuilder>> m_builders;
    std::vector<VulkanCommandList> m_commandLists;
};
    