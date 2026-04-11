#include "VulkanRenderGraph.hpp"
#include "VulkanCommandList.hpp"
#include "VulkanComputePipeline.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanImage.hpp"
#include "RHIcommon.hpp"
#include "RHIForward.hpp"
#include "VulkanResourceAllocator.hpp"
#include <map>
#include <queue>
#include <vector>
#include <set>
#include <algorithm>
#include <numeric>

namespace rhi::vk {
    class VulkanPassBuilder : public PassBuilder {
    public:
        VulkanPassBuilder(LogicalPass& node) : m_node(node) {}

        PassBuilder& bindPipeline(VulkanComputePipeline& pipeline) {
            m_node.commands.push_back([&pipeline](VulkanCommandList& cmd) {
                cmd.bindPipeline(pipeline);
                cmd.bindGlobalDescriptorSet();
            });
            return *this;
        }

        PassBuilder& setPushResource(uint32_t offset, const Buffer& resource) override {
            uint32_t index = static_cast<const VulkanBuffer&>(resource).getBindlessIndex();
            m_node.commands.push_back([offset, index](VulkanCommandList& cmd) {
                cmd.setPushData(offset, sizeof(uint32_t), &index);
            });
            return *this;
        }

        PassBuilder& setPushResource(uint32_t offset, const Image& resource) override {
            uint32_t index = static_cast<const VulkanImage&>(resource).getBindlessIndex();
            m_node.commands.push_back([offset, index](VulkanCommandList& cmd) {
                cmd.setPushData(offset, sizeof(uint32_t), &index);
            });
            return *this;
        }

        PassBuilder& dispatch(uint32_t x, uint32_t y, uint32_t z) override {
            // 実行時に呼ばれるコマンドを保存
            m_node.commands.push_back([x, y, z](VulkanCommandList& cmd) {
                cmd.dispatch(x, y, z); //
            });
            return *this;
        }
    private:
        LogicalPass& m_node;
    };

    PassBuilder& VulkanRenderGraph::addPass(const PassTemplate& proto, const std::vector<ResourceHandle>& resources) {
        uint32_t passIndex = static_cast<uint32_t>(m_logicalNodes.size());
        m_logicalNodes.emplace_back();
        auto& node = m_logicalNodes.back();

        node.name = proto.getName();
        node.resources = resources;
        node.requirements = proto.getRequirements();

        for (size_t i = 0; i < resources.size(); ++i) {
            ResourceHandle h = resources[i];
            auto& req = proto.getRequirements()[i]; // ReadかWriteかの情報
            // 書き込み（StorageWrite, TransferDstなど）なら Producer
            if (isWriteUsage(req.usage)) {
                m_resourceRegistry[h].producers.push_back(passIndex);
            } else {
                // 読み込みなら Consumer
                m_resourceRegistry[h].consumers.push_back(passIndex);
            }
        }
        
        auto builder = std::make_unique<VulkanPassBuilder>(node);
        m_builders.push_back(std::move(builder));
        return *m_builders.back();
    }





