#include "VulkanRenderGraph.hpp"
#include "VulkanCommandList.hpp"
#include "VulkanComputePipeline.hpp"
#include "VulkanGraphicsPipeline.hpp"
#include "VulkanConstantBufferManager.hpp"
#include "VulkanCache.hpp"
#include "RHIconfig.hpp"
#include "VulkanSync.hpp"
#include <algorithm>
#include <numeric>
#include <iostream>
#include <set>

namespace rhi::vk {

    auto getAspectMask = [](rhi::Format format) -> VkImageAspectFlags {
        if (format == rhi::Format::D32_Sfloat || format == rhi::Format::D24_Unorm) return VK_IMAGE_ASPECT_DEPTH_BIT;
        return VK_IMAGE_ASPECT_COLOR_BIT;
    };

    auto getWaitStageForQueue = [](QueueType type) -> VkPipelineStageFlags {
        switch (type) {
            case QueueType::Graphics: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            case QueueType::Compute:  return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            case QueueType::Transfer: return VK_PIPELINE_STAGE_TRANSFER_BIT;
            default: return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }
    };

    namespace {
        void CollectRequirements(
            rhi::RenderPass* pass,
            const std::map<uint32_t, rhi::ResourceHandle>& resourceOffsets,
            std::set<rhi::ResourceHandle>& seenHandles,
            rhi::ShaderStage stage) 
        {
            const auto& signature = pass->getSignature();
            for (const auto& [offset, handle] : resourceOffsets) {
                auto it = signature.find(offset);
                if (it != signature.end()) {
                    if (seenHandles.insert(handle).second) {
                        pass->addRequirement(handle, it->second, stage);
                    }
                }
            }
        }

        struct PushConstantPack {
            std::array<uint8_t, MAX_PUSH_CONSTANT_SIZE> data{};
            uint32_t size = 0;
        };

        PushConstantPack BuildPushConstants(
            rhi::vk::VulkanRenderGraph& graph,
            rhi::vk::VulkanDevice& device,
            const std::map<uint32_t, rhi::ResourceHandle>& resourceOffsets,
            const std::map<uint32_t, std::vector<uint8_t>>& dynamicUniforms)
        {
            PushConstantPack pack{};
            
            for (auto const& [offset, handle] : resourceOffsets) {
                uint32_t bindlessIndex = graph.getPhysicalIndex(handle);
                if (offset + 4 <= MAX_PUSH_CONSTANT_SIZE) {
                    std::memcpy(pack.data.data() + offset, &bindlessIndex, 4);
                    pack.size = std::max(pack.size, (uint32_t)(offset + 4));
                }
            }
            for (auto const& [offset, dataVec] : dynamicUniforms) {
                auto allocation = device.getConstantBufferManager().allocateAndWrite(dataVec.data(), dataVec.size());
                if (offset + 8 <= MAX_PUSH_CONSTANT_SIZE) {
                    std::memcpy(pack.data.data() + offset, &allocation.index, 4);
                    std::memcpy(pack.data.data() + offset + 4, &allocation.offset, 4);
                    pack.size = std::max(pack.size, (uint32_t)(offset + 8));
                }
            }
            return pack;
        }
    } // anonymous namespace

    // === パスクラスの実装 ===
    class VulkanComputePass : public ComputePass {
    public:
        VulkanComputePass(const std::string& name, const std::string& shaderPath, QueueType qType, VulkanRenderGraph& graph)
            : ComputePass(name, shaderPath, qType, graph) {
            auto& device = static_cast<VulkanDevice&>(graph.getDevice());
            const auto& shaderData = device.getShaderCache().getOrCreateShader(shaderPath, shaderc_compute_shader);
            m_localSizeX = shaderData.reflection.localSizeX;
            m_localSizeY = shaderData.reflection.localSizeY;
            m_localSizeZ = shaderData.reflection.localSizeZ;
            m_pushConstantOffsets = shaderData.reflection.pushConstantOffsets;
        }
        void compile(Device& device) override {
            auto& vkDevice = static_cast<VulkanDevice&>(device);
            m_pipeline = vkDevice.getPipelineCacheManager().getOrCreateComputePipeline(m_shaderPath, MAX_PUSH_CONSTANT_SIZE);
            std::set<ResourceHandle> seenHandles;
            for (const auto& dsp : m_dispatches) {
                CollectRequirements(this, dsp.m_resourceOffsets, seenHandles, ShaderStage::Compute);
            }
        }
        void execute(CommandList& cmdList) override {
            auto& vkCmd = static_cast<VulkanCommandList&>(cmdList);
            if (m_pipeline) {
                vkCmd.bindPipeline(*m_pipeline);
                vkCmd.bindGlobalDescriptorSet();
            }
            auto& vkDevice = static_cast<VulkanDevice&>(m_graph.getDevice());
            auto& vkGraph = static_cast<VulkanRenderGraph&>(m_graph);
            for (auto& state : m_dispatches) {
                auto pack = BuildPushConstants(vkGraph, vkDevice, state.m_resourceOffsets, state.m_dynamicUniforms);
                if (pack.size > 0) vkCmd.setPushData(0, pack.size, pack.data.data());
                vkCmd.dispatch(state.m_x, state.m_y, state.m_z);
            }
        }
    private:
        VulkanComputePipeline* m_pipeline = nullptr;
    };

