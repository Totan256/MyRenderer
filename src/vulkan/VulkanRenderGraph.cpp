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
#include "VulkanCache.hpp"
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
        DispatchObject& setDrawParams(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex = 0, uint32_t firstInstance = 0) override {
            m_ds.vertexCount = vertexCount;
            m_ds.instanceCount = instanceCount;
            m_ds.firstVertex = firstVertex;
            m_ds.firstInstance = firstInstance;
            m_ds.isIndirect = false;
            return *this;
        }
        DispatchObject& setIndirectParams(ResourceHandle indirectBuffer, size_t indirectOffset, ResourceHandle countBuffer, size_t countOffset, uint32_t maxDrawCount) override {
            m_ds.isIndirect = true;
            m_ds.indirectBuffer = indirectBuffer;
            m_ds.indirectOffset = indirectOffset;
            m_ds.countBuffer = countBuffer;
            m_ds.countOffset = countOffset;
            m_ds.maxDrawCount = maxDrawCount;
            // パスに必要なリソースとして登録 (バリア用)
            m_node.signature[0xFFFFFFFE] = rhi::ResourceState::StorageRead; // ダミーオフセットで登録
            updateResource(0xFFFFFFFE, indirectBuffer);
            m_node.signature[0xFFFFFFFF] = rhi::ResourceState::StorageRead;
            updateResource(0xFFFFFFFF, countBuffer);
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

        PassBuilder& setGraphicsState(const rhi::GraphicsState& state) override {
            m_node.graphicsState = state;
            return *this;
        }
        PassBuilder& addColorOutput(uint32_t location, ResourceHandle handle, LoadOp loadOp, StoreOp storeOp, ColorClearValue clearValue) override {
            m_node.colorAttachments.push_back({location, handle, loadOp, storeOp, clearValue});
            m_node.resourceHandles.push_back(handle);
            m_node.requirements.push_back({0x10000 + location, rhi::ResourceState::ColorAttachment, rhi::ShaderStage::Fragment});
            const auto& reg = m_graph.getRegistration(handle);
            if (reg.isImported && reg.physicalResource && reg.physicalResource->isImage()) {
                m_node.colorFormats[location] = static_cast<VulkanImage*>(reg.physicalResource)->getDesc().format;
            } else if (std::holds_alternative<rhi::ImageDesc>(reg.desc)) {
                m_node.colorFormats[location] = std::get<rhi::ImageDesc>(reg.desc).format;
            }
            return *this;
        }
        PassBuilder& addColorOutput(StringHash nameHash, ResourceHandle handle, LoadOp loadOp, StoreOp storeOp, ColorClearValue clearValue) override {
            uint32_t location = m_node.outputLocations[nameHash];
            return addColorOutput(location, handle, loadOp, storeOp, clearValue);
        }
        PassBuilder& setDepthOutput(ResourceHandle handle, LoadOp loadOp, StoreOp storeOp, DepthClearValue clearValue) override {
            m_node.depthAttachment = {handle, loadOp, storeOp, clearValue};
            m_node.resourceHandles.push_back(handle);
            m_node.requirements.push_back({0x20000, rhi::ResourceState::DepthStencilWrite, rhi::ShaderStage::Fragment});
            m_node.hasDepth = true;
            const auto& reg = m_graph.getRegistration(handle);
            if (reg.isImported && reg.physicalResource && reg.physicalResource->isImage()) {
                m_node.depthFormat = static_cast<VulkanImage*>(reg.physicalResource)->getDesc().format;
            } else if (std::holds_alternative<rhi::ImageDesc>(reg.desc)) {
                m_node.depthFormat = std::get<rhi::ImageDesc>(reg.desc).format;
            }
            return *this;
        }
        DispatchObject& draw(uint32_t vertexCount, uint32_t instanceCount) override {
            auto& obj = m_graph.createDispatch(m_node, 1, 1, 1).setDrawParams(vertexCount, instanceCount);
            return obj;
        }
        DispatchObject& drawIndexedIndirectCount(ResourceHandle indirectBuffer, ResourceHandle countBuffer, uint32_t maxDrawCount) override {
            auto& obj = m_graph.createDispatch(m_node, 1, 1, 1).setIndirectParams(indirectBuffer, 0, countBuffer, 0, maxDrawCount);
            return obj;
        }
        
    private:
        VulkanRenderGraph& m_graph;
        LogicalPass& m_node;
    };

    VulkanRenderGraph::~VulkanRenderGraph() {
        clearBatchSemaphores();
    }
    void VulkanRenderGraph::clearBatchSemaphores() {
        for (VkSemaphore sem : m_batchSemaphores) {
            if (sem != VK_NULL_HANDLE) {
                m_device.enqueueDeletion([device = &m_device, s = sem]() {
                    device->releaseSemaphore(s);
                });
            }
        }
        m_batchSemaphores.clear();
    }
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

        if (shaderPath.ends_with(".comp")) { 
            // Deviceのキャッシュからリフレクション情報を取得
            const auto& shaderData = m_device.getShaderCache().getOrCreateShader(shaderPath, shaderc_compute_shader);
            
            node.type = shaderData.reflection.passType;
            node.queueType = queueType; 
            node.localSizeX = shaderData.reflection.localSizeX;
            node.localSizeY = shaderData.reflection.localSizeY;
            node.localSizeZ = shaderData.reflection.localSizeZ;
            node.pushConstantOffsets = shaderData.reflection.pushConstantOffsets;
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

    PassBuilder& VulkanRenderGraph::addGraphicsPass(const std::string& name, const std::string& vertShaderPath, const std::string& fragShaderPath) {
        m_logicalNodes.emplace_back();
        auto& node = m_logicalNodes.back();
        node.name = name;
        node.type = PassType::Graphics;
        node.queueType = QueueType::Graphics;
        node.vertShaderPath = vertShaderPath;
        node.fragShaderPath = fragShaderPath;

        // Deviceのキャッシュから頂点/フラグメント双方のリフレクション情報を取得
        const auto& vertData = m_device.getShaderCache().getOrCreateShader(vertShaderPath, shaderc_vertex_shader);
        const auto& fragData = m_device.getShaderCache().getOrCreateShader(fragShaderPath, shaderc_fragment_shader);
        
        node.pushConstantOffsets = vertData.reflection.pushConstantOffsets;
        node.outputLocations = fragData.reflection.outputLocations; // フラグメントの出力位置情報をパスに保存

        m_builders.push_back(std::make_unique<VulkanPassBuilder>(*this, node));
        return *m_builders.back();
    }

    auto getAspectMask = [](rhi::Format format) -> VkImageAspectFlags {
        if (format == rhi::Format::D32_Sfloat || format == rhi::Format::D24_Unorm) {
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        return VK_IMAGE_ASPECT_COLOR_BIT;
    };

    void VulkanRenderGraph::compile() {
        std::cout << "Compiling RenderGraph..." << std::endl;//debug
        //古いセマフォのクリア
        clearBatchSemaphores();

        // 0. アップロード要求の取得とパスの自動追加
        auto uploadManager = m_device.getUploadManager();
        auto uploads = uploadManager->getAndClearPendingUploads();
        auto imgUploads = uploadManager->getAndClearPendingImageUploads();
        if (!imgUploads.empty()) {
            m_logicalNodes.emplace_front();
            auto& mipNode = m_logicalNodes.front();
            mipNode.name = "AutoMipmapGen";
            mipNode.type = PassType::GenerateMipmaps; // 専用のシステムパスタイプ
            mipNode.queueType = rhi::QueueType::Compute; // 今後コンピュート化する際もこのままでOK
            
            for (const auto& up : imgUploads) {
                ResourceHandle hDst = importResource(up.dstImage);
                mipNode.resourceHandles.push_back(hDst);
                // レンダーグラフに対して、この段階で画像への書き込み要件が発生することを通知
                mipNode.requirements.push_back({0, rhi::ResourceState::StorageWrite, rhi::ShaderStage::Compute});
            }
        }
        if (!imgUploads.empty()) {
            m_logicalNodes.emplace_front();
            auto& copyImgNode = m_logicalNodes.front();
            copyImgNode.name = "AutoImageUpload";
            copyImgNode.type = PassType::Copy;
            copyImgNode.queueType = rhi::QueueType::Transfer;
            
            for (const auto& up : imgUploads) {
                ResourceHandle hStaging = importResource(up.stagingBuffer);
                ResourceHandle hDst = importResource(up.dstImage);
                
                copyImgNode.resourceHandles.push_back(hStaging);
                copyImgNode.requirements.push_back({0, rhi::ResourceState::TransferSrc, rhi::ShaderStage::Transfer});
                copyImgNode.resourceHandles.push_back(hDst);
                copyImgNode.requirements.push_back({1, rhi::ResourceState::TransferDst, rhi::ShaderStage::Transfer});
                
                copyImgNode.dispatchStates.push_back({});
                auto& ds = copyImgNode.dispatchStates.back();
                ds.resourceOffsets[0] = hStaging;
                ds.resourceOffsets[1] = hDst;
                ds.x = up.width;           // execute側へ引き渡すメタデータ
                ds.y = up.height;
                ds.z = up.stagingOffset;
                ds.id = up.mipLevels;      // Mipレベル数を格納
            }
        }
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
        m_physicalNodes.resize(m_logicalNodes.size());

        // 1. 各パスの要件（requirements）をディスパッチから遅延集計する
        for (size_t passIndex = 0; passIndex < m_logicalNodes.size(); ++passIndex) {
            auto& node = m_logicalNodes[passIndex];
            auto& physNode = m_physicalNodes[passIndex];
            // --- パイプラインの遅延解決 ---
            if (node.type == PassType::Compute && !node.shaderPath.empty()) {
                physNode.computePipeline = m_device.getPipelineCache().getOrCreateComputePipeline(node.shaderPath, MAX_PUSH_CONSTANT_SIZE);
            }
            else if (node.type == PassType::Graphics) {
                std::vector<VkFormat> vkColorFormats;
                for (auto const& [idx, fmt] : node.colorFormats) {
                    vkColorFormats.push_back(mapFormat(fmt));
                }
                VkFormat vkDepthFormat = node.hasDepth ? mapFormat(node.depthFormat) : VK_FORMAT_UNDEFINED;

                // フォーマットを含めてパイプラインを要求
                physNode.graphicsPipeline = m_device.getPipelineCache().getOrCreateGraphicsPipeline(
                    node.vertShaderPath, node.fragShaderPath, vkColorFormats, vkDepthFormat, MAX_PUSH_CONSTANT_SIZE);
            }
            // リソースハンドル集計
            std::set<ResourceHandle> seenHandles;
            for (auto handle : node.resourceHandles) {
                seenHandles.insert(handle);
            }

            for (const auto& ds : node.dispatchStates) {
                for (const auto& [offset, handle] : ds.resourceOffsets) {
                    auto it = node.signature.find(offset);
                    if (it != node.signature.end()) {
                        // まだ要件として登録されていないリソースのみ追記する
                        if (seenHandles.insert(handle).second) {
                            node.resourceHandles.push_back(handle);
                            node.requirements.push_back({offset, it->second, rhi::ShaderStage::Compute}); 
                            // ※必要に応じて Fragment/Vertex 等も考慮
                        }
                    }
                }
            }
        }
        std::cout << "RenderGraph requirements collected." << std::endl;//debug
        std::vector<uint32_t> passIndices(m_logicalNodes.size());
        std::iota(passIndices.begin(), passIndices.end(), 0);
        std::cout << "Sorting passes..." << std::endl;//debug
        // 2. パスソート、ライフタイム計算、物理リソース割り当て
        std::vector<uint32_t> sortedIndices = getSortPasses(passIndices);
        calculateLifetimes(sortedIndices);
        m_resourceAllocator.allocate(m_resourceRegistry, m_resourceLifetimes);
        std::cout << "Physical resources allocated." << std::endl;//debug
        // 3. バリアとレイアウト遷移の生成
        struct ResourceTrackingState {
            uint32_t lastPassIdx;
            VulkanResourceState state;
            QueueType queueType;
        };
        std::map<rhi::ResourceHandle, ResourceTrackingState> currentStates;
        for (uint32_t passIdx : sortedIndices) {
            auto& logicalNode = m_logicalNodes[passIdx];
            auto& physiaclNode = m_physicalNodes[passIdx];
            physiaclNode.imageBarriers.clear();
            physiaclNode.bufferBarriers.clear();
            uint32_t currentQueueFamily = m_device.getQueueFamilyIndex(logicalNode.queueType);
            for (size_t i = 0; i < logicalNode.resourceHandles.size(); ++i) {
                rhi::ResourceHandle h = logicalNode.resourceHandles[i];
                const auto& req = logicalNode.requirements[i];
                VulkanResourceState next = MapResourceState(req.state, req.stage);
                if (currentStates.count(h)) {
                    auto& prevTrack = currentStates[h];
                    uint32_t prevQueueFamily = m_device.getQueueFamilyIndex(prevTrack.queueType);
                    if (prevQueueFamily != currentQueueFamily) {
                        // キューファミリーが異なるため、所有権移行（Ownership Transfer）を生成
                        if (m_resourceRegistry[h].isImage()) {
                            VulkanImage* physImg = m_resourceAllocator.getPhysicalImage(h);
                            VkImage img = physImg->getImage();
                            // 移行元キュー（前回のパス）に Release バリアを挿入
                            VkImageMemoryBarrier2 releaseBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                            releaseBarrier.srcStageMask        = prevTrack.state.stageMask;
                            releaseBarrier.srcAccessMask       = prevTrack.state.accessMask;
                            releaseBarrier.dstStageMask        = VK_PIPELINE_STAGE_2_NONE;
                            releaseBarrier.dstAccessMask       = VK_ACCESS_2_NONE;
                            releaseBarrier.oldLayout           = prevTrack.state.layout;
                            releaseBarrier.newLayout           = prevTrack.state.layout; // ★同じレイアウトを維持
                            releaseBarrier.srcQueueFamilyIndex = prevQueueFamily;
                            releaseBarrier.dstQueueFamilyIndex = currentQueueFamily;
                            releaseBarrier.image               = img;
                            releaseBarrier.subresourceRange    = { getAspectMask(physImg->getDesc().format), 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
                            m_physicalNodes[prevTrack.lastPassIdx].imageBarriers.push_back(releaseBarrier);
                            // 移行先キュー（現在のパス）に Acquire バリアを挿入
                            VkImageMemoryBarrier2 acquireBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                            acquireBarrier.srcStageMask        = VK_PIPELINE_STAGE_2_NONE;
                            acquireBarrier.srcAccessMask       = VK_ACCESS_2_NONE;
                            acquireBarrier.dstStageMask        = next.stageMask;
                            acquireBarrier.dstAccessMask       = next.accessMask;
                            acquireBarrier.oldLayout           = prevTrack.state.layout;
                            acquireBarrier.newLayout           = prevTrack.state.layout; // ★同じレイアウトを維持
                            acquireBarrier.srcQueueFamilyIndex = prevQueueFamily;
                            acquireBarrier.dstQueueFamilyIndex = currentQueueFamily;
                            acquireBarrier.image               = img;
                            acquireBarrier.subresourceRange    = { getAspectMask(physImg->getDesc().format), 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
                            physiaclNode.imageBarriers.push_back(acquireBarrier);
                            // Acquire 後にレイアウト遷移が必要な場合、現在のキューで追加のバリアを発行
                            if (prevTrack.state.layout != next.layout) {
                                VkImageMemoryBarrier2 layoutBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                                layoutBarrier.srcStageMask  = next.stageMask; 
                                layoutBarrier.srcAccessMask = next.accessMask;
                                layoutBarrier.dstStageMask  = next.stageMask;
                                layoutBarrier.dstAccessMask = next.accessMask;
                                layoutBarrier.oldLayout     = prevTrack.state.layout;
                                layoutBarrier.newLayout     = next.layout;
                                layoutBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                layoutBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                layoutBarrier.image         = img;
                                layoutBarrier.subresourceRange = { getAspectMask(physImg->getDesc().format), 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
                                physiaclNode.imageBarriers.push_back(layoutBarrier);
                            }
                        } else {
                            VkBuffer buf = m_resourceAllocator.getPhysicalBuffer(h)->getNativeBuffer();
                            
                            // Buffer の Release バリア
                            VkBufferMemoryBarrier2 releaseBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                            releaseBarrier.srcStageMask        = prevTrack.state.stageMask;
                            releaseBarrier.srcAccessMask       = prevTrack.state.accessMask;
                            releaseBarrier.dstStageMask        = VK_PIPELINE_STAGE_2_NONE;
                            releaseBarrier.dstAccessMask       = VK_ACCESS_2_NONE;
                            releaseBarrier.srcQueueFamilyIndex = prevQueueFamily;
                            releaseBarrier.dstQueueFamilyIndex = currentQueueFamily;
                            releaseBarrier.buffer              = buf;
                            releaseBarrier.offset              = 0;
                            releaseBarrier.size                = VK_WHOLE_SIZE;
                            m_physicalNodes[prevTrack.lastPassIdx].bufferBarriers.push_back(releaseBarrier);

                            // Buffer の Acquire バリア
                            VkBufferMemoryBarrier2 acquireBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                            acquireBarrier.srcStageMask        = VK_PIPELINE_STAGE_2_NONE;
                            acquireBarrier.srcAccessMask       = VK_ACCESS_2_NONE;
                            acquireBarrier.dstStageMask        = next.stageMask;
                            acquireBarrier.dstAccessMask       = next.accessMask;
                            acquireBarrier.srcQueueFamilyIndex = prevQueueFamily;
                            acquireBarrier.dstQueueFamilyIndex = currentQueueFamily;
                            acquireBarrier.buffer              = buf;
                            acquireBarrier.offset              = 0;
                            acquireBarrier.size                = VK_WHOLE_SIZE;
                            physiaclNode.bufferBarriers.push_back(acquireBarrier);
                        }
                    } else {
                        // 同一キュー内での通常のパイプラインバリアおよびレイアウト遷移
                        if (prevTrack.state.layout != next.layout || (next.accessMask & VK_ACCESS_2_SHADER_WRITE_BIT)) {
                            if (m_resourceRegistry[h].isImage()) {
                                auto physImg = m_resourceAllocator.getPhysicalImage(h);
                                VkImageMemoryBarrier2 imgBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                                imgBarrier.srcStageMask  = prevTrack.state.stageMask; imgBarrier.srcAccessMask = prevTrack.state.accessMask;
                                imgBarrier.dstStageMask  = next.stageMask; imgBarrier.dstAccessMask = next.accessMask;
                                imgBarrier.oldLayout     = prevTrack.state.layout;    imgBarrier.newLayout     = next.layout;
                                imgBarrier.image         = physImg->getImage();
                                imgBarrier.subresourceRange = { getAspectMask(physImg->getDesc().format), 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
                                physiaclNode.imageBarriers.push_back(imgBarrier);
                            } else {
                                VkBufferMemoryBarrier2 bufBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                                bufBarrier.srcStageMask  = prevTrack.state.stageMask; bufBarrier.srcAccessMask = prevTrack.state.accessMask;
                                bufBarrier.dstStageMask  = next.stageMask; bufBarrier.dstAccessMask = next.accessMask;
                                bufBarrier.buffer        = m_resourceAllocator.getPhysicalBuffer(h)->getNativeBuffer();
                                bufBarrier.offset        = 0;              bufBarrier.size          = VK_WHOLE_SIZE;
                                physiaclNode.bufferBarriers.push_back(bufBarrier);
                            }
                        }
                    }
                } else {
                    // 初回アクセス（Undefined からの遷移）
                    VulkanResourceState prev = VulkanResourceState{ VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED };
                    if (m_resourceRegistry[h].isImage()) {
                        VkImageMemoryBarrier2 imgBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                        imgBarrier.srcStageMask  = prev.stageMask; imgBarrier.srcAccessMask = prev.accessMask;
                        imgBarrier.dstStageMask  = next.stageMask; imgBarrier.dstAccessMask = next.accessMask;
                        imgBarrier.oldLayout     = prev.layout;    imgBarrier.newLayout     = next.layout;
                        imgBarrier.image         = m_resourceAllocator.getPhysicalImage(h)->getImage();
                        imgBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
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
                // リソース追跡状態の更新
                currentStates[h] = ResourceTrackingState{ passIdx, next, logicalNode.queueType };
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
                    VkSemaphore sem = m_device.requestSemaphore();
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
        // UploadManagerから非同期アップロード完了待ちのセマフォを回収
        auto asyncSems = m_device.getUploadManager()->consumeAsyncSemaphores();
        std::vector<SemaphoreHandle> combinedWaitSems = waitSemaphores;
        combinedWaitSems.insert(combinedWaitSems.end(), asyncSems.begin(), asyncSems.end());

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
                        rhi::ResourceHandle hSrc = ds.resourceOffsets[0];
                        rhi::ResourceHandle hDst = ds.resourceOffsets[1];
                        
                        bool srcIsImage = m_resourceRegistry[hSrc].isImage();
                        bool dstIsImage = m_resourceRegistry[hDst].isImage();

                        if (srcIsImage && !dstIsImage) { // Image から Buffer へのコピー
                            VulkanImage* physSrcImg = m_resourceAllocator.getPhysicalImage(hSrc);
                            VulkanBuffer* physDstBuf = static_cast<VulkanBuffer*>(m_resourceAllocator.getPhysicalBuffer(hDst));
                            VkBufferImageCopy region{};
                            region.bufferOffset = ds.z; // stagingOffset 等として使用
                            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                            region.imageExtent = { physSrcImg->getDesc().width, physSrcImg->getDesc().height, 1 };
                            vkCmdCopyImageToBuffer(vkCmdBuf, physSrcImg->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, physDstBuf->getNativeBuffer(), 1, &region);
                        } 
                        else if (!srcIsImage && dstIsImage) { // Buffer -> Image
                            VulkanImage* physDstImg = m_resourceAllocator.getPhysicalImage(hDst);
                            VulkanBuffer* physSrcBuf = static_cast<VulkanBuffer*>(m_resourceAllocator.getPhysicalBuffer(hSrc));
                            VkBufferImageCopy region{};
                            region.bufferOffset = ds.z; 
                            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                            region.imageExtent = { physDstImg->getDesc().width, physDstImg->getDesc().height, 1 };
                            vkCmdCopyBufferToImage(vkCmdBuf, physSrcBuf->getNativeBuffer(), physDstImg->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                        } 
                        else if (!srcIsImage && !dstIsImage) { // Buffer -> Buffer
                            rhi::Buffer* physSrc = m_resourceAllocator.getPhysicalBuffer(hSrc);
                            rhi::Buffer* physDst = m_resourceAllocator.getPhysicalBuffer(hDst);
                            batch.cmdList->copyBuffer(physSrc, physDst, ds.x, ds.y, 0);
                        }
                    }
                }
                else if (logicalNode.type == PassType::Compute) {
                    if (!logicalNode.shaderPath.empty()) {
                        if (physiaclNode.computePipeline) {
                            batch.cmdList->bindPipeline(*physiaclNode.computePipeline);
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
                }else if (logicalNode.type == PassType::GenerateMipmaps) {
                    for (rhi::ResourceHandle hImg : logicalNode.resourceHandles) {
                        VulkanImage* physImage = m_resourceAllocator.getPhysicalImage(hImg);
                        if (physImage) {// todo computeシェーダでのミップマップ生成
                            physImage->recordMipmapGenerationCmds(vkCmdBuf);
                        }
                    }
                }else if (logicalNode.type == PassType::Graphics) {
                    std::string pipeKey = logicalNode.vertShaderPath + "|" + logicalNode.fragShaderPath;
                    auto* pipeline = physiaclNode.graphicsPipeline;
                    auto* vkCmd = static_cast<VulkanCommandList*>(batch.cmdList.get());
                    VkCommandBuffer cmdBuf = vkCmd->getCommandBuffer();

                    if (pipeline) {
                        std::vector<VulkanCommandList::RenderAttachment> vkColorAtts;
                        std::optional<VulkanCommandList::RenderAttachment> vkDepthAtt = std::nullopt;
                        uint32_t width = 0, height = 0;
                        // 1. カラーアタッチメントの構築
                        std::vector<LogicalPass::ColorAttachmentInfo> sortedAtts = logicalNode.colorAttachments;
                        // location順にソートして、シェーダーの layout(location = X) と一致させる
                        std::sort(sortedAtts.begin(), sortedAtts.end(), [](const auto& a, const auto& b){ return a.location < b.location; });
                        for (const auto& att : sortedAtts) {
                            VulkanImage* img = m_resourceAllocator.getPhysicalImage(att.handle);
                            // 最初の有効なアタッチメントから解像度を取得
                            if (width == 0) { 
                                width = img->getDesc().width; 
                                height = img->getDesc().height; 
                            }
                            VkClearValue clearVal{};
                            clearVal.color = {{att.clearValue.r, att.clearValue.g, att.clearValue.b, att.clearValue.a}};
                            vkColorAtts.push_back({
                                img->getView(),
                                mapLoadOp(att.loadOp),
                                mapStoreOp(att.storeOp),
                                clearVal
                            });
                        }
                        // 2. 深度アタッチメントの構築処理
                        if (logicalNode.depthAttachment.has_value()) {
                            const auto& depthInfo = logicalNode.depthAttachment.value();
                            VulkanImage* depthImg = m_resourceAllocator.getPhysicalImage(depthInfo.handle);
                            // Z-Prepass等、カラー出力がなく深度出力のみのパスの場合にここで解像度を取得する
                            if (width == 0) { 
                                width = depthImg->getDesc().width; 
                                height = depthImg->getDesc().height; 
                            }
                            VkClearValue clearVal{};
                            // DepthClearValueから深度とステンシルを設定
                            clearVal.depthStencil = { depthInfo.clearValue.depth, depthInfo.clearValue.stencil };
                            vkDepthAtt = {
                                depthImg->getView(),
                                mapLoadOp(depthInfo.loadOp),
                                mapStoreOp(depthInfo.storeOp),
                                clearVal
                            };
                        }
                        // 解像度が0の場合は描画領域がないのでスキップするかエラーにする
                        if (width == 0 || height == 0) {
                            throw std::runtime_error("Render target resolution is 0x0");
                            continue;
                        }
                        // 3. レンダリングの開始 (Dynamic Rendering)
                        const rhi::vk::VulkanCommandList::RenderAttachment* pDepthAtt = vkDepthAtt.has_value() ? &vkDepthAtt.value() : nullptr;
                        vkCmd->beginRendering(vkColorAtts, pDepthAtt, width, height);

                        // パイプラインとディスクリプタのバインド
                        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());
                        VkDescriptorSet globalSet = m_device.getBindlessDescriptorSet();
                        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(), 0, 1, &globalSet, 0, nullptr);
                        // Dynamic State の適用
                        vkCmd->setTopology(mapTopology(logicalNode.graphicsState.topology));
                        vkCmd->setCullMode(mapCullMode(logicalNode.graphicsState.cullMode));
                        vkCmd->setFrontFace(mapFrontFace(logicalNode.graphicsState.frontFace));
                        vkCmd->setDepthTestEnable(logicalNode.graphicsState.depthTestEnable);
                        vkCmd->setDepthWriteEnable(logicalNode.graphicsState.depthWriteEnable);
                        vkCmd->setDepthCompareOp(mapCompareOp(logicalNode.graphicsState.depthCompareOp));
                        // Drawの発行
                        for (auto& state : logicalNode.dispatchStates) {
                            // todo Computeと同じ処理を共通化
                            std::array<uint8_t, MAX_PUSH_CONSTANT_SIZE> finalPushData = state.pushData;
                            uint32_t finalPushSize = state.pushDataSize;
                            for (auto const& [offset, handle] : state.resourceOffsets) {
                                if(offset == 0xFFFFFFFE || offset == 0xFFFFFFFF) continue;  // 内部管理用ダミーオフセットはスキップ
                                uint32_t bindlessIndex = getPhysicalIndex(handle);
                                if (offset + 4 <= MAX_PUSH_CONSTANT_SIZE) {
                                    std::memcpy(finalPushData.data() + offset, &bindlessIndex, 4);
                                    finalPushSize = std::max(finalPushSize, offset + 4);
                                }
                            }
                            if (finalPushSize > 0) {
                                vkCmdPushConstants(cmdBuf, pipeline->getPipelineLayout(),
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                    0, finalPushSize, finalPushData.data()
                                );
                            }

                            // Draw (Indirect または Direct)
                            if (state.isIndirect) {
                                VkBuffer indirectBuf = m_resourceAllocator.getPhysicalBuffer(state.indirectBuffer)->getNativeBuffer();
                                VkBuffer countBuf = m_resourceAllocator.getPhysicalBuffer(state.countBuffer)->getNativeBuffer();
                                vkCmd->drawIndexedIndirectCount(indirectBuf, state.indirectOffset, countBuf, state.countOffset, state.maxDrawCount);
                            } else {
                                vkCmd->draw(state.vertexCount, state.instanceCount, state.firstVertex, state.firstInstance);
                            }
                        }

                        // 5. レンダリングの終了
                        vkCmd->endRendering();
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

            if (batchIdx == 0 && !combinedWaitSems.empty()) {
                for (auto semHandle : combinedWaitSems) {
                    currentWaitSemaphores.push_back(static_cast<VkSemaphore>(semHandle));
                    currentWaitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); 
                }
            }
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