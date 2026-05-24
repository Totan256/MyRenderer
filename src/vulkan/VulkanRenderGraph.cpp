#include "VulkanRenderGraph.hpp"
#include "VulkanCommandList.hpp"
#include "VulkanComputePipeline.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanImage.hpp"
#include "RHIcommon.hpp"
#include "RHIForward.hpp"
#include "VulkanResourceAllocator.hpp"
#include "VulkanConstantBufferManager.hpp"
#include "ShaderReflection.hpp"
#include <map>
#include <queue>
#include <vector>
#include <set>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <iostream>

namespace rhi::vk {
    class VulkanDispatchObject : public DispatchObject {
    public:
        VulkanDispatchObject(LogicalPass& node, LogicalPass::DispatchState& ds, VulkanRenderGraph& graph) 
            : m_node(node), m_ds(ds), m_graph(graph) {}
        
        ~VulkanDispatchObject(){}
        DispatchObject& updateConstantRaw(uint32_t offset, const void* data, size_t size) override{
            if (offset + size > MAX_PUSH_CONSTANT_SIZE) throw std::runtime_error("Push constants limit exceeded!");
            std::memcpy(m_ds.pushData.data() + offset, data, size);
            m_ds.pushDataSize = std::max(m_ds.pushDataSize, static_cast<uint32_t>(offset + size));
            return *this;
        }
        DispatchObject& read(ResourceHandle handle) override {
            return bindByHash(handle, rhi::ResourceState::StorageRead);
        }
        DispatchObject& write(ResourceHandle handle) override {
            return bindByHash(handle, rhi::ResourceState::StorageWrite);
        }
        DispatchObject& readUniform(ResourceHandle handle) override {
            return bindByHash(handle, rhi::ResourceState::ConstantBuffer);
        }
        void updateResource(uint32_t offset, ResourceHandle handle){
            // 簡易バリデーション: パスのbind()で宣言されていないオフセットへのセットを弾く
            if (m_node.signature.find(offset) == m_node.signature.end()) {
                throw std::runtime_error("Cannot set resource: offset " + std::to_string(offset) + 
                                         " is not defined in the pass signature. Did you forget to bind() it?");
            }
            // ToDo: 将来的には m_node.signature[offset] (要求Usage) と registry[handle] でisCompatibleによる互換性チェックを追加
            m_ds.resourceOffsets[offset] = handle;
            return;
        }
        DispatchObject& setUniformRaw(StringHash nameHash, const void* data, size_t size) override {
            auto it = m_node.pushConstantOffsets.find(nameHash);
            if (it == m_node.pushConstantOffsets.end()) throw std::runtime_error("Uniform not found in shader!");
            auto& vec = m_ds.dynamicUniforms[it->second];
            vec.resize(size);
            std::memcpy(vec.data(), data, size);
            return *this;
        }
        DispatchObject& updateSize(uint32_t x, uint32_t y, uint32_t z) override {
            m_ds.x = x;
            m_ds.y = y;
            m_ds.z = z;
            return *this;
        }
    private:
        VulkanRenderGraph& m_graph;
        LogicalPass& m_node;
        LogicalPass::DispatchState& m_ds;
        
        DispatchObject& bindByHash(ResourceHandle handle, rhi::ResourceState state) {
            StringHash nameHash = m_graph.getRegistration(handle).nameHash;
            auto it = m_node.pushConstantOffsets.find(nameHash);
            if (it == m_node.pushConstantOffsets.end()) {
                throw std::runtime_error("Resource name not found in shader push constants!");
            }
            uint32_t offset = it->second;
            m_node.signature[offset] = state; // Usageを自動セット
            updateResource(offset, handle);
            return *this;
        }
    };

    
    class VulkanPassBuilder : public PassBuilder {
    public:
        VulkanPassBuilder(VulkanRenderGraph& graph, LogicalPass& node) : m_graph(graph), m_node(node) {}

        PassBuilder& bind(const BindGroup& desc) override {
            for(const auto& entry : desc.requirements) {
                m_node.signature[entry.offset] = entry.state;
            }
            return *this;
        }
        PassBuilder& bind(uint32_t offset, ResourceState state) override {
            m_node.signature[offset] = state;
            return *this;
        }
        DispatchObject& dispatch(uint32_t x, uint32_t y, uint32_t z) override {
            return m_graph.createDispatch(m_node, x, y, z);
        }
        DispatchObject& dispatchThreads(uint32_t width, uint32_t height, uint32_t depth) {
            uint32_t gx = (width + m_node.localSizeX - 1) / m_node.localSizeX;
            uint32_t gy = (height + m_node.localSizeY - 1) / m_node.localSizeY;
            uint32_t gz = (depth + m_node.localSizeZ - 1) / m_node.localSizeZ;
            return m_graph.createDispatch(m_node, gx, gy, gz);
        }
        PassBuilder& forceBatchBreak() override {
            m_node.forceBatchBreak = true;
            return *this;
        }
        
