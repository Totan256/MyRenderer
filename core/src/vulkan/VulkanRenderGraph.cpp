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
        // void CollectRequirements(
        //     rhi::RenderPass* pass,
        //     const std::map<uint32_t, rhi::ResourceHandle>& resourceOffsets,
        //     std::set<rhi::ResourceHandle>& seenHandles,
        //     rhi::ShaderStage stage) 
        // {
        //     const auto& signature = pass->getSignature();
        //     for (const auto& [offset, handle] : resourceOffsets) {
        //         auto it = signature.find(offset);
        //         if (it != signature.end()) {
        //             if (seenHandles.insert(handle).second) {
        //                 pass->addRequirement(handle, it->second, stage);
        //             }
        //         }
        //     }
        // }

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
    protected:
        void compile(Device& device) override {
            auto& vkDevice = static_cast<VulkanDevice&>(device);
            m_pipeline = vkDevice.getPipelineCacheManager().getOrCreateComputePipeline(m_shaderPath, MAX_PUSH_CONSTANT_SIZE);
            std::set<ResourceHandle> seenHandles;
            for (const auto& dsp : m_dispatches) {
                CollectRequirements(dsp.m_resourceOffsets, seenHandles, ShaderStage::Compute);
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
    protected:
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
                CollectRequirements(draw.m_resourceOffsets, seenHandles, ShaderStage::AllGraphics);
                auto state = draw.getState();
                if (state.isIndirect) {
                    addRequirement(state.indirectBuffer, ResourceState::StorageRead, ShaderStage::DrawIndirect);
                    addRequirement(state.countBuffer, ResourceState::StorageRead, ShaderStage::DrawIndirect);
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
                for (auto& draw : m_draws) {
                    auto pack = BuildPushConstants(vkGraph, vkDevice, draw.m_resourceOffsets, draw.m_dynamicUniforms);
                    if (pack.size > 0) {
                        vkCmdPushConstants(cmdBuf, m_pipeline->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, pack.size, pack.data.data());
                    }
                    auto state = draw.getState();
                    if (state.isIndirect) {
                        VkBuffer indirectBuf = allocator.getPhysicalBuffer(state.indirectBuffer)->getNativeBuffer();
                        VkBuffer countBuf = allocator.getPhysicalBuffer(state.countBuffer)->getNativeBuffer();
                        vkCmd.drawIndexedIndirectCount(indirectBuf, state.indirectOffset, countBuf, state.countOffset, state.maxDrawCount);
                    } else {
                        vkCmd.draw(state.vertexCount, state.instanceCount, state.firstVertex, state.firstInstance);
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
        for (auto& batch : m_batches) {
            if (batch.commandList) {
                delete batch.commandList;
                batch.commandList = nullptr;
            }
        }
        m_batches.clear();
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
        
        m_swapchainSyncs.clear();   
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

        std::cout << "uploads: " << uploads.size() << std::endl;

        // 1. Collect Requirements
        for (auto& pass : m_passes) compilePass(pass.get(), m_device);

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

        std::cout << "Total Passes: " << m_passes.size() << std::endl;

        // 2. Sort & Allocate
        std::vector<uint32_t> sortedIndices = getSortPasses(passIndices);
        calculateLifetimes(sortedIndices);
        std::cout << "Sorted Pass Indices: ";
        m_resourceAllocator.allocate(m_device.getCurrentFrame(), m_resourceRegistry, m_resourceLifetimes);
        std::cout << "Resource Allocation Done." << std::endl;
        m_device.flushDescriptorUpdates();

        std::cout << "Sorted Pass Indices: ";

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
                currentBatch->commandList = new VulkanCommandList(m_device, pass->getQueueType()); // メモリリーク注意：今回はプロトタイピングのまま
                currentQueue = pass->getQueueType();

                // 新しいバッチの SignalSyncPoint を発行
                currentBatch->signalSyncPoint = m_device.advanceTimeline(currentQueue);
            }
            currentBatch->passIndices.push_back(passIdx);
            passToBatch[passIdx] = m_batches.size() - 1;
        }

        std::cout << "Total Passes: " << m_passes.size() << ", Total Batches: " << m_batches.size() << std::endl;

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
                                imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                imgBarrier.srcStageMask = prevTrack.state.stageMask; imgBarrier.srcAccessMask = prevTrack.state.accessMask;
                                imgBarrier.dstStageMask = next.stageMask; imgBarrier.dstAccessMask = next.accessMask;
                                imgBarrier.oldLayout = prevTrack.state.layout; imgBarrier.newLayout = next.layout;
                                imgBarrier.image = physImg->getImage();
                                imgBarrier.subresourceRange = { getAspectMask(physImg->getDesc().format), 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
                                m_batches[currentBatchIdx].imageBarriers.push_back(imgBarrier);
                            } else {
                                VkBufferMemoryBarrier2 bufBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                                bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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
                    
                    if (m_resourceRegistry[h].isImported && m_resourceRegistry[h].physicalResource != nullptr) {
                        rhi::ResourceState currState = m_resourceRegistry[h].physicalResource->getCurrentState();
                        rhi::ShaderStage currStage = m_resourceRegistry[h].physicalResource->getCurrentStage();
                        prev = MapResourceState(currState, currStage);
                    }

                    if (m_resourceRegistry[h].isImage()) {
                        VkImageMemoryBarrier2 imgBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                        imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        imgBarrier.srcStageMask = prev.stageMask; imgBarrier.srcAccessMask = prev.accessMask;
                        imgBarrier.dstStageMask = next.stageMask; imgBarrier.dstAccessMask = next.accessMask;
                        imgBarrier.oldLayout = prev.layout; imgBarrier.newLayout = next.layout;
                        imgBarrier.image = m_resourceAllocator.getPhysicalImage(h)->getImage();
                        imgBarrier.subresourceRange = { 
                            getAspectMask(m_resourceAllocator.getPhysicalImage(h)->getDesc().format), 
                            0, VK_REMAINING_MIP_LEVELS, 0, 1 
                        };
                        m_batches[currentBatchIdx].imageBarriers.push_back(imgBarrier);
                    } else {
                        VkBufferMemoryBarrier2 bufBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                        bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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
        std::cout << "Barrier Insertion Done." << std::endl;
        // --- SyncPoint Tracking ---
        struct ResourceSyncState {
            SyncPoint writeSync = {QueueType::Compute, 0};
            std::vector<SyncPoint> readSyncs;
        };
        std::map<rhi::ResourceHandle, ResourceSyncState> currentSyncStates;

        // Initialize from imported physical resources
        for (size_t i = 0; i < m_resourceRegistry.size(); ++i) {
            const auto& reg = m_resourceRegistry[i];
            if (reg.isImported && reg.physicalResource) {
                currentSyncStates[i].writeSync = reg.physicalResource->getWriteSync();
                currentSyncStates[i].readSyncs = reg.physicalResource->getReadSyncs();
            }
        }

        auto addWaitSync = [](std::vector<SyncPoint>& waits, SyncPoint sp) {
            if (sp.value == 0) return;
            for (auto& w : waits) {
                if (w.queueType == sp.queueType) {
                    w.value = std::max(w.value, sp.value);
                    return;
                }
            }
            waits.push_back(sp);
        };

        for (uint32_t passIdx : sortedIndices) {
            auto& pass = m_passes[passIdx];
            uint32_t batchIdx = passToBatch[passIdx];
            RenderBatch& batch = m_batches[batchIdx];

            for (size_t i = 0; i < pass->getResourceHandles().size(); ++i) {
                rhi::ResourceHandle h = pass->getResourceHandles()[i];
                const auto& req = pass->getRequirements()[i];
                auto& syncState = currentSyncStates[h];

                if (isWriteUsage(req.state)) {
                    if (syncState.writeSync.value > 0 && syncState.writeSync.queueType != batch.queueType) {
                        addWaitSync(batch.waitSyncPoints, syncState.writeSync);
                    }
                    for (const auto& rs : syncState.readSyncs) {
                        if (rs.value > 0 && rs.queueType != batch.queueType) {
                            addWaitSync(batch.waitSyncPoints, rs);
                        }
                    }
                    syncState.writeSync = batch.signalSyncPoint;
                    syncState.readSyncs.clear();
                } else {
                    if (syncState.writeSync.value > 0 && syncState.writeSync.queueType != batch.queueType) {
                        addWaitSync(batch.waitSyncPoints, syncState.writeSync);
                    }
                    bool found = false;
                    for (auto& rs : syncState.readSyncs) {
                        if (rs.queueType == batch.queueType) {
                            rs.value = std::max(rs.value, batch.signalSyncPoint.value);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        syncState.readSyncs.push_back(batch.signalSyncPoint);
                    }
                }
            }
        }

        std::cout << "Final SyncStates Applied." << std::endl;
        // Apply final SyncStates back to physical resources
        for (auto& [h, syncState] : currentSyncStates) {
            const auto& reg = m_resourceRegistry[h];
            if (reg.isImported && reg.physicalResource) {
                reg.physicalResource->setWriteSync(syncState.writeSync);
                reg.physicalResource->clearReadSyncs();
                for (const auto& rs : syncState.readSyncs) {
                    reg.physicalResource->addReadSync(rs);
                }
            }
        }
        std::cout << "Physical Resource SyncStates Updated." << std::endl;
        for (const auto& [h, track] : currentStates) {
            const auto& reg = m_resourceRegistry[h];
            if (reg.isImage() && reg.isImported && reg.physicalResource) {
                VulkanImage* physImg = static_cast<VulkanImage*>(reg.physicalResource);
                if (physImg->isSwapchainImage()) {
                    rhi::Swapchain* swapchain = physImg->getSwapchain();                    
                    uint32_t firstPassIdx = m_resourceLifetimes[h].firstPass;
                    uint32_t firstBatchIdx = passToBatch[firstPassIdx];
                    uint32_t lastBatchIdx = passToBatch[track.lastPassIdx];                    
                    m_swapchainSyncs.push_back({swapchain, firstBatchIdx, lastBatchIdx});
                    VkImageMemoryBarrier2 presentBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                    presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    presentBarrier.srcStageMask = track.state.stageMask;
                    presentBarrier.srcAccessMask = track.state.accessMask;
                    presentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE; 
                    presentBarrier.dstAccessMask = VK_ACCESS_2_NONE;
                    presentBarrier.oldLayout = track.state.layout;
                    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    presentBarrier.image = physImg->getImage();
                    presentBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    
                    m_batches[lastBatchIdx].postImageBarriers.push_back(presentBarrier);
                }
            }
        }
        m_sortedIndices = sortedIndices;
    }

    void VulkanRenderGraph::execute(const std::vector<SemaphoreHandle>& waitSemaphores) {
        std::cout << "Executing RenderGraph..." << std::endl;
        auto asyncSems = m_device.getUploadManager()->consumeAsyncSyncPoints();
        std::vector<SyncPoint> combinedAsyncWaits = asyncSems;

        // --- バッチサブミット用の構造体 (vkQueueSubmit2 準拠) ---
        struct SubmitBatch {
            VkQueue queue = VK_NULL_HANDLE;
            std::vector<VkSubmitInfo2> submits;
            VkFence fence = VK_NULL_HANDLE;
            
            // VkSubmitInfo2が指すポインタの寿命をこの構造体内で管理する
            std::vector<std::vector<VkSemaphoreSubmitInfo>> waitSemaphoresList;
            std::vector<std::vector<VkSemaphoreSubmitInfo>> signalSemaphoresList;
            std::vector<VkCommandBufferSubmitInfo> cmdBufferInfosList;
        };

        std::vector<SubmitBatch> submitBatches;
        SubmitBatch currentSubmitBatch;

        for (size_t batchIdx = 0; batchIdx < m_batches.size(); ++batchIdx) {
            auto& batch = m_batches[batchIdx];
            batch.commandList->reset();
            batch.commandList->begin();
            auto vkCmdBuf = static_cast<VulkanCommandList*>(batch.commandList)->getCommandBuffer();

            if (!batch.imageBarriers.empty() || !batch.bufferBarriers.empty()) {
                VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                depInfo.imageMemoryBarrierCount = (uint32_t)batch.imageBarriers.size();
                depInfo.pImageMemoryBarriers    = batch.imageBarriers.data();
                depInfo.bufferMemoryBarrierCount = (uint32_t)batch.bufferBarriers.size();
                depInfo.pBufferMemoryBarriers    = batch.bufferBarriers.data();
                vkCmdPipelineBarrier2(vkCmdBuf, &depInfo);
            }

            for (uint32_t passIdx : batch.passIndices) {
                executePass(m_passes[passIdx].get(), *batch.commandList);
            }

            // パス実行後のバリア (Presentへの遷移など) を発行
            if (!batch.postImageBarriers.empty()) {
                VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                depInfo.imageMemoryBarrierCount = (uint32_t)batch.postImageBarriers.size();
                depInfo.pImageMemoryBarriers    = batch.postImageBarriers.data();
                vkCmdPipelineBarrier2(vkCmdBuf, &depInfo);
            }

            batch.commandList->end();

            // キューの切り替え判定
            VkQueue queue = m_device.getQueue(batch.queueType);
            if (currentSubmitBatch.queue != queue && currentSubmitBatch.queue != VK_NULL_HANDLE) {
                submitBatches.push_back(currentSubmitBatch);
                currentSubmitBatch = SubmitBatch{};
            }
            currentSubmitBatch.queue = queue;

            std::vector<VkSemaphoreSubmitInfo> currentWaitInfos;
            std::vector<VkSemaphoreSubmitInfo> currentSignalInfos;

            // --- タイムラインセマフォからの Wait 構築 ---
            for (const auto& sp : batch.waitSyncPoints) {
                VkSemaphore sem = m_device.getTimelineSemaphore(sp.queueType);
                if (sem != VK_NULL_HANDLE && sp.value > 0) {
                    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                    waitInfo.semaphore = sem;
                    waitInfo.value = sp.value;
                    waitInfo.stageMask = getWaitStageForQueue(batch.queueType);
                    currentWaitInfos.push_back(waitInfo);
                }
            }

            if (batchIdx == 0) {
                for (auto semHandle : waitSemaphores) {
                    VkSemaphore sem = static_cast<VkSemaphore>(semHandle);
                    if (sem != VK_NULL_HANDLE) {
                        VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                        waitInfo.semaphore = sem;
                        waitInfo.value = 0; // バイナリセマフォ
                        waitInfo.stageMask = getWaitStageForQueue(batch.queueType);
                        currentWaitInfos.push_back(waitInfo);
                    }
                }
                for (auto sp : combinedAsyncWaits) {
                    VkSemaphore sem = m_device.getTimelineSemaphore(sp.queueType);
                    if (sem != VK_NULL_HANDLE && sp.value > 0) {
                        VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                        waitInfo.semaphore = sem;
                        waitInfo.value = sp.value;
                        waitInfo.stageMask = getWaitStageForQueue(batch.queueType);
                        currentWaitInfos.push_back(waitInfo);
                    }
                }
            }

            // --- スワップチェーンセマフォからの Wait 構築 ---
            for (const auto& sync : m_swapchainSyncs) {
                if (sync.firstBatchIdx == batchIdx) {
                    VkSemaphore acquireSem = static_cast<VkSemaphore>(sync.swapchain->getCurrentAcquireSemaphore());
                    if (acquireSem != VK_NULL_HANDLE) {
                        VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                        waitInfo.semaphore = acquireSem;
                        waitInfo.value = 0;
                        waitInfo.stageMask = getWaitStageForQueue(batch.queueType);
                        currentWaitInfos.push_back(waitInfo);
                    }
                }
            }

            // --- タイムラインセマフォへの Signal 構築 ---
            VkSemaphore sigSem = m_device.getTimelineSemaphore(batch.signalSyncPoint.queueType);
            if (sigSem != VK_NULL_HANDLE) {
                VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                signalInfo.semaphore = sigSem;
                signalInfo.value = batch.signalSyncPoint.value;
                signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                currentSignalInfos.push_back(signalInfo);
            }

            // --- スワップチェーンセマフォへの Signal 構築 ---
            for (const auto& sync : m_swapchainSyncs) {
                if (sync.lastBatchIdx == batchIdx) {
                    VkSemaphore presentSem = static_cast<VkSemaphore>(sync.swapchain->getCurrentPresentSemaphore());
                    if (presentSem != VK_NULL_HANDLE) {
                        VkSemaphoreSubmitInfo pSignalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                        pSignalInfo.semaphore = presentSem;
                        pSignalInfo.value = 0;
                        pSignalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                        currentSignalInfos.push_back(pSignalInfo);
                    }
                }
            }

            VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
            cmdInfo.commandBuffer = vkCmdBuf;

            // サブミット情報の蓄積
            currentSubmitBatch.waitSemaphoresList.push_back(currentWaitInfos);
            currentSubmitBatch.signalSemaphoresList.push_back(currentSignalInfos);
            currentSubmitBatch.cmdBufferInfosList.push_back(cmdInfo);
            currentSubmitBatch.submits.push_back(VkSubmitInfo2{VK_STRUCTURE_TYPE_SUBMIT_INFO_2});

            if (batchIdx == m_batches.size() - 1) {
                currentSubmitBatch.fence = m_device.getCurrentFrameFence();
            }
        }

        if (currentSubmitBatch.queue != VK_NULL_HANDLE) {
            submitBatches.push_back(currentSubmitBatch);
        }
        
        std::cout << "Batch Preparation Done. Total Batches to Submit: " << submitBatches.size() + (currentSubmitBatch.queue != VK_NULL_HANDLE ? 1 : 0) << std::endl;

        // --- バッチサブミットの実行 ---
        for (auto& sb : submitBatches) {
            // vectorが再アロケートされないこのタイミングで、安全にポインタを設定する
            for(size_t i = 0; i < sb.submits.size(); ++i) {
                sb.submits[i].waitSemaphoreInfoCount = (uint32_t)sb.waitSemaphoresList[i].size();
                sb.submits[i].pWaitSemaphoreInfos = sb.waitSemaphoresList[i].empty() ? nullptr : sb.waitSemaphoresList[i].data();

                sb.submits[i].signalSemaphoreInfoCount = (uint32_t)sb.signalSemaphoresList[i].size();
                sb.submits[i].pSignalSemaphoreInfos = sb.signalSemaphoresList[i].empty() ? nullptr : sb.signalSemaphoresList[i].data();

                sb.submits[i].commandBufferInfoCount = 1;
                sb.submits[i].pCommandBufferInfos = &sb.cmdBufferInfosList[i];
            }
            // バッチごとに vkQueueSubmit2 にて実行
            VK_CHECK(vkQueueSubmit2(sb.queue, (uint32_t)sb.submits.size(), sb.submits.data(), sb.fence));
        }
    }
}