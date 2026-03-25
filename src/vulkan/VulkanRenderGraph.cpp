#include "VulkanRenderGraph.hpp"

class VulkanPassBuilder : public PassBuilder {
public:
    VulkanPassBuilder(VulkanRenderGraph::PassNode& node) : m_node(node) {}

    VulkanPassBuilder& bindPipeline(VulkanComputePipeline& pipeline) {
        m_node.commands.push_back([&pipeline](VulkanCommandList& cmd) {
            cmd.bindPipeline(pipeline);
            cmd.bindGlobalDescriptorSet();
        });
        return *this;
    }

    VulkanPassBuilder& setPushData(uint32_t offset, uint32_t size, const void* data) override {
        // Push Constants のコピーを保持（実行時にデータが消えているのを防ぐため）
        std::vector<uint8_t> blob((uint8_t*)data, (uint8_t*)data + size);
        m_node.commands.push_back([offset, size, blob](VulkanCommandList& cmd) {
            cmd.setPushData(offset, size, blob.data());
        });
        return *this;
    }

    VulkanPassBuilder& setPushResource(uint32_t offset, const rhi::Buffer& resource) override {
        m_node.commands.push_back([offset, &resource](VulkanCommandList& cmd) {
            cmd.setPushResource(offset, resource);
        });
        return *this;
    }

    VulkanPassBuilder& setPushResource(uint32_t offset, const rhi::Image& resource) override {
        m_node.commands.push_back([offset, &resource](VulkanCommandList& cmd) {
            cmd.setPushResource(offset, resource);
        });
        return *this;
    }

    VulkanPassBuilder& dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        // 実行時に呼ばれるコマンドを保存
        m_node.commands.push_back([x, y, z](VulkanCommandList& cmd) {
            cmd.dispatch(x, y, z); //
        });
        return *this;
    }
private:
    VulkanRenderGraph::PassNode& m_node;
};

PassBuilder& VulkanRenderGraph::addPass(const PassTemplate& proto, const std::vector<rhi::Resource*>& resources) {
    m_nodes.emplace_back();
    auto& node = m_nodes.back();
    node.name = proto.getName();
    node.resources = resources;
    node.requirements = proto.getRequirements();
    
    auto builder = std::make_unique<VulkanPassBuilder>(node);
    m_builders.push_back(std::move(builder));
    return *m_builders.back();
}

void VulkanRenderGraph::compile() {
    // グラフ全体でのリソースの「最新状態」を追跡するためのテンポラリマップ
    std::map<rhi::Resource*, VulkanResourceState> lastStates;

    for (auto& node : m_nodes) {
        node.imageBarriers.clear();
        node.bufferBarriers.clear();
        for (size_t i = 0; i < node.resources.size(); ++i) {
            rhi::Resource* res = node.resources[i];
            auto& req = node.requirements[i];

            VulkanResourceState prev = lastStates.contains(res) ? 
                lastStates[res] : MapResourceState(res->getCurrentUsage(), res->getCurrentStage());
            VulkanResourceState next = MapResourceState(req.usage, req.stage);

            // 同期が必要かチェック
            if (prev.layout != next.layout || (prev.accessMask & next.accessMask) != next.accessMask) {
                if (res->isImage()) {
                    // --- Image Barrier ---
                    VkImageMemoryBarrier2 imgBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                    imgBarrier.srcStageMask  = prev.stageMask;
                    imgBarrier.srcAccessMask = prev.accessMask;
                    imgBarrier.oldLayout     = prev.layout;
                    imgBarrier.dstStageMask  = next.stageMask;
                    imgBarrier.dstAccessMask = next.accessMask;
                    imgBarrier.newLayout     = next.layout;
                    imgBarrier.image         = static_cast<VulkanImage*>(res)->getImage();
                    imgBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    node.imageBarriers.push_back(imgBarrier);
                } else {
                    // --- Buffer Barrier ---
                    VkBufferMemoryBarrier2 bufBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
                    bufBarrier.srcStageMask  = prev.stageMask;
                    bufBarrier.srcAccessMask = prev.accessMask;
                    bufBarrier.dstStageMask  = next.stageMask;
                    bufBarrier.dstAccessMask = next.accessMask;
                    // バッファにレイアウトは存在しないため無視
                    bufBarrier.buffer = static_cast<VulkanBuffer*>(res)->getNativeBuffer();
                    bufBarrier.offset = 0;
                    bufBarrier.size   = VK_WHOLE_SIZE; // 全域を対象
                    node.bufferBarriers.push_back(bufBarrier);
                }
            }
            lastStates[res] = next;
        }
    }
}

void VulkanRenderGraph::execute(VulkanCommandList& cmd) {
    for (auto& node : m_nodes) {
        // 1. このパスに必要なバリアを一括で発行
        if (!node.imageBarriers.empty() || !node.bufferBarriers.empty()) {
            VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depInfo.imageMemoryBarrierCount = (uint32_t)node.imageBarriers.size();
            depInfo.pImageMemoryBarriers    = node.imageBarriers.data();
            depInfo.bufferMemoryBarrierCount = (uint32_t)node.bufferBarriers.size();
            depInfo.pBufferMemoryBarriers    = node.bufferBarriers.data();
            
            vkCmdPipelineBarrier2(cmd.getCommandBuffer(), &depInfo);
        }

        // 2. 実際のコマンドを実行
        for (auto& command : node.commands) {
            command(cmd);
        }
        
        // 3. 実行後、リソースの実状態を更新（次のフレームやグラフ外での利用のため）
        for (size_t i = 0; i < node.resources.size(); ++i) {
            if (auto* res = node.resources[i]) {
                res->setState(node.requirements[i].usage, node.requirements[i].stage);
            }
        }
    }
}
