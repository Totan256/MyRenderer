#include "VulkanRenderGraph.hpp"

class VulkanPassBuilder : public PassBuilder {
public:
    VulkanPassBuilder(VulkanRenderGraph::PassNode& node) : m_node(node) {}

    void dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        // 実行時に呼ばれるコマンドを保存
        m_node.commands.push_back([x, y, z](VulkanCommandList& cmd) {
            cmd.dispatch(x, y, z); //
        });
    }
private:
    VulkanRenderGraph::PassNode& m_node;
};

PassBuilder& VulkanRenderGraph::addPass(const PassTemplate& proto, const std::vector<VulkanImage*>& resources) {
    PassNode node;
    node.name = proto.getName();
    node.resources = resources;
    node.requirements = proto.getRequirements();
    
    m_nodes.push_back(std::move(node));
    return *new VulkanPassBuilder(m_nodes.back()); // 簡易実装。実際はスマートポインタ等で管理
}

void VulkanRenderGraph::compile() {
    // グラフ全体でのリソースの「最新状態」を追跡するためのテンポラリマップ
    std::map<VulkanImage*, VulkanResourceState> lastStates;

    for (auto& node : m_nodes) {
        node.imageBarriers.clear();

        for (size_t i = 0; i < node.resources.size(); ++i) {
            VulkanImage* img = node.resources[i];
            auto& req = node.requirements[i];

            // 1. このリソースの「直前の状態」を取得
            VulkanResourceState prev;
            if (lastStates.find(img) == lastStates.end()) {
                // グラフ内で初登場なら、Imageが持っている現在の実状態を使う
                prev = MapResourceState(img->getCurrentUsage(), img->getCurrentStage());
            } else {
                prev = lastStates[img];
            }

            // 2. このパスが要求する「次の状態」を取得
            VulkanResourceState next = MapResourceState(req.usage, req.stage);

            // 3. 状態変化（レイアウト変更やアクセス権の変更）が必要かチェック
            if (prev.layout != next.layout || (prev.accessMask & next.accessMask) != next.accessMask) {
                VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                barrier.srcStageMask  = prev.stageMask;
                barrier.srcAccessMask = prev.accessMask;
                barrier.oldLayout     = prev.layout;
                
                barrier.dstStageMask  = next.stageMask;
                barrier.dstAccessMask = next.accessMask;
                barrier.newLayout     = next.layout;
                
                // VulkanImageからハンドル等を取得して設定
                VulkanImage* vImg = static_cast<VulkanImage*>(img);
                barrier.image = vImg->getImage();
                barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

                node.imageBarriers.push_back(barrier);
            }

            // このリソースの最新状態を更新
            lastStates[img] = next;
        }
    }
}

void VulkanRenderGraph::execute(VulkanCommandList& cmd) {
    for (auto& node : m_nodes) {
        // 1. このパスに必要なバリアを一括で発行
        if (!node.imageBarriers.empty()) {
            VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(node.imageBarriers.size());
            depInfo.pImageMemoryBarriers = node.imageBarriers.data();
            
            vkCmdPipelineBarrier2(static_cast<VulkanCommandList&>(cmd).getCommandBuffer(), &depInfo);
        }

        // 2. 実際のコマンドを実行
        for (auto& command : node.commands) {
            command(cmd);
        }
        
        // 3. 実行後、リソースの実状態を更新（次のフレームやグラフ外での利用のため）
        for (size_t i = 0; i < node.resources.size(); ++i) {
            node.resources[i]->setState(node.requirements[i].usage, node.requirements[i].stage);
        }
    }
}
