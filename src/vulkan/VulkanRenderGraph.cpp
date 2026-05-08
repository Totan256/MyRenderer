#include "VulkanRenderGraph.hpp"
#include "VulkanCommandList.hpp"
#include "VulkanComputePipeline.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanImage.hpp"
#include "RHIcommon.hpp"
#include "RHIForward.hpp"
#include "VulkanResourceAllocator.hpp"
#include "VulkanConstantBufferManager.hpp"
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
        VulkanDispatchObject(LogicalPass& node, LogicalPass::DispatchState& ds, VulkanDevice& device) 
            : m_node(node), m_ds(ds), m_device(device) {}
        ~VulkanDispatchObject(){}
        DispatchObject& updateConstantRaw(uint32_t offset, const void* data, size_t size) override{
            if (offset + size > MAX_PUSH_CONSTANT_SIZE) throw std::runtime_error("Push constants limit exceeded!");
            std::memcpy(m_ds.pushData.data() + offset, data, size);
            m_ds.pushDataSize = std::max(m_ds.pushDataSize, static_cast<uint32_t>(offset + size));
            return *this;
        }
        DispatchObject& updateResource(uint32_t offset, ResourceHandle handle) override{
            // 簡易バリデーション: パスのbind()で宣言されていないオフセットへのセットを弾く
            if (m_node.signature.find(offset) == m_node.signature.end()) {
                throw std::runtime_error("Cannot set resource: offset " + std::to_string(offset) + 
                                         " is not defined in the pass signature. Did you forget to bind() it?");
            }
            // ToDo: 将来的には m_node.signature[offset] (要求Usage) と registry[handle] でisCompatibleによる互換性チェックを追加
            m_ds.resourceOffsets[offset] = handle;
            return *this;
        }
        DispatchObject& updateSize(uint32_t x, uint32_t y, uint32_t z) override {
            m_ds.x = x;
            m_ds.y = y;
            m_ds.z = z;
            return *this;
        }
    private:
        LogicalPass& m_node;
        LogicalPass::DispatchState& m_ds;
        VulkanDevice& m_device;
    };

    
    class VulkanPassBuilder : public PassBuilder {
    public:
        VulkanPassBuilder(VulkanRenderGraph& graph, LogicalPass& node) : m_graph(graph), m_node(node) {}

        PassBuilder& bind(const BindGroup& desc) override {
            for(const auto& entry : desc.requirements) {
                m_node.signature[entry.offset] = entry.usage;
            }
            return *this;
        }
        PassBuilder& bind(uint32_t offset, ResourceUsage usage) override {
            m_node.signature[offset] = usage;
            return *this;
        }
        DispatchObject& dispatch(uint32_t x, uint32_t y, uint32_t z) override {
            return m_graph.createDispatch(m_node, x, y, z);
        }
        
    private:
        VulkanRenderGraph& m_graph;
        LogicalPass& m_node;
    };

    DispatchObject& VulkanRenderGraph::createDispatch(LogicalPass& node, uint32_t x, uint32_t y, uint32_t z) {
        node.dispatchStates.push_back({});
        auto& ds = node.dispatchStates.back();
        ds.id = static_cast<uint32_t>(node.dispatchStates.size() - 1);
        ds.x = x; ds.y = y; ds.z = z;
        
        m_dispatchObjects.push_back(std::make_unique<VulkanDispatchObject>(node, ds, m_device));
        return *m_dispatchObjects.back();
    }
    PassBuilder& VulkanRenderGraph::addPass(const std::string& name, const std::string& shaderPath) {
        m_logicalNodes.emplace_back();
        auto& node = m_logicalNodes.back();
        node.name = name;
        node.shaderPath = shaderPath;
        
        m_builders.push_back(std::make_unique<VulkanPassBuilder>(*this, node));
        return *m_builders.back();
    }
    ResourceHandle VulkanRenderGraph::importResource(Resource* res) {// todo リソースをdescから作れるようにするmust
        ResourceHandle handle = static_cast<ResourceHandle>(m_resourceRegistry.size());
        ResourceRegistration reg{}; reg.isImported = true; reg.physicalResource = res;
        m_resourceRegistry.push_back(reg); return handle;
    }
    ResourceHandle VulkanRenderGraph::createImage(const ImageDesc& desc) {
        ResourceHandle handle = static_cast<ResourceHandle>(m_resourceRegistry.size());
        ResourceRegistration reg{}; reg.isImported = false; reg.desc = desc;
        m_resourceRegistry.push_back(reg); return handle;
    }
    ResourceHandle VulkanRenderGraph::createBuffer(const BufferDesc& desc) {
        ResourceHandle handle = static_cast<ResourceHandle>(m_resourceRegistry.size());
        ResourceRegistration reg{}; reg.isImported = false; reg.desc = desc;
        m_resourceRegistry.push_back(reg); return handle;
    }
    uint32_t VulkanRenderGraph::getPhysicalIndex(ResourceHandle handle) {
        if (handle == InvalidResource || handle >= m_resourceRegistry.size()) return 0;
        const auto& reg = m_resourceRegistry[handle];
        bool isImg = reg.isImported ? reg.physicalResource->isImage() : reg.isImage();
        if (isImg) {
            VulkanImage* physImg = m_resourceAllocator.getPhysicalImage(handle);
            return physImg ? physImg->getBindlessIndex() : 0;
        } else {
            VulkanBuffer* physBuf = m_resourceAllocator.getPhysicalBuffer(handle);
            return physBuf ? physBuf->getBindlessIndex() : 0;
        }
    }
    void VulkanRenderGraph::compile() {
        std::cout << "Compiling RenderGraph..." << std::endl;//debug
        // 1. 各パスの要件（requirements）をディスパッチから遅延集計する
        for (size_t passIndex = 0; passIndex < m_logicalNodes.size(); ++passIndex) {
            auto& node = m_logicalNodes[passIndex];
            
            // パイプラインの作成・キャッシュ
            if (!node.shaderPath.empty() && m_pipelines.find(node.shaderPath) == m_pipelines.end()) {
                m_pipelines[node.shaderPath] = std::make_unique<VulkanComputePipeline>(m_device, node.shaderPath, MAX_PUSH_CONSTANT_SIZE);
            }

            node.resourceHandles.clear();
            node.requirements.clear();
            std::set<ResourceHandle> seenHandles; // 重複排除用

            for (const auto& ds : node.dispatchStates) {
                for (const auto& [offset, handle] : ds.resourceOffsets) {
                    auto it = node.signature.find(offset);
                    // ここに到達した時点でシグネチャとの整合性は setResource で保証されている，はず
                    
                    // todo: マルチディスパッチ時の競合チェック（同一リソースの読み書き衝突検知）はここに実装する
                    // ds（ディスパッチ）を跨いで同じ handle が isWriteUsage でアクセスされていないか等

                    if (seenHandles.insert(handle).second) {
                        node.resourceHandles.push_back(handle);
                        node.requirements.push_back({offset, it->second, rhi::ShaderStage::Compute}); // Compute固定　todo他のstage
                        
                        // 基底クラスのソート機能のために producers / consumers を更新
                        if (isWriteUsage(it->second)) {
                            m_resourceRegistry[handle].producers.push_back((uint32_t)passIndex);
                        } else {
                            m_resourceRegistry[handle].consumers.push_back((uint32_t)passIndex);
                        }
                    }
                }
            }
        }
        std::cout << "RenderGraph requirements collected." << std::endl;//debug
        m_physicalNodes.resize(m_logicalNodes.size());
        std::vector<uint32_t> passIndices(m_logicalNodes.size());
        std::iota(passIndices.begin(), passIndices.end(), 0);
        std::cout << "Sorting passes..." << std::endl;//debug
        // 2. パスソート、ライフタイム計算、物理リソース割り当て
        std::vector<uint32_t> sortedIndices = getSortPasses(passIndices);
        calculateLifetimes(sortedIndices);
        m_resourceAllocator.allocate(m_resourceRegistry, m_resourceLifetimes);
        std::cout << "Physical resources allocated." << std::endl;//debug
        // 3. バリアとレイアウト遷移の生成
        std::map<rhi::ResourceHandle, VulkanResourceState> currentStates;
        for (uint32_t passIdx : sortedIndices) {
            auto& logicalNode = m_logicalNodes[passIdx];
            auto& physiaclNode = m_physicalNodes[passIdx];
            physiaclNode.imageBarriers.clear();
            physiaclNode.bufferBarriers.clear();
            
            for (size_t i = 0; i < logicalNode.resourceHandles.size(); ++i) {
                rhi::ResourceHandle h = logicalNode.resourceHandles[i];
                const auto& req = logicalNode.requirements[i];
                VulkanResourceState next = MapResourceState(req.usage, req.stage);
                VulkanResourceState prev = currentStates.count(h) ? currentStates[h] : 
                    VulkanResourceState{ VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED };
                
                if (prev.layout != next.layout || (next.accessMask & VK_ACCESS_2_SHADER_WRITE_BIT)) {
                    if (m_resourceRegistry[h].isImage()) {
                        VkImageMemoryBarrier2 imgBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                        imgBarrier.srcStageMask  = prev.stageMask; imgBarrier.srcAccessMask = prev.accessMask;
                        imgBarrier.dstStageMask  = next.stageMask; imgBarrier.dstAccessMask = next.accessMask;
                        imgBarrier.oldLayout     = prev.layout;    imgBarrier.newLayout     = next.layout;
                        imgBarrier.image         = m_resourceAllocator.getPhysicalImage(h)->getImage();
                        imgBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                        physiaclNode.imageBarriers.push_back(imgBarrier);
                    } else {
                        VkBufferMemoryBarrier2 bufBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                        bufBarrier.srcStageMask  = prev.stageMask; bufBarrier.srcAccessMask = prev.accessMask;
                        bufBarrier.dstStageMask  = next.stageMask; bufBarrier.dstAccessMask = next.accessMask;
                        bufBarrier.buffer        = m_resourceAllocator.getPhysicalBuffer(h)->getNativeBuffer();
                        bufBarrier.offset        = 0;              bufBarrier.size          = VK_WHOLE_SIZE;
                        physiaclNode.bufferBarriers.push_back(bufBarrier);
                    }
                }
                currentStates[h] = next;
            }
        }
        std::cout << "Barriers and layout transitions generated." << std::endl;//debug
        m_sortedIndices = sortedIndices;
    }

    void VulkanRenderGraph::execute(CommandList& cmd) {
        auto& vkCmd = static_cast<VulkanCommandList&>(cmd);

        for (size_t i = 0; i < m_logicalNodes.size(); ++i) {
            uint32_t nodeIdx = m_sortedIndices[i];
            auto& logicalNode = m_logicalNodes[nodeIdx];
            auto& physiaclNode = m_physicalNodes[nodeIdx];

            // 1. 一括バリアの発行
            if (!physiaclNode.imageBarriers.empty() || !physiaclNode.bufferBarriers.empty()) {
                VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                depInfo.imageMemoryBarrierCount = (uint32_t)physiaclNode.imageBarriers.size();
                depInfo.pImageMemoryBarriers    = physiaclNode.imageBarriers.data();
                depInfo.bufferMemoryBarrierCount = (uint32_t)physiaclNode.bufferBarriers.size();
                depInfo.pBufferMemoryBarriers    = physiaclNode.bufferBarriers.data();
                vkCmdPipelineBarrier2(vkCmd.getCommandBuffer(), &depInfo);
            }

            // 2. パイプラインのバインド
            if (!logicalNode.shaderPath.empty()) {
                auto* pipeline = m_pipelines[logicalNode.shaderPath].get();
                if (pipeline) {
                    vkCmd.bindPipeline(*pipeline);
                    vkCmd.bindGlobalDescriptorSet();
                }
            }

            // 3. ループでマルチディスパッチを処理
            for (auto& state : logicalNode.dispatchStates) {
                std::array<uint8_t, MAX_PUSH_CONSTANT_SIZE> finalPushData = state.pushData;
                uint32_t finalPushSize = state.pushDataSize;

                for (auto const& [offset, handle] : state.resourceOffsets) {
                    uint32_t bindlessIndex = getPhysicalIndex(handle);
                    if (offset + 4 <= MAX_PUSH_CONSTANT_SIZE) {
                        std::memcpy(finalPushData.data() + offset, &bindlessIndex, 4);
                        finalPushSize = std::max(finalPushSize, offset + 4);
                    }
                }
                
                for (auto const& [offset, binding] : state.uboBindings) {
                    if (offset + 8 <= MAX_PUSH_CONSTANT_SIZE) {
                        std::memcpy(finalPushData.data() + offset, &binding.index, 4);
                        std::memcpy(finalPushData.data() + offset + 4, &binding.offset, 4);
                        finalPushSize = std::max(finalPushSize, offset + 8);
                    }
                }

                if (finalPushSize > 0) {
                    vkCmd.setPushData(0, finalPushSize, finalPushData.data());
                }
                vkCmd.dispatch(state.x, state.y, state.z);
            }
        }
    }


}