    // src/vulkan/VulkanRenderGraph.cpp

void VulkanRenderGraph::compile() {
    // 1. 全パスのインデックスリストを作成
    std::vector<uint32_t> passIndices(m_logicalNodes.size());
    std::iota(passIndices.begin(), passIndices.end(), 0);
    // 2. パスソート (以前実装したトポロジカルソート)
    std::vector<uint32_t> sortedIndices = getSortPasses(passIndices);
    // 3. ライフタイムの計算
    calculateLifetimes(sortedIndices);
    // 4. 物理リソースの割り当て (VulkanResourceAllocatorを使用)
    m_resourceAllocator.allocate(m_resourceRegistry, m_resourceLifetimes);
    // 5. バリアとレイアウト遷移の生成
    // 各リソースの「現在の状態」を追跡するためのマップ
    std::map<rhi::ResourceHandle, VulkanResourceState> currentStates;

    for (uint32_t passIdx : sortedIndices) {
        auto& logicalNode = m_logicalNodes[passIdx];
        auto& physiaclNode = m_physiaclNodes[passIdx];
        physiaclNode.imageBarriers.clear();
        physiaclNode.bufferBarriers.clear();
        for (size_t i = 0; i < logicalNode.resources.size(); ++i) {
            rhi::ResourceHandle h = logicalNode.resources[i];
            const auto& req = logicalNode.requirements[i];
            // 要求される次の状態
            VulkanResourceState next = MapResourceState(req.usage, req.stage);
            // 初登場のリソースは Undefined 状態から開始
            VulkanResourceState prev = currentStates.count(h) ? currentStates[h] : 
                VulkanResourceState{ VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED };
            // 同期が必要（レイアウトが違う、または書き込みが含まれる）かチェック
            if (prev.layout != next.layout || (next.accessMask & VK_ACCESS_2_SHADER_WRITE_BIT)) {
                if (m_resourceRegistry[h].isImage()) {
                    VkImageMemoryBarrier2 imgBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                    imgBarrier.srcStageMask  = prev.stageMask;
                    imgBarrier.srcAccessMask = prev.accessMask;
                    imgBarrier.dstStageMask  = next.stageMask;
                    imgBarrier.dstAccessMask = next.accessMask;
                    imgBarrier.oldLayout     = prev.layout;
                    imgBarrier.newLayout     = next.layout;
                    imgBarrier.image         = m_resourceAllocator.getPhysicalImage(h)->getImage();
                    imgBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    physiaclNode.imageBarriers.push_back(imgBarrier);
                } else {
                    VkBufferMemoryBarrier2 bufBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                    bufBarrier.srcStageMask  = prev.stageMask;
                    bufBarrier.srcAccessMask = prev.accessMask;
                    bufBarrier.dstStageMask  = next.stageMask;
                    bufBarrier.dstAccessMask = next.accessMask;
                    bufBarrier.buffer        = m_resourceAllocator.getPhysicalBuffer(h)->getNativeBuffer();
                    bufBarrier.offset        = 0;
                    bufBarrier.size          = VK_WHOLE_SIZE;
                    physiaclNode.bufferBarriers.push_back(bufBarrier);
                }
            }
            // 状態を更新
            currentStates[h] = next;
        }
    }
    // ソートされた順序にノードを並び替える（または実行時に順序を参照する）
    m_sortedIndices = sortedIndices;
}

    void VulkanRenderGraph::execute(CommandList& cmd) {
        for (size_t i = 0; i < m_logicalNodes.size(); ++i) {
            // 1. このパスに必要なバリアを一括で発行
            auto& logicalNode = m_logicalNodes[i];
            auto& physiaclNode = m_physiaclNodes[i];
            if (!physiaclNode.imageBarriers.empty() || !physiaclNode.bufferBarriers.empty()) {
                VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                depInfo.imageMemoryBarrierCount = (uint32_t)physiaclNode.imageBarriers.size();
                depInfo.pImageMemoryBarriers    = physiaclNode.imageBarriers.data();
                depInfo.bufferMemoryBarrierCount = (uint32_t)physiaclNode.bufferBarriers.size();
                depInfo.pBufferMemoryBarriers    = physiaclNode.bufferBarriers.data();
                
                vkCmdPipelineBarrier2(cmd.getCommandBuffer(), &depInfo);
            }

            // 2. 実際のコマンドを実行
            for (auto& command : logicalNode.commands) {
                command(cmd);
            }
            
            // 3. 実行後、リソースの実状態を更新（次のフレームやグラフ外での利用のため）
            // for (size_t i = 0; i < logicalNode.resources.size(); ++i) {
            //     if (auto* res = logicalNode.requirements[i]) {
            //         res->setState(logicalNode.requirements[i].usage, logicalNode.requirements[i].stage);
            //     }
            // }
        }
    }
}