    class VulkanGraphicsPass : public GraphicsPass {
    public:
        VulkanGraphicsPass(const std::string& name, const std::string& vertPath, const std::string& fragPath, VulkanRenderGraph& graph)
            : GraphicsPass(name, vertPath, fragPath, graph) {
            auto& device = static_cast<VulkanDevice&>(graph.getDevice());
            const auto& vertData = device.getShaderCache().getOrCreateShader(vertPath, shaderc_vertex_shader);
            const auto& fragData = device.getShaderCache().getOrCreateShader(fragPath, shaderc_fragment_shader);
            m_pushConstantOffsets = vertData.reflection.pushConstantOffsets;
            m_outputLocations = fragData.reflection.outputLocations;
        }
        void compile(Device& device) override {
            auto& vkDevice = static_cast<VulkanDevice&>(device);
            auto& graph = static_cast<VulkanRenderGraph&>(m_graph);
            std::vector<VkFormat> vkColorFormats;
            for (const auto& att : m_colorAttachments) {
                const auto& reg = graph.getRegistration(att.handle);
                if (reg.isImported && reg.physicalResource && reg.physicalResource->isImage()) {
                    vkColorFormats.push_back(mapFormat(static_cast<VulkanImage*>(reg.physicalResource)->getDesc().format));
                } else if (std::holds_alternative<rhi::ImageDesc>(reg.desc)) {
                    vkColorFormats.push_back(mapFormat(std::get<rhi::ImageDesc>(reg.desc).format));
                }
                addRequirement(att.handle, ResourceState::ColorAttachment, ShaderStage::Fragment);
            }
            VkFormat vkDepthFormat = VK_FORMAT_UNDEFINED;
            if (m_depthAttachment.has_value()) {
                const auto& reg = graph.getRegistration(m_depthAttachment->handle);
                if (reg.isImported && reg.physicalResource && reg.physicalResource->isImage()) {
                    vkDepthFormat = mapFormat(static_cast<VulkanImage*>(reg.physicalResource)->getDesc().format);
                } else if (std::holds_alternative<rhi::ImageDesc>(reg.desc)) {
                    vkDepthFormat = mapFormat(std::get<rhi::ImageDesc>(reg.desc).format);
                }
                addRequirement(m_depthAttachment->handle, ResourceState::DepthStencilWrite, ShaderStage::Fragment);
            }
            m_pipeline = vkDevice.getPipelineCacheManager().getOrCreateGraphicsPipeline(
                m_vertShaderPath, m_fragShaderPath, vkColorFormats, vkDepthFormat, MAX_PUSH_CONSTANT_SIZE);
            std::set<ResourceHandle> seenHandles;
            for (const auto& draw : m_draws) {
                CollectRequirements(this, draw.m_resourceOffsets, seenHandles, ShaderStage::AllGraphics);
                if (draw.m_isIndirect) {
                    addRequirement(draw.m_indirectBuffer, ResourceState::StorageRead, ShaderStage::DrawIndirect);
                    addRequirement(draw.m_countBuffer, ResourceState::StorageRead, ShaderStage::DrawIndirect);
                }
            }
        }
        void execute(CommandList& cmdList) override {
            auto& vkCmd = static_cast<VulkanCommandList&>(cmdList);
            VkCommandBuffer cmdBuf = vkCmd.getCommandBuffer();
            auto& allocator = static_cast<VulkanRenderGraph&>(m_graph).getAllocator();
            if (m_pipeline) {
                std::vector<VulkanCommandList::RenderAttachment> vkColorAtts;
                std::optional<VulkanCommandList::RenderAttachment> vkDepthAtt = std::nullopt;
                uint32_t width = 0, height = 0;
                std::vector<ColorAttachmentInfo> sortedAtts = m_colorAttachments;
                std::sort(sortedAtts.begin(), sortedAtts.end(), [](const auto& a, const auto& b){ return a.location < b.location; });
                
                for (const auto& att : sortedAtts) {
                    VulkanImage* img = allocator.getPhysicalImage(att.handle);
                    if (width == 0) { width = img->getDesc().width; height = img->getDesc().height; }
                    VkClearValue clearVal{};
                    clearVal.color = {{att.clearValue.r, att.clearValue.g, att.clearValue.b, att.clearValue.a}};
                    vkColorAtts.push_back({ img->getView(), mapLoadOp(att.loadOp), mapStoreOp(att.storeOp), clearVal });
                }
                if (m_depthAttachment.has_value()) {
                    VulkanImage* depthImg = allocator.getPhysicalImage(m_depthAttachment->handle);
                    if (width == 0) { width = depthImg->getDesc().width; height = depthImg->getDesc().height; }
                    VkClearValue clearVal{};
                    clearVal.depthStencil = { m_depthAttachment->clearValue.depth, m_depthAttachment->clearValue.stencil };
                    vkDepthAtt = { depthImg->getView(), mapLoadOp(m_depthAttachment->loadOp), mapStoreOp(m_depthAttachment->storeOp), clearVal };
                }
                if (width == 0 || height == 0) return;
                const VulkanCommandList::RenderAttachment* pDepthAtt = vkDepthAtt.has_value() ? &vkDepthAtt.value() : nullptr;
                vkCmd.beginRendering(vkColorAtts, pDepthAtt, width, height);

                vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipeline());
                VkDescriptorSet globalSet = static_cast<VulkanDevice&>(m_graph.getDevice()).getBindlessDescriptorSet();
                vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getPipelineLayout(), 0, 1, &globalSet, 0, nullptr);
                vkCmd.setTopology(mapTopology(m_graphicsState.topology));
                vkCmd.setCullMode(mapCullMode(m_graphicsState.cullMode));
                vkCmd.setFrontFace(mapFrontFace(m_graphicsState.frontFace));
                vkCmd.setDepthTestEnable(m_graphicsState.depthTestEnable);
                vkCmd.setDepthWriteEnable(m_graphicsState.depthWriteEnable);
                vkCmd.setDepthCompareOp(mapCompareOp(m_graphicsState.depthCompareOp));

