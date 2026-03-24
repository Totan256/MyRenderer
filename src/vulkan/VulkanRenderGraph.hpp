#pragma once
#include "RenderGraph.hpp"
#include "VulkanImage.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanSync.hpp"
#include "RHIcommon.hpp"
#include <map>

class VulkanRenderGraph : public RenderGraph {
public:
    struct PassNode {
        std::string name;
        std::vector<VulkanImage*> resources;
        std::vector<rhi::ResourceRequirement> requirements;
        // compile() で計算されたバリアを保持
        std::vector<VkImageMemoryBarrier2> imageBarriers;
        // 実行すべきコマンド（ラムダ等で保存）
        std::vector<std::function<void(VulkanCommandList&)>> commands;
    };

    PassBuilder& addPass(const PassTemplate& proto, const std::vector<VulkanImage*>& resources) override;

    // バリア決定アルゴリズム
    void compile() override;

    void execute(VulkanCommandList& cmd) override;

private:
    std::vector<PassNode> m_nodes;
    std::vector<VulkanCommandList> m_commandLists;
};
    