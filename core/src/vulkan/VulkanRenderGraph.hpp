#pragma once
#include "RenderGraph.hpp"
#include "VulkanImage.hpp"
#include "VulkanBuffer.hpp"
#include "vulkan/VulkanCommandList.hpp"
#include "VulkanSync.hpp"
#include "rhi/Swapchain.hpp"
#include "VulkanResourceAllocator.hpp"
#include <map>

namespace rhi::vk {
    struct CommandPoolData {
        VkCommandPool pool = VK_NULL_HANDLE;
        std::vector<std::unique_ptr<VulkanCommandList>> commandLists;
        uint32_t activeCount = 0; // そのフレームで使用中のコマンドリスト数
    };

    struct PerFrameData {
        std::map<QueueType, CommandPoolData> pools;
    };

    struct RenderScopeAttachment {
        ResourceHandle handle;
        VkAttachmentLoadOp loadOp;
        VkAttachmentStoreOp storeOp;
        VkClearValue clearValue;
    };

    struct VirtualImageBarrier {
        ResourceHandle handle;
        VkPipelineStageFlags2 srcStageMask;
        VkAccessFlags2 srcAccessMask;
        VkPipelineStageFlags2 dstStageMask;
        VkAccessFlags2 dstAccessMask;
        VkImageLayout oldLayout;
        VkImageLayout newLayout;
        uint32_t srcQueueFamilyIndex;
        uint32_t dstQueueFamilyIndex;
    };

    struct VirtualBufferBarrier {
        ResourceHandle handle;
        VkPipelineStageFlags2 srcStageMask;
        VkAccessFlags2 srcAccessMask;
        VkPipelineStageFlags2 dstStageMask;
        VkAccessFlags2 dstAccessMask;
        uint32_t srcQueueFamilyIndex;
        uint32_t dstQueueFamilyIndex;
    };

    struct RenderScope {
        bool isGraphics = false;
        std::vector<uint32_t> passIndices;

        // Graphicsの場合、スコープの開始時に渡すアタッチメント情報
        std::vector<RenderScopeAttachment> colorAtts;
        std::optional<RenderScopeAttachment> depthAtt;
    };
    
    struct RenderBatch {
        QueueType queueType;
        struct RelativeSync {
            QueueType queueType;
            uint32_t offset;
            VkPipelineStageFlags2 waitStageMask = VK_PIPELINE_STAGE_2_NONE;
        };
        // compile時に決まる相対的な依存関係
        std::vector<RelativeSync> relativeWaitPoints;
        uint32_t relativeSignalOffset = 0;
        // execute時に計算される絶対的な同期ポイント
        struct RuntimeWait {
            QueueType queueType;
            uint64_t value;
            VkPipelineStageFlags2 waitStageMask = VK_PIPELINE_STAGE_2_NONE;
        };
        // execute時に計算される絶対的な同期ポイント
        std::vector<RuntimeWait> runtimeWaitSyncPoints;
        SyncPoint runtimeSignalSyncPoint;

        std::vector<RenderScope> scopes;
        std::vector<VirtualImageBarrier> imageBarriers;
        std::vector<VirtualBufferBarrier> bufferBarriers;
        std::vector<VirtualImageBarrier> postImageBarriers;
    };

    struct SwapchainSync {
        rhi::Swapchain* swapchain;
        uint32_t firstBatchIdx;
        uint32_t lastBatchIdx;
    };

    class VulkanRenderGraph : public RenderGraph {
    public:
        VulkanRenderGraph(VulkanDevice& device);
        ~VulkanRenderGraph() override;

        ComputePass& addComputePass(const std::string& name, const std::string& shaderPath, QueueType queueType = QueueType::Compute) override;
        GraphicsPass& addGraphicsPass(const std::string& name, const std::string& vertShaderPath, const std::string& fragShaderPath) override;
        CopyPass& addCopyPass(const std::string& name, ResourceHandle srcBuffer, ResourceHandle dstBuffer, size_t size, QueueType queueType = QueueType::Transfer) override;

        ResourceHandle importResource(Resource* res, StringHash nameHash = {0}) override;
        ResourceHandle importSwapchain(rhi::Swapchain* swapchain, StringHash nameHash = {0}) override;
        ResourceHandle createImage(const ImageDesc& desc, StringHash nameHash = {0}) override;
        ResourceHandle createBuffer(const BufferDesc& desc, StringHash nameHash = {0}) override;
        
        const ResourceRegistration& getRegistration(ResourceHandle handle) const override { return m_resourceRegistry[handle]; }
        uint32_t getPhysicalIndex(ResourceHandle handle) override;
        Device& getDevice() override { return m_device; }
        VulkanResourceAllocator& getAllocator() { return m_resourceAllocator; }

        void setProfiler(GPUProfiler* profiler) override { m_profiler = profiler; }
        GPUProfiler* getProfiler() const override { return m_profiler; }

        void compile() override;
        void resize(uint32_t width, uint32_t height) override;
        void execute(const std::vector<SemaphoreHandle>& waitSemaphores) override;

        void bindPhysicalResource(ResourceHandle handle, Resource* res) override;

    private:
        struct SwapchainSync {
            rhi::Swapchain* swapchain;
            uint32_t firstBatchIdx;
            uint32_t lastBatchIdx;
        };
        VulkanDevice& m_device;
        VulkanResourceAllocator m_resourceAllocator;
        std::vector<uint32_t> m_sortedIndices;
        std::map<Resource*, ResourceHandle> m_physicalToHandle;

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
        std::vector<RenderBatch> m_batches;
        std::vector<VkSemaphore> m_batchSemaphores;
        std::vector<SubmitBatch> m_submitBatches; 
        std::vector<VkCommandBufferSubmitInfo> m_cbInfos;
        std::vector<SwapchainSync> m_swapchainSyncs;

        std::array<PerFrameData, MAX_FRAMES_IN_FLIGHT> m_frameData;
        void clearBatchSemaphores();

        // 各キューで現在の最大オフセットを追跡（次の同期ポイントを計算するため）
        std::map<QueueType, uint32_t> m_queueMaxOffsets;
    };
}