                auto& vkDevice = static_cast<VulkanDevice&>(m_graph.getDevice());
                auto& vkGraph = static_cast<VulkanRenderGraph&>(m_graph);
                for (auto& state : m_draws) {
                    auto pack = BuildPushConstants(vkGraph, vkDevice, state.m_resourceOffsets, state.m_dynamicUniforms);
                    if (pack.size > 0) {
                        vkCmdPushConstants(cmdBuf, m_pipeline->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, pack.size, pack.data.data());
                    }
                    if (state.m_isIndirect) {
                        VkBuffer indirectBuf = allocator.getPhysicalBuffer(state.m_indirectBuffer)->getNativeBuffer();
                        VkBuffer countBuf = allocator.getPhysicalBuffer(state.m_countBuffer)->getNativeBuffer();
                        vkCmd.drawIndexedIndirectCount(indirectBuf, state.m_indirectOffset, countBuf, state.m_countOffset, state.m_maxDrawCount);
                    } else {
                        vkCmd.draw(state.m_vertexCount, state.m_instanceCount, state.m_firstVertex, state.m_firstInstance);
                    }
                }
                vkCmd.endRendering();
            }
        }
    private:
        VulkanGraphicsPipeline* m_pipeline = nullptr;
    };

    class VulkanCopyPass : public CopyPass {
    public:
        VulkanCopyPass(const std::string& name, ResourceHandle src, ResourceHandle dst, size_t size, QueueType qType, VulkanRenderGraph& graph)
            : CopyPass(name, src, dst, size, qType, graph) {}
        void compile(Device& device) override {
            addRequirement(m_src, ResourceState::TransferSrc, ShaderStage::Transfer);
            addRequirement(m_dst, ResourceState::TransferDst, ShaderStage::Transfer);
        }
        void execute(CommandList& cmdList) override {
            auto& vkCmd = static_cast<VulkanCommandList&>(cmdList);
            VkCommandBuffer cmdBuf = vkCmd.getCommandBuffer();
            auto& allocator = static_cast<VulkanRenderGraph&>(m_graph).getAllocator();

            bool srcIsImage = m_graph.getRegistration(m_src).isImage();
            bool dstIsImage = m_graph.getRegistration(m_dst).isImage();

            if (srcIsImage && !dstIsImage) {
                VulkanImage* physSrcImg = allocator.getPhysicalImage(m_src);
                VulkanBuffer* physDstBuf = static_cast<VulkanBuffer*>(allocator.getPhysicalBuffer(m_dst));
                VkBufferImageCopy region{};
                region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                region.imageExtent = { physSrcImg->getDesc().width, physSrcImg->getDesc().height, 1 };
                vkCmdCopyImageToBuffer(cmdBuf, physSrcImg->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, physDstBuf->getNativeBuffer(), 1, &region);
            } 
            else if (!srcIsImage && dstIsImage) {
                VulkanImage* physDstImg = allocator.getPhysicalImage(m_dst);
                VulkanBuffer* physSrcBuf = static_cast<VulkanBuffer*>(allocator.getPhysicalBuffer(m_src));
                VkBufferImageCopy region{};
                region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                region.imageExtent = { physDstImg->getDesc().width, physDstImg->getDesc().height, 1 };
                vkCmdCopyBufferToImage(cmdBuf, physSrcBuf->getNativeBuffer(), physDstImg->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            } 
            else if (!srcIsImage && !dstIsImage) {
                rhi::Buffer* physSrc = allocator.getPhysicalBuffer(m_src);
                rhi::Buffer* physDst = allocator.getPhysicalBuffer(m_dst);
                vkCmd.copyBuffer(physSrc, physDst, m_size, 0, 0);
            }
        }
    };

    class VulkanMultiCopyPass : public RenderPass {
    public:
        VulkanMultiCopyPass(const std::string& name, RenderGraph& graph) : RenderPass(name, PassType::Copy, QueueType::Transfer, graph) {}
        void addCopy(ResourceHandle src, ResourceHandle dst, uint32_t w, uint32_t h, size_t offset) {
            m_copies.push_back({src, dst, w, h, offset});
        }
        void compile(Device& device) override {
            for(auto& c : m_copies) {
                addRequirement(c.src, ResourceState::TransferSrc, ShaderStage::Transfer);
                addRequirement(c.dst, ResourceState::TransferDst, ShaderStage::Transfer);
            }
        }
        void execute(CommandList& cmdList) override {
            auto& vkCmd = static_cast<VulkanCommandList&>(cmdList);
            auto& allocator = static_cast<VulkanRenderGraph&>(m_graph).getAllocator();
            for(auto& c : m_copies) {
                VulkanImage* physDstImg = allocator.getPhysicalImage(c.dst);
                VulkanBuffer* physSrcBuf = static_cast<VulkanBuffer*>(allocator.getPhysicalBuffer(c.src));
                VkBufferImageCopy region{};
                region.bufferOffset = c.offset;
                region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                region.imageExtent = { c.w, c.h, 1 };
                vkCmdCopyBufferToImage(vkCmd.getCommandBuffer(), physSrcBuf->getNativeBuffer(), physDstImg->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            }
        }
        struct CopyData { ResourceHandle src, dst; uint32_t w, h; size_t offset; };
        std::vector<CopyData> m_copies;
    };

    class VulkanMipmapPass : public RenderPass {
    public:
        VulkanMipmapPass(const std::string& name, RenderGraph& graph) : RenderPass(name, PassType::GenerateMipmaps, QueueType::Compute, graph) {}
        void addImage(ResourceHandle h) { m_images.push_back(h); }
        void compile(Device& device) override {
            for(auto h : m_images) addRequirement(h, ResourceState::StorageWrite, ShaderStage::Compute);
        }
        void execute(CommandList& cmdList) override {
            auto& vkCmd = static_cast<VulkanCommandList&>(cmdList);
            auto& allocator = static_cast<VulkanRenderGraph&>(m_graph).getAllocator();
            for(auto h : m_images) {
                VulkanImage* physImage = allocator.getPhysicalImage(h);
                if (physImage) physImage->recordMipmapGenerationCmds(vkCmd.getCommandBuffer());
            }
        }
        std::vector<ResourceHandle> m_images;
    };

    VulkanRenderGraph::~VulkanRenderGraph() {
        clearBatchSemaphores();
    }

    void VulkanRenderGraph::clearBatchSemaphores() {
        for (VkSemaphore sem : m_batchSemaphores) {
            if (sem != VK_NULL_HANDLE) {
                m_device.enqueueDeletion([device = &m_device, s = sem]() { device->releaseSemaphore(s); });
            }
        }
        m_batchSemaphores.clear();
    }

    ComputePass& VulkanRenderGraph::addComputePass(const std::string& name, const std::string& shaderPath, QueueType queueType) {
        m_passes.push_back(std::make_unique<VulkanComputePass>(name, shaderPath, queueType, *this));
        return *static_cast<ComputePass*>(m_passes.back().get());
    }

    GraphicsPass& VulkanRenderGraph::addGraphicsPass(const std::string& name, const std::string& vertShaderPath, const std::string& fragShaderPath) {
        m_passes.push_back(std::make_unique<VulkanGraphicsPass>(name, vertShaderPath, fragShaderPath, *this));
        return *static_cast<GraphicsPass*>(m_passes.back().get());
    }

    CopyPass& VulkanRenderGraph::addCopyPass(const std::string& name, ResourceHandle srcBuffer, ResourceHandle dstBuffer, size_t size, QueueType queueType) {
        m_passes.push_back(std::make_unique<VulkanCopyPass>(name, srcBuffer, dstBuffer, size, queueType, *this));
        return *static_cast<CopyPass*>(m_passes.back().get());
    }

    ResourceHandle VulkanRenderGraph::importResource(Resource* res, StringHash nameHash) {
        if (m_physicalToHandle.count(res)) return m_physicalToHandle[res];
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

    void VulkanRenderGraph::compile() {
        std::cout << "Compiling RenderGraph..." << std::endl;
        clearBatchSemaphores();
        
        m_device.flushDescriptorUpdates();

        // 0. Auto Upload Passes
        auto uploadManager = m_device.getUploadManager();
        auto uploads = uploadManager->getAndClearPendingUploads();
        auto imgUploads = uploadManager->getAndClearPendingImageUploads();
        
        if (!imgUploads.empty()) {
            auto mipPass = std::make_unique<VulkanMipmapPass>("AutoMipmapGen", *this);
            auto copyImgPass = std::make_unique<VulkanMultiCopyPass>("AutoImageUpload", *this);
            for (const auto& up : imgUploads) {
                ResourceHandle hStaging = importResource(up.stagingBuffer);
                ResourceHandle hDst = importResource(up.dstImage);
                copyImgPass->addCopy(hStaging, hDst, up.width, up.height, up.stagingOffset);
                mipPass->addImage(hDst);
            }
            m_passes.insert(m_passes.begin(), std::move(mipPass));
            m_passes.insert(m_passes.begin(), std::move(copyImgPass));
        }
        if (!uploads.empty()) {
            for (size_t i = 0; i < uploads.size(); ++i) {
                ResourceHandle hStaging = importResource(uploads[i].stagingBuffer);
                ResourceHandle hDst = importResource(uploads[i].dstBuffer);
                auto copyPass = std::make_unique<VulkanCopyPass>("AutoUpload", hStaging, hDst, uploads[i].size, rhi::QueueType::Transfer, *this);
                m_passes.insert(m_passes.begin(), std::move(copyPass));
            }
        }

        // 1. Collect Requirements
        for (auto& pass : m_passes) pass->compile(m_device);

        std::vector<uint32_t> passIndices(m_passes.size());
        std::iota(passIndices.begin(), passIndices.end(), 0);
        
        for (size_t i = 0; i < m_resourceRegistry.size(); ++i) {
            m_resourceRegistry[i].producers.clear(); m_resourceRegistry[i].consumers.clear();
        }
        for (uint32_t i = 0; i < m_passes.size(); ++i) {
            for (size_t j = 0; j < m_passes[i]->getResourceHandles().size(); ++j) {
                ResourceHandle h = m_passes[i]->getResourceHandles()[j];
                ResourceState state = m_passes[i]->getRequirements()[j].state;
                if (isWriteUsage(state)) m_resourceRegistry[h].producers.push_back(i);
                else m_resourceRegistry[h].consumers.push_back(i);
            }
        }

        // 2. Sort & Allocate
        std::vector<uint32_t> sortedIndices = getSortPasses(passIndices);
        calculateLifetimes(sortedIndices);
        m_resourceAllocator.allocate(m_resourceRegistry, m_resourceLifetimes);

        m_device.flushDescriptorUpdates();

        // 3. Batch Division
        m_batches.clear();
        QueueType currentQueue = QueueType::Compute; 
        RenderBatch* currentBatch = nullptr;
        std::vector<uint32_t> passToBatch(m_passes.size(), 0);

        for (uint32_t passIdx : sortedIndices) {
            auto& pass = m_passes[passIdx];
            bool shouldBreak = (currentBatch == nullptr) || (pass->getQueueType() != currentQueue) || (pass->isForceBatchBreak());
            if (shouldBreak) {
                m_batches.push_back({});
                currentBatch = &m_batches.back();
                currentBatch->queueType = pass->getQueueType();
                currentBatch->cmdList = std::make_unique<VulkanCommandList>(m_device, pass->getQueueType());
                currentQueue = pass->getQueueType();

                if (m_batches.size() > 1) {
                    RenderBatch& prevBatch = m_batches[m_batches.size() - 2];
                    VkSemaphore sem = m_device.requestSemaphore();
                    m_batchSemaphores.push_back(sem);
                    prevBatch.signalSemaphores.push_back(sem);
                    currentBatch->waitSemaphores.push_back(sem);
                    // 修正点3: 待機ステージの最適化
                    currentBatch->waitStages.push_back(getWaitStageForQueue(currentBatch->queueType)); 
                }
            }
            currentBatch->passIndices.push_back(passIdx);
            passToBatch[passIdx] = m_batches.size() - 1;
        }

        // 4. Barriers & Transitions
        struct ResourceTrackingState { 
            uint32_t lastPassIdx; 
            VulkanResourceState state; 
            QueueType queueType;
            rhi::ResourceState rhiState;
            rhi::ShaderStage rhiStage;
        };
        std::map<rhi::ResourceHandle, ResourceTrackingState> currentStates;
        
        for (uint32_t passIdx : sortedIndices) {
            auto& pass = m_passes[passIdx];
            uint32_t currentBatchIdx = passToBatch[passIdx];
            uint32_t currentQueueFamily = m_device.getQueueFamilyIndex(pass->getQueueType());
            
            for (size_t i = 0; i < pass->getResourceHandles().size(); ++i) {
                rhi::ResourceHandle h = pass->getResourceHandles()[i];
                const auto& req = pass->getRequirements()[i];
                VulkanResourceState next = MapResourceState(req.state, req.stage);
                
                if (currentStates.count(h)) {
                    auto& prevTrack = currentStates[h];
                    uint32_t prevQueueFamily = m_device.getQueueFamilyIndex(prevTrack.queueType);
                    uint32_t prevBatchIdx = passToBatch[prevTrack.lastPassIdx];
                    
                    if (prevQueueFamily != currentQueueFamily) {
                        if (m_resourceRegistry[h].isImage()) {
                            VulkanImage* physImg = m_resourceAllocator.getPhysicalImage(h);
                            VkImage img = physImg->getImage();
                            
                            VkImageMemoryBarrier2 releaseBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                            releaseBarrier.srcStageMask = prevTrack.state.stageMask; releaseBarrier.srcAccessMask = prevTrack.state.accessMask;
                            releaseBarrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE; releaseBarrier.dstAccessMask = VK_ACCESS_2_NONE;
                            releaseBarrier.oldLayout = prevTrack.state.layout; releaseBarrier.newLayout = prevTrack.state.layout;
                            releaseBarrier.srcQueueFamilyIndex = prevQueueFamily; releaseBarrier.dstQueueFamilyIndex = currentQueueFamily;
                            releaseBarrier.image = img;
                            releaseBarrier.subresourceRange = { getAspectMask(physImg->getDesc().format), 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
                            m_batches[prevBatchIdx].imageBarriers.push_back(releaseBarrier);
                            
                            VkImageMemoryBarrier2 acquireBarrier = releaseBarrier;
                            acquireBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE; acquireBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                            acquireBarrier.dstStageMask = next.stageMask; acquireBarrier.dstAccessMask = next.accessMask;
                            acquireBarrier.oldLayout = prevTrack.state.layout; 
                            acquireBarrier.newLayout = next.layout;
                            m_batches[currentBatchIdx].imageBarriers.push_back(acquireBarrier);
                            
                        } else {
                            VkBuffer buf = m_resourceAllocator.getPhysicalBuffer(h)->getNativeBuffer();
                            VkBufferMemoryBarrier2 releaseBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                            releaseBarrier.srcStageMask = prevTrack.state.stageMask; releaseBarrier.srcAccessMask = prevTrack.state.accessMask;
                            releaseBarrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE; releaseBarrier.dstAccessMask = VK_ACCESS_2_NONE;
                            releaseBarrier.srcQueueFamilyIndex = prevQueueFamily; releaseBarrier.dstQueueFamilyIndex = currentQueueFamily;
                            releaseBarrier.buffer = buf; releaseBarrier.offset = 0; releaseBarrier.size = VK_WHOLE_SIZE;
                            m_batches[prevBatchIdx].bufferBarriers.push_back(releaseBarrier);

                            VkBufferMemoryBarrier2 acquireBarrier = releaseBarrier;
                            acquireBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE; acquireBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                            acquireBarrier.dstStageMask = next.stageMask; acquireBarrier.dstAccessMask = next.accessMask;
                            m_batches[currentBatchIdx].bufferBarriers.push_back(acquireBarrier);
                        }
                    } else {
                        if (prevTrack.state.layout != next.layout || (next.accessMask & VK_ACCESS_2_SHADER_WRITE_BIT)) {
                            if (m_resourceRegistry[h].isImage()) {
                                auto physImg = m_resourceAllocator.getPhysicalImage(h);
                                VkImageMemoryBarrier2 imgBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                                imgBarrier.srcStageMask = prevTrack.state.stageMask; imgBarrier.srcAccessMask = prevTrack.state.accessMask;
                                imgBarrier.dstStageMask = next.stageMask; imgBarrier.dstAccessMask = next.accessMask;
                                imgBarrier.oldLayout = prevTrack.state.layout; imgBarrier.newLayout = next.layout;
                                imgBarrier.image = physImg->getImage();
                                imgBarrier.subresourceRange = { getAspectMask(physImg->getDesc().format), 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
                                m_batches[currentBatchIdx].imageBarriers.push_back(imgBarrier);
                            } else {
                                VkBufferMemoryBarrier2 bufBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                                bufBarrier.srcStageMask = prevTrack.state.stageMask; bufBarrier.srcAccessMask = prevTrack.state.accessMask;
                                bufBarrier.dstStageMask = next.stageMask; bufBarrier.dstAccessMask = next.accessMask;
                                bufBarrier.buffer = m_resourceAllocator.getPhysicalBuffer(h)->getNativeBuffer();
                                bufBarrier.offset = 0; bufBarrier.size = VK_WHOLE_SIZE;
                                m_batches[currentBatchIdx].bufferBarriers.push_back(bufBarrier);
                            }
                        }
                    }
                } else {
                    VulkanResourceState prev = VulkanResourceState{ VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED };
                    
                    // インポートされた物理リソースの場合、その現在の状態を取得して初期レイアウトとする
                    if (m_resourceRegistry[h].isImported && m_resourceRegistry[h].physicalResource != nullptr) {
                        rhi::ResourceState currState = m_resourceRegistry[h].physicalResource->getCurrentState();
                        rhi::ShaderStage currStage = m_resourceRegistry[h].physicalResource->getCurrentStage();
                        prev = MapResourceState(currState, currStage);
                    }

                    if (m_resourceRegistry[h].isImage()) {
                        VkImageMemoryBarrier2 imgBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                        imgBarrier.srcStageMask = prev.stageMask; imgBarrier.srcAccessMask = prev.accessMask;
                        imgBarrier.dstStageMask = next.stageMask; imgBarrier.dstAccessMask = next.accessMask;
                        imgBarrier.oldLayout = prev.layout; imgBarrier.newLayout = next.layout;
                        imgBarrier.image = m_resourceAllocator.getPhysicalImage(h)->getImage();
                        imgBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
                        m_batches[currentBatchIdx].imageBarriers.push_back(imgBarrier);
                    } else {
                        VkBufferMemoryBarrier2 bufBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                        bufBarrier.srcStageMask = prev.stageMask; bufBarrier.srcAccessMask = prev.accessMask;
                        bufBarrier.dstStageMask = next.stageMask; bufBarrier.dstAccessMask = next.accessMask;
                        bufBarrier.buffer = m_resourceAllocator.getPhysicalBuffer(h)->getNativeBuffer();
                        bufBarrier.offset = 0; bufBarrier.size = VK_WHOLE_SIZE;
                        m_batches[currentBatchIdx].bufferBarriers.push_back(bufBarrier);
                    }
                }
                currentStates[h] = ResourceTrackingState{ passIdx, next, pass->getQueueType(), req.state, req.stage };
            }
        }
        
        // グラフ実行完了後の最終状態を物理リソースに記録し、次フレームで正しく引き継げるようにする
        for (const auto& [h, track] : currentStates) {
            if (m_resourceRegistry[h].isImported && m_resourceRegistry[h].physicalResource != nullptr) {
                m_resourceRegistry[h].physicalResource->setState(track.rhiState, track.rhiStage);
            }
        }
        
        m_sortedIndices = sortedIndices;
    }

    void VulkanRenderGraph::execute(const std::vector<SemaphoreHandle>& waitSemaphores) {
        auto asyncSems = m_device.getUploadManager()->consumeAsyncSemaphores();
        std::vector<SemaphoreHandle> combinedWaitSems = waitSemaphores;
        combinedWaitSems.insert(combinedWaitSems.end(), asyncSems.begin(), asyncSems.end());

        // --- 修正点2: バッチサブミット用の構造体 ---
        struct SubmitBatch {
            VkQueue queue = VK_NULL_HANDLE;
            std::vector<VkSubmitInfo> submits;
            VkFence fence = VK_NULL_HANDLE;
            
            // VkSubmitInfoが指すポインタの寿命をこの構造体内で管理する
            std::vector<std::vector<VkSemaphore>> waitSemaphoresList;
            std::vector<std::vector<VkPipelineStageFlags>> waitStagesList;
            std::vector<std::vector<VkSemaphore>> signalSemaphoresList;
            std::vector<VkCommandBuffer> cmdBuffers;
        };

        std::vector<SubmitBatch> submitBatches;
        SubmitBatch currentSubmitBatch;

        for (size_t batchIdx = 0; batchIdx < m_batches.size(); ++batchIdx) {
            auto& batch = m_batches[batchIdx];
            batch.cmdList->reset();
            batch.cmdList->begin();
            auto vkCmdBuf = static_cast<VulkanCommandList*>(batch.cmdList.get())->getCommandBuffer();

            if (!batch.imageBarriers.empty() || !batch.bufferBarriers.empty()) {
                VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                depInfo.imageMemoryBarrierCount = (uint32_t)batch.imageBarriers.size();
                depInfo.pImageMemoryBarriers    = batch.imageBarriers.data();
                depInfo.bufferMemoryBarrierCount = (uint32_t)batch.bufferBarriers.size();
                depInfo.pBufferMemoryBarriers    = batch.bufferBarriers.data();
                vkCmdPipelineBarrier2(vkCmdBuf, &depInfo);
            }

            for (uint32_t passIdx : batch.passIndices) {
                m_passes[passIdx]->execute(*batch.cmdList);
            }

            batch.cmdList->end();

            // キューの切り替え判定
            VkQueue queue = m_device.getQueue(batch.queueType);
            if (currentSubmitBatch.queue != queue && currentSubmitBatch.queue != VK_NULL_HANDLE) {
                submitBatches.push_back(currentSubmitBatch);
                currentSubmitBatch = SubmitBatch{};
            }
            currentSubmitBatch.queue = queue;

            std::vector<VkSemaphore> currentWaitSemaphores = batch.waitSemaphores;
            std::vector<VkPipelineStageFlags> currentWaitStages = batch.waitStages;

            if (batchIdx == 0) {
                for (auto semHandle : combinedWaitSems) {
                    currentWaitSemaphores.push_back(static_cast<VkSemaphore>(semHandle));
                    currentWaitStages.push_back(getWaitStageForQueue(batch.queueType)); 
                }
            }

            // サブミット情報の蓄積
            currentSubmitBatch.waitSemaphoresList.push_back(currentWaitSemaphores);
            currentSubmitBatch.waitStagesList.push_back(currentWaitStages);
            currentSubmitBatch.signalSemaphoresList.push_back(batch.signalSemaphores);
            currentSubmitBatch.cmdBuffers.push_back(vkCmdBuf);
            currentSubmitBatch.submits.push_back(VkSubmitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO}); // ダミーをPush。後でポインタを張る

            if (batchIdx == m_batches.size() - 1) {
                currentSubmitBatch.fence = m_device.getCurrentFrameFence();
            }
        }

        if (currentSubmitBatch.queue != VK_NULL_HANDLE) {
            submitBatches.push_back(currentSubmitBatch);
        }

        // --- バッチサブミットの実行 ---
        for (auto& sb : submitBatches) {
            // vectorが再アロケートされないこのタイミングで、安全にポインタを設定する
            for(size_t i = 0; i < sb.submits.size(); ++i) {
                sb.submits[i].waitSemaphoreCount = (uint32_t)sb.waitSemaphoresList[i].size();
                sb.submits[i].pWaitSemaphores = sb.waitSemaphoresList[i].empty() ? nullptr : sb.waitSemaphoresList[i].data();
                sb.submits[i].pWaitDstStageMask = sb.waitStagesList[i].empty() ? nullptr : sb.waitStagesList[i].data();

                sb.submits[i].signalSemaphoreCount = (uint32_t)sb.signalSemaphoresList[i].size();
                sb.submits[i].pSignalSemaphores = sb.signalSemaphoresList[i].empty() ? nullptr : sb.signalSemaphoresList[i].data();

                sb.submits[i].commandBufferCount = 1;
                sb.submits[i].pCommandBuffers = &sb.cmdBuffers[i];
            }
            // キューごとに1回だけ実行
            VK_CHECK(vkQueueSubmit(sb.queue, (uint32_t)sb.submits.size(), sb.submits.data(), sb.fence));
        }
    }
}