    private:
        VulkanRenderGraph& m_graph;
        LogicalPass& m_node;
    };

    DispatchObject& VulkanRenderGraph::createDispatch(LogicalPass& node, uint32_t x, uint32_t y, uint32_t z) {
        if (node.dispatchStates.empty()) {
            node.dispatchStates.push_back({});
        } else {
            node.dispatchStates.push_back(node.dispatchStates.back());
        }
        auto& ds = node.dispatchStates.back();
        ds.id = static_cast<uint32_t>(node.dispatchStates.size() - 1);
        ds.x = x; ds.y = y; ds.z = z;
        
        m_dispatchObjects.push_back(std::make_unique<VulkanDispatchObject>(node, ds, *this));
        return *m_dispatchObjects.back();
    }
    PassBuilder& VulkanRenderGraph::addPass(const std::string& name, const std::string& shaderPath, QueueType queueType) {
        m_logicalNodes.emplace_back();
        auto& node = m_logicalNodes.back();
        node.name = name;
        node.shaderPath = shaderPath;

        // リフレクション実行
        if (shaderPath.ends_with(".comp")) { // TODO: SPV対応
            auto spirv = VulkanComputePipeline::compileGLSLToSPIRV(shaderPath);
            std::cout << "Compiled shader to SPIR-V: " << shaderPath << " (size: " << spirv.size() * sizeof(uint32_t) << " bytes)" << std::endl;
            auto reflection = ShaderReflection::reflect(spirv);
            
            node.type = reflection.passType;
            node.queueType = queueType; // 引数優先
            node.localSizeX = reflection.localSizeX;
            node.localSizeY = reflection.localSizeY;
            node.localSizeZ = reflection.localSizeZ;
            node.pushConstantOffsets = reflection.pushConstantOffsets;
            std::cout << "Reflected shader: " << shaderPath << " (localSize: " << node.localSizeX << "," << node.localSizeY << "," << node.localSizeZ << ")" << std::endl;

        }

        m_builders.push_back(std::make_unique<VulkanPassBuilder>(*this, node));
        return *m_builders.back();
    }
    ResourceHandle VulkanRenderGraph::importResource(Resource* res, StringHash nameHash) {
        if (m_physicalToHandle.count(res)) {
            return m_physicalToHandle[res];
        }
        ResourceHandle handle = static_cast<ResourceHandle>(m_resourceRegistry.size());
        ResourceRegistration reg{}; reg.isImported = true; reg.physicalResource = res; reg.nameHash = nameHash;
        m_resourceRegistry.push_back(reg); 
        m_physicalToHandle[res] = handle;
        return handle;
    }
    ResourceHandle VulkanRenderGraph::createImage(const ImageDesc& desc, StringHash nameHash) {
        ResourceHandle handle = static_cast<ResourceHandle>(m_resourceRegistry.size());
        ResourceRegistration reg{}; reg.isImported = false; reg.desc = desc; reg.nameHash = nameHash;
        m_resourceRegistry.push_back(reg); return handle;
    }
    ResourceHandle VulkanRenderGraph::createBuffer(const BufferDesc& desc, StringHash nameHash) {
        ResourceHandle handle = static_cast<ResourceHandle>(m_resourceRegistry.size());
        ResourceRegistration reg{}; reg.isImported = false; reg.desc = desc; reg.nameHash = nameHash;
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

    void VulkanRenderGraph::addCopyPass(const std::string& name, ResourceHandle srcBuffer, ResourceHandle dstBuffer, size_t size, QueueType queueType) {
        m_logicalNodes.emplace_back();
        auto& node = m_logicalNodes.back();
        node.name = name;
        node.type = PassType::Copy; // パスタイプをコピーに設定
        node.queueType = queueType;
        
        // 依存関係（バリア）を解決するために、入出力として登録する
        node.resourceHandles.push_back(srcBuffer);
        node.requirements.push_back({0, rhi::ResourceState::TransferSrc, rhi::ShaderStage::Transfer});
        
        node.resourceHandles.push_back(dstBuffer);
        node.requirements.push_back({1, rhi::ResourceState::TransferDst, rhi::ShaderStage::Transfer});
        
        // DispatchStateを悪用（借用）してコピー情報を保存しておく（オフセットとサイズ）
        node.dispatchStates.push_back({});
        auto& ds = node.dispatchStates.back();
        ds.resourceOffsets[0] = srcBuffer;
        ds.resourceOffsets[1] = dstBuffer;
        ds.x = size; // サイズ情報を一時的に保存
        ds.y = 0; ds.z = 0;    // 未使用
    }

    void VulkanRenderGraph::compile() {
        std::cout << "Compiling RenderGraph..." << std::endl;//debug
        //古いセマフォのクリア
        clearBatchSemaphores();

        // 0. アップロード要求の取得とパスの自動追加
        auto uploads = m_device.getUploadManager()->getAndClearPendingUploads();
        if (!uploads.empty()) {
            m_logicalNodes.emplace_front(); // Transferパスを先頭に挿入
            auto& node = m_logicalNodes.front();
            node.name = "AutoUpload";
            node.type = PassType::Copy;
            node.queueType = rhi::QueueType::Transfer;
            
            for (size_t i = 0; i < uploads.size(); ++i) {
                const auto& up = uploads[i];
                ResourceHandle hStaging = importResource(up.stagingBuffer);
                ResourceHandle hDst = importResource(up.dstBuffer);
                
                node.resourceHandles.push_back(hStaging);
                node.requirements.push_back({0, rhi::ResourceState::TransferSrc, rhi::ShaderStage::Transfer});
                node.resourceHandles.push_back(hDst);
                node.requirements.push_back({1, rhi::ResourceState::StorageWrite, rhi::ShaderStage::Transfer}); // Write依存を作るためStorageWriteとして扱う
                
                node.dispatchStates.push_back({});
                auto& ds = node.dispatchStates.back();
                ds.resourceOffsets[0] = hStaging;
                ds.resourceOffsets[1] = hDst;
                ds.x = up.size;
                ds.y = up.stagingOffset;
            }
        }

        // 1. 各パスの要件（requirements）をディスパッチから遅延集計する
        for (size_t passIndex = 0; passIndex < m_logicalNodes.size(); ++passIndex) {
            auto& node = m_logicalNodes[passIndex];
            
            // パイプラインの作成・キャッシュ //Todo あとでリフレクションのときにまとめて行う
            if (!node.shaderPath.empty() && m_pipelines.find(node.shaderPath) == m_pipelines.end()) {
                m_pipelines[node.shaderPath] = std::make_unique<VulkanComputePipeline>(m_device, node.shaderPath, MAX_PUSH_CONSTANT_SIZE);
            }

            node.resourceHandles.clear();
            node.requirements.clear();
            std::set<ResourceHandle> seenHandles; // 重複排除用

            for (const auto& ds : node.dispatchStates) {
                for (const auto& [offset, handle] : ds.resourceOffsets) {
                    if (node.type == PassType::Copy) continue;  // PassType::Copyの時はsignatureがないので特別扱い
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
            // Copyパスの依存関係はすでに追加済みなので producers/consumers だけ登録
            if (node.type == PassType::Copy) {
                for(size_t i=0; i<node.resourceHandles.size(); ++i) {
                    if (isWriteUsage(node.requirements[i].state)) {
                        m_resourceRegistry[node.resourceHandles[i]].producers.push_back((uint32_t)passIndex);
                    } else {
                        m_resourceRegistry[node.resourceHandles[i]].consumers.push_back((uint32_t)passIndex);
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
                VulkanResourceState next = MapResourceState(req.state, req.stage);
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

        // 4. バッチの分割と同期セマフォの生成
        m_batches.clear();
        QueueType currentQueue = QueueType::Compute; 
        RenderBatch* currentBatch = nullptr;

        for (uint32_t passIdx : sortedIndices) {
            auto& logicalNode = m_logicalNodes[passIdx];
            // バッチの分割条件: キューが変わった、または明示的な手動分割要求があった
            bool shouldBreak = (currentBatch == nullptr) || 
                               (logicalNode.queueType != currentQueue) ||
                               (logicalNode.forceBatchBreak);
            if (shouldBreak) {
                m_batches.push_back({});
                currentBatch = &m_batches.back();
                currentBatch->queueType = logicalNode.queueType;
                currentBatch->cmdList = std::make_unique<VulkanCommandList>(m_device, logicalNode.queueType);
                currentQueue = logicalNode.queueType;

                // バッチを跨ぐ場合、セマフォを生成して前後のバッチを繋ぐ
                if (m_batches.size() > 1) {
                    RenderBatch& prevBatch = m_batches[m_batches.size() - 2];
                    VkSemaphore sem = m_device.createSemaphore();
                    m_batchSemaphores.push_back(sem);
                    prevBatch.signalSemaphores.push_back(sem);
                    currentBatch->waitSemaphores.push_back(sem);
                    // 適切なステージで待つ。単純化のため全コマンドの完了を待機
                    currentBatch->waitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); 
                }
            }
            currentBatch->passIndices.push_back(passIdx);
        }

        std::cout << "Barriers and layout transitions generated." << std::endl;//debug
        m_sortedIndices = sortedIndices;
    }

    void VulkanRenderGraph::execute(const std::vector<SemaphoreHandle>& waitSemaphores) {
        for (size_t batchIdx = 0; batchIdx < m_batches.size(); ++batchIdx) {
            auto& batch = m_batches[batchIdx];
            batch.cmdList->reset();
            batch.cmdList->begin();
            
            auto vkCmdBuf = static_cast<VulkanCommandList*>(batch.cmdList.get())->getCommandBuffer();

            for (uint32_t passIdx : batch.passIndices) {
                auto& logicalNode = m_logicalNodes[passIdx];
                auto& physiaclNode = m_physicalNodes[passIdx];

                if (!physiaclNode.imageBarriers.empty() || !physiaclNode.bufferBarriers.empty()) {
                    VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                    depInfo.imageMemoryBarrierCount = (uint32_t)physiaclNode.imageBarriers.size();
                    depInfo.pImageMemoryBarriers    = physiaclNode.imageBarriers.data();
                    depInfo.bufferMemoryBarrierCount = (uint32_t)physiaclNode.bufferBarriers.size();
                    depInfo.pBufferMemoryBarriers    = physiaclNode.bufferBarriers.data();
                    vkCmdPipelineBarrier2(vkCmdBuf, &depInfo);
                }

                if (logicalNode.type == PassType::Copy) {
                    for(auto& ds : logicalNode.dispatchStates) {
                        rhi::Buffer* physSrc = m_resourceAllocator.getPhysicalBuffer(ds.resourceOffsets[0]);
                        rhi::Buffer* physDst = m_resourceAllocator.getPhysicalBuffer(ds.resourceOffsets[1]);
                        batch.cmdList->copyBuffer(physSrc, physDst, ds.x, ds.y, 0);
                    }
                } 
                else if (logicalNode.type == PassType::Compute) {
                    if (!logicalNode.shaderPath.empty()) {
                        auto* pipeline = m_pipelines[logicalNode.shaderPath].get();
                        if (pipeline) {
                            batch.cmdList->bindPipeline(*pipeline);
                            batch.cmdList->bindGlobalDescriptorSet();
                        }
                    }
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
                        for (auto const& [offset, dataVec] : state.dynamicUniforms) {
                            auto allocation = m_device.getConstantBufferManager().allocateAndWrite(dataVec.data(), dataVec.size());
                            if (offset + 8 <= MAX_PUSH_CONSTANT_SIZE) {
                                std::memcpy(finalPushData.data() + offset, &allocation.index, 4);
                                std::memcpy(finalPushData.data() + offset + 4, &allocation.offset, 4);
                                finalPushSize = std::max(finalPushSize, offset + 8);
                            }
                        }
                        if (finalPushSize > 0) {
                            static_cast<VulkanCommandList*>(batch.cmdList.get())->setPushData(0, finalPushSize, finalPushData.data());
                        }
                        batch.cmdList->dispatch(state.x, state.y, state.z);
                    }
                }
            }
            batch.cmdList->end();

            // Submit 構築
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            // 外部からのWaitSemaphoresは「最初のバッチ」にのみ適用する
            std::vector<VkSemaphore> currentWaitSemaphores = batch.waitSemaphores;
            std::vector<VkPipelineStageFlags> currentWaitStages = batch.waitStages;

            if (batchIdx == 0 && !waitSemaphores.empty()) {
                for (auto semHandle : waitSemaphores) {
                    currentWaitSemaphores.push_back(static_cast<VkSemaphore>(semHandle));
                    currentWaitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); 
                }
            }
            
            if (!batch.waitSemaphores.empty()) {
                submitInfo.waitSemaphoreCount = (uint32_t)batch.waitSemaphores.size();
                submitInfo.pWaitSemaphores = batch.waitSemaphores.data();
                submitInfo.pWaitDstStageMask = batch.waitStages.data();
            }
            
            if (!batch.signalSemaphores.empty()) {
                submitInfo.signalSemaphoreCount = (uint32_t)batch.signalSemaphores.size();
                submitInfo.pSignalSemaphores = batch.signalSemaphores.data();
            }
            
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &vkCmdBuf;

            VkQueue queue = m_device.getQueue(batch.queueType);
            VkFence frameFence = VK_NULL_HANDLE;
            if (&batch == &m_batches.back()) {
                frameFence = m_device.getCurrentFrameFence();
            }
            VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, frameFence));
        }
    }
}