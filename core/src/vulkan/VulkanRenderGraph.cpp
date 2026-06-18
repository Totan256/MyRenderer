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

    void BuildScopeAttachments(rhi::vk::RenderScope& scope, const rhi::GraphicsPass* pass) {
        scope.colorAtts.clear();
        scope.depthAtt = std::nullopt;

        // カラーアタッチメントの構築
        for (const auto& [location, att] : pass->getColorAttachments()) {
            VkClearValue clearVal{};
            clearVal.color = {{ att.clearValue.r, att.clearValue.g, att.clearValue.b, att.clearValue.a }};
            scope.colorAtts.push_back({ 
                att.handle, 
                mapLoadOp(att.loadOp), 
                mapStoreOp(att.storeOp), 
                clearVal 
            });
        }
        // 深度アタッチメントの構築
        if (pass->getDepthAttachment().has_value()) {
            const auto& depthAtt = pass->getDepthAttachment().value();
            VkClearValue clearVal{};
            clearVal.depthStencil = { depthAtt.clearValue.depth, depthAtt.clearValue.stencil };
            scope.depthAtt = rhi::vk::RenderScopeAttachment{ 
                depthAtt.handle, 
                mapLoadOp(depthAtt.loadOp), 
                mapStoreOp(depthAtt.storeOp), 
                clearVal 
            };
        }
    }

    namespace {
        

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
            for (const auto& [location, att] : m_colorAttachments) {
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
                
                // vkCmd.beginRendering(vkColorAtts, pDepthAtt, width, height);

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
                // vkCmd.endRendering();
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

            auto& srcReg = m_graph.getRegistration(m_src);
            bool srcIsImage = srcReg.isImported ? (srcReg.physicalResource && srcReg.physicalResource->isImage()) : srcReg.isImage();
            auto& dstReg = m_graph.getRegistration(m_dst);
            bool dstIsImage = dstReg.isImported ? (dstReg.physicalResource && dstReg.physicalResource->isImage()) : dstReg.isImage();

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

    VulkanRenderGraph::VulkanRenderGraph(VulkanDevice& device)  : m_device(device), m_resourceAllocator(device, MAX_FRAMES_IN_FLIGHT) {
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            for (QueueType type : {QueueType::Graphics, QueueType::Compute, QueueType::Transfer}) {
                VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                // TRANSIENT_BIT は頻繁にリセットされるプールに最適
                poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; 
                poolInfo.queueFamilyIndex = m_device.getQueueFamilyIndex(type);
                
                vkCreateCommandPool(m_device.getDevice(), &poolInfo, nullptr, &m_frameData[i].pools[type].pool);
            }
        }
    }

    VulkanRenderGraph::~VulkanRenderGraph() {
        clearBatchSemaphores();
        for (auto& frame : m_frameData) {
        for (auto& [type, data] : frame.pools) {
            data.commandLists.clear(); // VulkanCommandListのデストラクタが呼ばれる (m_ownsPool=falseなのでプール自体は消さない)
            if (data.pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(m_device.getDevice(), data.pool, nullptr);
            }
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

    void VulkanRenderGraph::bindPhysicalResource(ResourceHandle handle, Resource* res) {
        if (handle == InvalidResource || handle >= m_resourceRegistry.size()) {
            return;
        }

        // 1. レジストリ内の物理リソースポインタを更新
        auto& reg = m_resourceRegistry[handle];
        reg.physicalResource = res;

        // 2. アロケータが持つ物理ハンドルマップ（m_imageMap / m_bufferMap）を即座に更新
        m_resourceAllocator.bindPhysicalResource(handle, res);
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
        m_queueMaxOffsets.clear();
        QueueType currentQueue = QueueType::Compute; 
        RenderBatch* currentBatch = nullptr;
        RenderScope* currentScope = nullptr;
        std::vector<uint32_t> passToBatch(m_passes.size(), 0);

        auto isSameAttachmentConfig = [](GraphicsPass* a, GraphicsPass* b) {
            if (a->getColorAttachments().size() != b->getColorAttachments().size()) return false;
            for (const auto& [loc, attA] : a->getColorAttachments()) {
                auto it = b->getColorAttachments().find(loc);
                if (it == b->getColorAttachments().end() || it->second.handle != attA.handle) return false;
            }
            if (a->getDepthAttachment().has_value() != b->getDepthAttachment().has_value()) return false;
            if (a->getDepthAttachment().has_value()) {
                if (a->getDepthAttachment()->handle != b->getDepthAttachment()->handle) return false;
            }
            return true;
        };

        for (uint32_t passIdx : sortedIndices) {
            auto& pass = m_passes[passIdx];
            bool shouldBreakBatch = (currentBatch == nullptr) || (pass->getQueueType() != currentQueue) || pass->isForceBatchBreak();
            
            if (shouldBreakBatch) {
                m_batches.push_back({});
                currentBatch = &m_batches.back();
                currentBatch->queueType = pass->getQueueType();
                currentQueue = pass->getQueueType();
                m_queueMaxOffsets[currentQueue]++;
                currentBatch->relativeSignalOffset = m_queueMaxOffsets[currentQueue];
                currentScope = nullptr;
            }
            
            passToBatch[passIdx] = m_batches.size() - 1;

            // スコープの構築とマージ判定
            bool isGraphics = pass->getType() == PassType::Graphics;
            bool canMerge = false;

            if (isGraphics && currentScope && currentScope->isGraphics) {
                GraphicsPass* currentPassGfx = static_cast<GraphicsPass*>(pass.get());
                GraphicsPass* scopeFirstPassGfx = static_cast<GraphicsPass*>(m_passes[currentScope->passIndices.front()].get());
                canMerge = isSameAttachmentConfig(currentPassGfx, scopeFirstPassGfx);
            }

            if (!canMerge) {
                // マージできない場合は新しいスコープを作成
                currentBatch->scopes.push_back({});
                currentScope = &currentBatch->scopes.back();
                currentScope->isGraphics = isGraphics;

                if (isGraphics) {
                    BuildScopeAttachments(*currentScope, static_cast<GraphicsPass*>(pass.get()));
                }
            }
            currentScope->passIndices.push_back(passIdx);
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
                
                const auto& reg = m_resourceRegistry[h];
                bool isImg = reg.isImported ? (reg.physicalResource && reg.physicalResource->isImage()) : reg.isImage();
                
                if (currentStates.count(h)) {
                    auto& prevTrack = currentStates[h];
                    uint32_t prevQueueFamily = m_device.getQueueFamilyIndex(prevTrack.queueType);
                    uint32_t prevBatchIdx = passToBatch[prevTrack.lastPassIdx];
                    
                    if (prevQueueFamily != currentQueueFamily) {
                        if (isImg) {
                            VirtualImageBarrier releaseBarrier{};
                            releaseBarrier.srcStageMask = prevTrack.state.stageMask; releaseBarrier.srcAccessMask = prevTrack.state.accessMask;
                            releaseBarrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE; releaseBarrier.dstAccessMask = VK_ACCESS_2_NONE;
                            releaseBarrier.oldLayout = prevTrack.state.layout; releaseBarrier.newLayout = prevTrack.state.layout;
                            releaseBarrier.srcQueueFamilyIndex = prevQueueFamily; releaseBarrier.dstQueueFamilyIndex = currentQueueFamily;
                            releaseBarrier.handle = h;
                            m_batches[prevBatchIdx].imageBarriers.push_back(releaseBarrier);
                            
                            VirtualImageBarrier acquireBarrier = releaseBarrier;
                            acquireBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE; acquireBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                            acquireBarrier.dstStageMask = next.stageMask; acquireBarrier.dstAccessMask = next.accessMask;
                            acquireBarrier.oldLayout = prevTrack.state.layout; 
                            acquireBarrier.newLayout = next.layout;
                            m_batches[currentBatchIdx].imageBarriers.push_back(acquireBarrier);
                            
                        } else {
                            VirtualBufferBarrier releaseBarrier{};
                            releaseBarrier.srcStageMask = prevTrack.state.stageMask; releaseBarrier.srcAccessMask = prevTrack.state.accessMask;
                            releaseBarrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE; releaseBarrier.dstAccessMask = VK_ACCESS_2_NONE;
                            releaseBarrier.srcQueueFamilyIndex = prevQueueFamily; releaseBarrier.dstQueueFamilyIndex = currentQueueFamily;
                            releaseBarrier.handle = h;
                            m_batches[prevBatchIdx].bufferBarriers.push_back(releaseBarrier);

                            VirtualBufferBarrier acquireBarrier = releaseBarrier;
                            acquireBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE; acquireBarrier.srcAccessMask = VK_ACCESS_2_NONE;
                            acquireBarrier.dstStageMask = next.stageMask; acquireBarrier.dstAccessMask = next.accessMask;
                            m_batches[currentBatchIdx].bufferBarriers.push_back(acquireBarrier);
                        }
                    } else {
                        if (prevTrack.state.layout != next.layout || (next.accessMask & VK_ACCESS_2_SHADER_WRITE_BIT)) {
                            if (isImg) {
                                auto physImg = m_resourceAllocator.getPhysicalImage(h);
                                VirtualImageBarrier imgBarrier{};
                                imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                imgBarrier.srcStageMask = prevTrack.state.stageMask; imgBarrier.srcAccessMask = prevTrack.state.accessMask;
                                imgBarrier.dstStageMask = next.stageMask; imgBarrier.dstAccessMask = next.accessMask;
                                imgBarrier.oldLayout = prevTrack.state.layout; imgBarrier.newLayout = next.layout;
                                imgBarrier.handle = h;
                                m_batches[currentBatchIdx].imageBarriers.push_back(imgBarrier);
                            } else {
                                VirtualBufferBarrier bufBarrier{};
                                bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                bufBarrier.srcStageMask = prevTrack.state.stageMask; bufBarrier.srcAccessMask = prevTrack.state.accessMask;
                                bufBarrier.dstStageMask = next.stageMask; bufBarrier.dstAccessMask = next.accessMask;
                                bufBarrier.handle = h;
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

                    if (isImg) {
                        VirtualImageBarrier imgBarrier{};
                        imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        imgBarrier.srcStageMask = prev.stageMask; imgBarrier.srcAccessMask = prev.accessMask;
                        imgBarrier.dstStageMask = next.stageMask; imgBarrier.dstAccessMask = next.accessMask;
                        imgBarrier.oldLayout = prev.layout; imgBarrier.newLayout = next.layout;
                        imgBarrier.handle = h;
                        m_batches[currentBatchIdx].imageBarriers.push_back(imgBarrier);
                    } else {
                        VirtualBufferBarrier bufBarrier{};
                        bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        bufBarrier.srcStageMask = prev.stageMask; bufBarrier.srcAccessMask = prev.accessMask;
                        bufBarrier.dstStageMask = next.stageMask; bufBarrier.dstAccessMask = next.accessMask;
                        bufBarrier.handle = h;
                        m_batches[currentBatchIdx].bufferBarriers.push_back(bufBarrier);
                    }
                }
                currentStates[h] = ResourceTrackingState{ passIdx, next, pass->getQueueType(), req.state, req.stage };
            }
        }
        std::cout << "Barrier Insertion Done." << std::endl;
        // --- 5. SyncPoint Tracking ---
        struct ResourceSyncState {
            RenderBatch::RelativeSync writeSync = {QueueType::Compute, 0};
            std::vector<RenderBatch::RelativeSync> readSyncs;
        };
        std::map<rhi::ResourceHandle, ResourceSyncState> currentSyncStates;

        auto addRelativeWait = [](std::vector<RenderBatch::RelativeSync>& waits, RenderBatch::RelativeSync sp) {
            if (sp.offset == 0) return;
            for (auto& w : waits) {
                if (w.queueType == sp.queueType) {
                    w.offset = std::max(w.offset, sp.offset);
                    return;
                }
            }
            waits.push_back(sp);
        };

        for (uint32_t passIdx : sortedIndices) {
            auto& pass = m_passes[passIdx];
            RenderBatch& batch = m_batches[passToBatch[passIdx]];

            for (size_t i = 0; i < pass->getResourceHandles().size(); ++i) {
                rhi::ResourceHandle h = pass->getResourceHandles()[i];
                const auto& req = pass->getRequirements()[i];
                auto& syncState = currentSyncStates[h];

                if (isWriteUsage(req.state)) {
                    if (syncState.writeSync.offset > 0 && syncState.writeSync.queueType != batch.queueType) {
                        addRelativeWait(batch.relativeWaitPoints, syncState.writeSync);
                    }
                    for (const auto& rs : syncState.readSyncs) {
                        if (rs.offset > 0 && rs.queueType != batch.queueType) {
                            addRelativeWait(batch.relativeWaitPoints, rs);
                        }
                    }
                    syncState.writeSync = {batch.queueType, batch.relativeSignalOffset};
                    syncState.readSyncs.clear();
                } else {
                    if (syncState.writeSync.offset > 0 && syncState.writeSync.queueType != batch.queueType) {
                        addRelativeWait(batch.relativeWaitPoints, syncState.writeSync);
                    }
                    bool found = false;
                    for (auto& rs : syncState.readSyncs) {
                        if (rs.queueType == batch.queueType) {
                            rs.offset = std::max(rs.offset, batch.relativeSignalOffset);
                            found = true; break;
                        }
                    }
                    if (!found) syncState.readSyncs.push_back({batch.queueType, batch.relativeSignalOffset});
                }
            }
        }
        std::cout << "Physical Resource SyncStates Updated." << std::endl;
        // --- Swapchain Sync Tracking ---
        for (const auto& [h, track] : currentStates) {
            const auto& reg = m_resourceRegistry[h];
            bool isImg = reg.isImported ? (reg.physicalResource && reg.physicalResource->isImage()) : reg.isImage();
            if (isImg && reg.isImported && reg.physicalResource) {
                VulkanImage* physImg = static_cast<VulkanImage*>(reg.physicalResource);
                std::cout<<"test_swapchain"<<std::endl;
                if (physImg->isSwapchainImage()) {
                    rhi::Swapchain* swapchain = physImg->getSwapchain();                    
                    uint32_t firstPassIdx = m_resourceLifetimes[h].firstPass;
                    uint32_t firstBatchIdx = passToBatch[firstPassIdx];
                    uint32_t lastBatchIdx = passToBatch[track.lastPassIdx];
                    std::cout << "Swapchain Image Detected: Resource " << h << ", First Batch " << firstBatchIdx << ", Last Batch " << lastBatchIdx << std::endl;
                    m_swapchainSyncs.push_back({swapchain, firstBatchIdx, lastBatchIdx});
                    VirtualImageBarrier presentBarrier;
                    presentBarrier.handle = h;
                    presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    presentBarrier.srcStageMask = track.state.stageMask;
                    presentBarrier.srcAccessMask = track.state.accessMask;
                    presentBarrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE; 
                    presentBarrier.dstAccessMask = VK_ACCESS_2_NONE;
                    presentBarrier.oldLayout = track.state.layout;
                    presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    
                    m_batches[lastBatchIdx].postImageBarriers.push_back(presentBarrier);
                }
            }
        }
        m_sortedIndices = sortedIndices;
    }

    void VulkanRenderGraph::execute(const std::vector<SemaphoreHandle>& waitSemaphores) {
        // std::cout << "Executing RenderGraph..." << std::endl;
        std::map<QueueType, uint64_t> queueBaseValues;
        for (QueueType type : {QueueType::Graphics, QueueType::Compute, QueueType::Transfer}) {
            queueBaseValues[type] = m_device.getTimelineSemaphoreObject(type).getCurrentValue();
        }
        for (auto& batch : m_batches) {
            batch.runtimeSignalSyncPoint = { batch.queueType, queueBaseValues[batch.queueType] + batch.relativeSignalOffset };
            batch.runtimeWaitSyncPoints.clear();
            for (const auto& relWait : batch.relativeWaitPoints) {
                batch.runtimeWaitSyncPoints.push_back({
                    relWait.queueType, 
                    queueBaseValues[relWait.queueType] + relWait.offset
                });
            }
        }

        // プール全体を一括リセット
        uint32_t frameIdx = m_device.getCurrentFrame() % MAX_FRAMES_IN_FLIGHT;
        auto& currentFrameData = m_frameData[frameIdx];
        for (auto& [type, poolData] : currentFrameData.pools) {
            vkResetCommandPool(m_device.getDevice(), poolData.pool, 0);
            poolData.activeCount = 0; // 使用カウントをリセット
        }
        auto asyncSems = m_device.getUploadManager()->consumeAsyncSyncPoints();
        std::vector<SyncPoint> combinedAsyncWaits = asyncSems;

        

        m_submitBatches.clear();
        SubmitBatch currentSubmitBatch;

        for (size_t batchIdx = 0; batchIdx < m_batches.size(); ++batchIdx) {
            auto& batch = m_batches[batchIdx];
            auto& poolData = currentFrameData.pools[batch.queueType];
            if (poolData.activeCount >= poolData.commandLists.size()) {
                // 足りない場合のみ新しいコマンドバッファをプールから確保
                poolData.commandLists.push_back(std::make_unique<VulkanCommandList>(m_device, batch.queueType, poolData.pool));
            }
            VulkanCommandList* cmdList = poolData.commandLists[poolData.activeCount++].get();
            cmdList->begin();
            auto vkCmdBuf = cmdList->getCommandBuffer();

            std::vector<VkImageMemoryBarrier2> actualImageBarriers;
            std::vector<VkBufferMemoryBarrier2> actualBufferBarriers;

            for (const auto& vb : batch.imageBarriers) {
                VulkanImage* physImg = m_resourceAllocator.getPhysicalImage(vb.handle);
                if (!physImg) continue; // リソースがバインドされていない場合はスキップ
                VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                b.srcStageMask = vb.srcStageMask; b.srcAccessMask = vb.srcAccessMask;
                b.dstStageMask = vb.dstStageMask; b.dstAccessMask = vb.dstAccessMask;
                b.oldLayout = vb.oldLayout; b.newLayout = vb.newLayout;
                b.srcQueueFamilyIndex = vb.srcQueueFamilyIndex; b.dstQueueFamilyIndex = vb.dstQueueFamilyIndex;
                b.image = physImg->getImage();
                b.subresourceRange = { getAspectMask(physImg->getDesc().format), 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
                actualImageBarriers.push_back(b);
            }

            for (const auto& vb : batch.bufferBarriers) {
                VulkanBuffer* physBuf = m_resourceAllocator.getPhysicalBuffer(vb.handle);
                if (!physBuf) continue;
                VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                b.srcStageMask = vb.srcStageMask; b.srcAccessMask = vb.srcAccessMask;
                b.dstStageMask = vb.dstStageMask; b.dstAccessMask = vb.dstAccessMask;
                b.srcQueueFamilyIndex = vb.srcQueueFamilyIndex; b.dstQueueFamilyIndex = vb.dstQueueFamilyIndex;
                b.buffer = physBuf->getNativeBuffer();
                b.offset = 0; b.size = VK_WHOLE_SIZE;
                actualBufferBarriers.push_back(b);
            }

            if (!actualImageBarriers.empty() || !actualBufferBarriers.empty()) {
                VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                depInfo.imageMemoryBarrierCount = (uint32_t)actualImageBarriers.size();
                depInfo.pImageMemoryBarriers    = actualImageBarriers.data();
                depInfo.bufferMemoryBarrierCount = (uint32_t)actualBufferBarriers.size();
                depInfo.pBufferMemoryBarriers    = actualBufferBarriers.data();
                vkCmdPipelineBarrier2(vkCmdBuf, &depInfo);
            }

            for (auto& scope : batch.scopes) {
                if (scope.isGraphics) {
                    // --- アタッチメントと解像度の遅延評価 ---
                    std::vector<VulkanCommandList::RenderAttachment> vkColorAtts;
                    std::optional<VulkanCommandList::RenderAttachment> vkDepthAtt = std::nullopt;
                    uint32_t renderWidth = 0;
                    uint32_t renderHeight = 0;

                    for (const auto& logicalAtt : scope.colorAtts) {
                        VulkanImage* physImg = m_resourceAllocator.getPhysicalImage(logicalAtt.handle);
                        vkColorAtts.push_back({
                            physImg->getView(),
                            logicalAtt.loadOp, logicalAtt.storeOp, logicalAtt.clearValue
                        });
                        // 最初の有効なアタッチメントから解像度を取得
                        if (renderWidth == 0) { renderWidth = physImg->getDesc().width; renderHeight = physImg->getDesc().height; }
                    }

                    if (scope.depthAtt.has_value()) {
                        VulkanImage* physDepth = m_resourceAllocator.getPhysicalImage(scope.depthAtt->handle);
                        vkDepthAtt = VulkanCommandList::RenderAttachment{
                            physDepth->getView(),
                            scope.depthAtt->loadOp, scope.depthAtt->storeOp, scope.depthAtt->clearValue
                        };
                        if (renderWidth == 0) { renderWidth = physDepth->getDesc().width; renderHeight = physDepth->getDesc().height; }
                    }

                    const auto* pDepthAtt = vkDepthAtt.has_value() ? &vkDepthAtt.value() : nullptr;
                    cmdList->beginRendering(vkColorAtts, pDepthAtt, renderWidth, renderHeight);

                    // パスの実行
                    for (uint32_t passIdx : scope.passIndices) {
                        executePass(m_passes[passIdx].get(), *cmdList);
                    }
                    cmdList->endRendering();
                } else {
                    for (uint32_t passIdx : scope.passIndices) {
                        executePass(m_passes[passIdx].get(), *cmdList);
                    }
                }
            }


            std::vector<VkImageMemoryBarrier2> actualPostImageBarriers;
            for (const auto& vb : batch.postImageBarriers) {
                VulkanImage* physImg = m_resourceAllocator.getPhysicalImage(vb.handle);
                if (!physImg) continue;
                VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                b.srcStageMask = vb.srcStageMask; b.srcAccessMask = vb.srcAccessMask;
                b.dstStageMask = vb.dstStageMask; b.dstAccessMask = vb.dstAccessMask;
                b.oldLayout = vb.oldLayout; b.newLayout = vb.newLayout;
                b.srcQueueFamilyIndex = vb.srcQueueFamilyIndex; b.dstQueueFamilyIndex = vb.dstQueueFamilyIndex;
                b.image = physImg->getImage();
                b.subresourceRange = { getAspectMask(physImg->getDesc().format), 0, VK_REMAINING_MIP_LEVELS, 0, 1 };
                actualPostImageBarriers.push_back(b);
            }

            if (!actualPostImageBarriers.empty()) {
                VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                depInfo.imageMemoryBarrierCount = (uint32_t)actualPostImageBarriers.size();
                depInfo.pImageMemoryBarriers    = actualPostImageBarriers.data();
                vkCmdPipelineBarrier2(vkCmdBuf, &depInfo);
            }

            cmdList->end();


            // キューの切り替え判定
            VkQueue queue = m_device.getQueue(batch.queueType);
            if (currentSubmitBatch.queue != queue && currentSubmitBatch.queue != VK_NULL_HANDLE) {
                m_submitBatches.push_back(currentSubmitBatch);
                currentSubmitBatch = SubmitBatch{};
            }
            currentSubmitBatch.queue = queue;

            std::vector<VkSemaphoreSubmitInfo> currentWaitInfos;
            std::vector<VkSemaphoreSubmitInfo> currentSignalInfos;

            for (const auto& sp : batch.runtimeWaitSyncPoints) {
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
            VkSemaphore sigSem = m_device.getTimelineSemaphore(batch.runtimeSignalSyncPoint.queueType);
            if (sigSem != VK_NULL_HANDLE) {
                VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                signalInfo.semaphore = sigSem;
                signalInfo.value = batch.runtimeSignalSyncPoint.value; // 絶対値を使用
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
            m_submitBatches.push_back(currentSubmitBatch);
        }
        

        // --- バッチサブミットの実行 ---
        for (auto& sb : m_submitBatches) {
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
        for (const auto& [type, maxOffset] : m_queueMaxOffsets) {
            for (uint64_t i = 0; i < maxOffset; ++i) {
                m_device.getTimelineSemaphoreObject(type).advanceAndGetNextValue();
            }
        }
    }
}