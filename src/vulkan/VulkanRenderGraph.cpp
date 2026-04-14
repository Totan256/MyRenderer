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
#include <stdexcept>

namespace rhi::vk {
    class VulkanDispatchObject : public DispatchObject {
    public:
        VulkanDispatchObject(LogicalPass::DispatchState& ds) : m_ds(&ds) {}
        ~VulkanDispatchObject(){}
        DispatchObject& updateConstantRaw(uint32_t slot, const void* data, size_t size) override{
            uint32_t offset = slot * 4;
            std::memcpy(m_ds->pushData.data() + offset, data, size);
            m_ds->pushDataSize = std::max(m_ds->pushDataSize, static_cast<uint32_t>(offset + size));
            return *this;
        }
        DispatchObject& updateResource(uint32_t slot, ResourceHandle handle) override{
            m_ds->slotValues.at(slot) = handle;
            return *this;
        }
        DispatchObject& updateSize(uint32_t x, uint32_t y, uint32_t z) override {
            m_ds->x = x;
            m_ds->y = y;
            m_ds->z = z;
            return *this;
        }
        LogicalPass::DispatchState* m_ds;
    };


    class VulkanPassBuilder : public PassBuilder {
    public:
        VulkanPassBuilder(VulkanRenderGraph& graph, LogicalPass& node) 
        : m_graph(graph), m_node(node) {}

        PassBuilder& bindPipeline(VulkanComputePipeline& pipeline) {
            m_node.commands.push_back([&pipeline](VulkanCommandList& cmd) {
                cmd.bindPipeline(pipeline);
                cmd.bindGlobalDescriptorSet();
            });
            return *this;
        }

        PassBuilder& setConstantRaw(uint32_t slot, const void* data, size_t size) override {
            uint32_t offset = slot * 4;
            if (offset + size > MAX_PUSH_CONSTANT_SIZE) {
                throw std::runtime_error("Push constants limit exceeded!");
            }
            // データを直接コピー
            std::memcpy(m_node.pushData.data() + offset, data, size);
            // 使用サイズを更新
            m_node.pushDataSize = std::max(m_node.pushDataSize, static_cast<uint32_t>(offset + size));
            return *this;
        }

        PassBuilder& setResource(uint32_t slot, ResourceHandle handle) override {
            m_node.slotValues[slot] = handle;
            return *this;
        }

        VulkanDispatchObject& dispatch(uint32_t x, uint32_t y, uint32_t z) override {
            uint32_t id = m_node.nextDispatchId++;
            // 1. 現在の builder/node の状態をスナップショットとして保存 (Capture by Value)
            LogicalPass::DispatchState& ds = m_node.dispatchStates[id];
            ds.x = x; ds.y = y; ds.z = z;
            ds.pushData = m_node.pushData;         // 配列のコピー
            ds.pushDataSize = m_node.pushDataSize;
            ds.slotValues = m_node.slotValues;     // Mapのコピー
            m_dispatchObjects.emplace_back(ds);
            // 2. 実行コマンドを登録。ラムダには ID のみをキャプチャさせる
            m_node.commands.push_back([&node = m_node, &graph = m_graph, id](VulkanCommandList& cmd) {
                // 実行時に最新のスナップショットを取得
                auto& state = node.dispatchStates.at(id);
                for (auto const& [slot, handle] : state.slotValues) {
                    uint32_t bindlessIndex = graph.getPhysicalIndex(handle);
                    uint32_t offset = slot * 4;
                    if (offset + 4 <= MAX_PUSH_CONSTANT_SIZE) {
                        std::memcpy(state.pushData.data() + offset, &bindlessIndex, 4);
                        state.pushDataSize = std::max(state.pushDataSize, offset + 4);
                    }
                }
                // この dispatch 専用のプッシュ定数を送信
                if (state.pushDataSize > 0) {
                    cmd.setPushData(0, state.pushDataSize, state.pushData.data());
                }
                cmd.dispatch(state.x, state.y, state.z);
            });
            return m_dispatchObjects.back();
        }
        
    private:
        VulkanRenderGraph& m_graph;
        LogicalPass& m_node;
        std::deque<VulkanDispatchObject> m_dispatchObjects;
    };

    
    ResourceHandle VulkanRenderGraph::importResource(Resource* res) {
        ResourceHandle handle = static_cast<ResourceHandle>(m_resourceRegistry.size());
        
        ResourceRegistration reg{};
        reg.isImported = true;
        reg.physicalResource = res; // 外部から渡された実リソース
        
        m_resourceRegistry.push_back(reg);
        return handle;
    }

    ResourceHandle VulkanRenderGraph::createImage(const ImageDesc& desc) {
        ResourceHandle handle = static_cast<ResourceHandle>(m_resourceRegistry.size());
        ResourceRegistration reg{};
        reg.isImported = false;
        reg.desc = desc;
        m_resourceRegistry.push_back(reg);
        return handle;
    }

    ResourceHandle VulkanRenderGraph::createBuffer(const BufferDesc& desc) {
        ResourceHandle handle = static_cast<ResourceHandle>(m_resourceRegistry.size());
        
        ResourceRegistration reg{};
        reg.isImported = false;
        reg.desc = desc;
        m_resourceRegistry.push_back(reg);
        return handle;
    }

    uint32_t VulkanRenderGraph::getPhysicalIndex(ResourceHandle handle) {
        // 1. ハンドルが有効かチェック
        if (handle == InvalidResource || handle >= m_resourceRegistry.size()) {
            return 0; // またはエラーを示す値
        }
        // 2. リソースレジストリからリソースの種類（Image or Buffer）を確認
        const auto& reg = m_resourceRegistry[handle];
        bool isImg = reg.isImported ? reg.physicalResource->isImage() : reg.isImage();
        // 3. Allocator から物理リソースへのポインタを取得し、BindlessIndex を返す
        if (isImg) {
            VulkanImage* physImg = m_resourceAllocator.getPhysicalImage(handle);
            return physImg ? physImg->getBindlessIndex() : 0; //
        } else {
            VulkanBuffer* physBuf = m_resourceAllocator.getPhysicalBuffer(handle);
            return physBuf ? physBuf->getBindlessIndex() : 0; //
        }
    }

    PassBuilder& VulkanRenderGraph::addPass(const PassTemplate& proto, const std::vector<ResourceHandle>& resources) {
        uint32_t passIndex = static_cast<uint32_t>(m_logicalNodes.size());
        m_logicalNodes.emplace_back();
        auto& node = m_logicalNodes.back();

        node.name = proto.getName();
        node.resourceHandles = resources;
        node.requirements = proto.getRequirements();

        for (size_t i = 0; i < resources.size(); ++i) {
            ResourceHandle h = resources[i];
            auto& req = proto.getRequirements()[i]; // ReadかWriteかの情報
            if (isWriteUsage(req.usage)) {
                m_resourceRegistry[h].producers.push_back(passIndex);
            } else {
                m_resourceRegistry[h].consumers.push_back(passIndex);
            }
        }
        
        auto builder = std::make_unique<VulkanPassBuilder>(*this, node);
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
        auto& physiaclNode = m_physicalNodes[passIdx];
        physiaclNode.imageBarriers.clear();
        physiaclNode.bufferBarriers.clear();
        for (size_t i = 0; i < logicalNode.resourceHandles.size(); ++i) {
            rhi::ResourceHandle h = logicalNode.resourceHandles[i];
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
            auto& logicalNode = m_logicalNodes[m_sortedIndices[i]];
            auto& physiaclNode = m_physicalNodes[m_sortedIndices[i]];
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