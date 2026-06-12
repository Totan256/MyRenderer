#include "VulkanUploadManager.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>
#include "vulkan/VulkanSync.hpp"


namespace rhi::vk {
    VulkanUploadManager::VulkanUploadManager(VulkanDevice& device, uint32_t framesInFlight, size_t ringBufferSize)
        : m_device(device), m_framesInFlight(framesInFlight) {
        m_frameStates.resize(m_framesInFlight);
        for (uint32_t i = 0; i < m_framesInFlight; ++i) {
            m_frameStates[i].ringBufferSize = ringBufferSize;
            resetRingBufferState(m_frameStates[i], getOrCreateRingBuffer(ringBufferSize));
        }
    }

    VulkanUploadManager::~VulkanUploadManager() {
        retireCompletedAsyncContexts(true);
        for (auto& ctx : m_asyncContextPool) {
            if (ctx->syncFence != VK_NULL_HANDLE) {
                vkDestroyFence(m_device.getDevice(), ctx->syncFence, nullptr);
            }
        }
    }

    StagingAllocation VulkanUploadManager::allocateStagingSpace(UploadState& state, size_t size, size_t alignment) {
        const size_t THRESHOLD = state.ringBufferSize / 4;
        size_t allocOffset = (state.ringBufferOffset + alignment - 1) & ~(alignment - 1);

        if (size > THRESHOLD || allocOffset + size > state.ringBufferSize) {
            auto tempBuf = std::make_unique<VulkanBuffer>(
                m_device, m_device.getAllocator(), size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU
            );
            void* ptr = tempBuf->map();
            VulkanBuffer* rawPtr = tempBuf.get();
            state.pendingTemporaryBuffers.push_back(std::move(tempBuf));
            return { rawPtr, 0, ptr, true };
        }

        void* ptr = state.ringMappedPtr + allocOffset;
        state.ringBufferOffset = allocOffset + size;
        return { state.ringBuffer.get(), allocOffset, ptr, false };
    }
    AsyncUploadContext* VulkanUploadManager::getOrCreateAsyncContext() {
        for (auto& ctx : m_asyncContextPool) {
            if (std::find(m_activeAsyncContexts.begin(), m_activeAsyncContexts.end(), ctx.get()) == m_activeAsyncContexts.end()) {
                return ctx.get();
            }
        }
        auto ctx = std::make_unique<AsyncUploadContext>();
        ctx->cmdList = std::make_unique<VulkanCommandList>(m_device, QueueType::Transfer);
        
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(m_device.getDevice(), &fenceInfo, nullptr, &ctx->syncFence);
        
        m_asyncContextPool.push_back(std::move(ctx));
        return m_asyncContextPool.back().get();
    }

    void VulkanUploadManager::retireCompletedAsyncContexts(bool waitAll) {
        auto it = m_activeAsyncContexts.begin();
        while (it != m_activeAsyncContexts.end()) {
            AsyncUploadContext* ctx = *it;
            VkResult status = vkGetFenceStatus(m_device.getDevice(), ctx->syncFence);
            
            if (waitAll || status == VK_SUCCESS) {
                if (waitAll && status != VK_SUCCESS) {
                    vkWaitForFences(m_device.getDevice(), 1, &ctx->syncFence, VK_TRUE, UINT64_MAX);
                }
                vkResetFences(m_device.getDevice(), 1, &ctx->syncFence);
                
                // 【変更】使い終わったリングバッファを破棄せずプールに戻す
                if (ctx->retainedRingBuffer) {
                    m_freeRingBuffers.push_back(std::move(ctx->retainedRingBuffer));
                }
                ctx->retainedBuffers.clear();
                
                it = m_activeAsyncContexts.erase(it);
            } else {
                ++it;
            }
        }
    }

    void VulkanUploadManager::beginFrame(uint64_t currentFrameIndex) {
        m_currentFrameIndex = currentFrameIndex % m_framesInFlight;
        auto& state = m_frameStates[m_currentFrameIndex];
        state.pendingTemporaryBuffers.clear();
        state.ringBufferOffset = 0;
        state.pendingUploads.clear();
    }

    std::vector<UploadRequest> VulkanUploadManager::getAndClearPendingUploads() {
        auto& state = m_frameStates[m_currentFrameIndex];
        auto res = std::move(state.pendingUploads);
        state.pendingUploads.clear();
        return res;
    }
    std::vector<rhi::ImageUploadRequest> VulkanUploadManager::getAndClearPendingImageUploads() {
        auto& state = m_frameStates[m_currentFrameIndex];
        auto res = std::move(state.pendingImageUploads);
        state.pendingImageUploads.clear();
        return res;
    }

    void VulkanUploadManager::enqueueBufferUpload(Buffer* dstBuffer, const void* data, size_t size, size_t dstOffset) {
        auto& state = m_frameStates[m_currentFrameIndex];
        StagingAllocation alloc = allocateStagingSpace(state, size);
        std::memcpy(alloc.mappedPtr, data, size);
        state.pendingUploads.push_back({alloc.buffer, alloc.offset, dstBuffer, size});
    }

    void VulkanUploadManager::enqueueImageUpload(Image* dstImage, const void* data, size_t size, uint32_t width, uint32_t height, uint32_t mipLevels) {
        auto& state = m_frameStates[m_currentFrameIndex];
        StagingAllocation alloc = allocateStagingSpace(state, size);
        std::memcpy(alloc.mappedPtr, data, size);
        state.pendingImageUploads.push_back({
            alloc.buffer, alloc.offset, dstImage, width, height, mipLevels
        });
    }

    SyncPoint VulkanUploadManager::submitUploadsAsync() {
        auto& state = m_frameStates[m_currentFrameIndex];
        if (state.pendingUploads.empty() && state.pendingImageUploads.empty()) {
            return SyncPoint{QueueType::Transfer, 0}; // 無効なSyncPointを返す
        }

        retireCompletedAsyncContexts(false);
        AsyncUploadContext* ctx = getOrCreateAsyncContext();
        m_activeAsyncContexts.push_back(ctx);
        vkResetFences(m_device.getDevice(), 1, &ctx->syncFence);
        ctx->cmdList->reset();
        ctx->cmdList->begin();

        // コマンドの記録
        for (const auto& up : state.pendingUploads) {
            ctx->cmdList->copyBuffer(up.stagingBuffer, up.dstBuffer, up.size, up.stagingOffset, 0);
        }
        VkCommandBuffer cmd = ctx->cmdList->getCommandBuffer();
        std::vector<VkImageMemoryBarrier> initialBarriers;
        initialBarriers.reserve(state.pendingImageUploads.size());
        // レイアウト遷移バリアを先に記録しておく（全てのアップロードで共通なので一括で）
        for (const auto& up : state.pendingImageUploads) {
            VulkanImage* physDstImg = static_cast<VulkanImage*>(up.dstImage);
            
            VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.image = physDstImg->getImage();
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, up.mipLevels, 0, 1 };
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            
            initialBarriers.push_back(barrier);
        }
        if (!initialBarriers.empty()) {
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 
                                0, nullptr, 0, nullptr, (uint32_t)initialBarriers.size(), initialBarriers.data());
        }

        // コピーとミップマップ生成を記録
        for (const auto& up : state.pendingImageUploads) {
            VulkanImage* physDstImg = static_cast<VulkanImage*>(up.dstImage);
            VulkanBuffer* physSrcBuf = static_cast<VulkanBuffer*>(up.stagingBuffer);
            
            VkBufferImageCopy region{};
            region.bufferOffset = up.stagingOffset;
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageExtent = { up.width, up.height, 1 };
            vkCmdCopyBufferToImage(cmd, physSrcBuf->getNativeBuffer(), physDstImg->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            
            physDstImg->recordMipmapGenerationCmds(cmd);
        }
        ctx->cmdList->end();

        // バッファ所有権の退避と新規補充
        ctx->retainedRingBuffer = std::move(state.ringBuffer);
        for(auto& tempBuf : state.pendingTemporaryBuffers){
            ctx->retainedBuffers.push_back(std::move(tempBuf));
        }
        state.pendingTemporaryBuffers.clear();
        resetRingBufferState(state, getOrCreateRingBuffer(state.ringBufferSize));
        
        // --- タイムラインセマフォの取得とシグナル設定 ---
        SyncPoint syncPoint = m_device.advanceTimeline(QueueType::Transfer);
        VkSemaphore timelineSemaphore = m_device.getTimelineSemaphore(QueueType::Transfer);
        m_pendingAsyncSyncPoints.push_back(syncPoint); // グラフ側での回収用に記録

        // アップロード先リソースに直接同期情報を書き込む
        for (const auto& up : state.pendingUploads) {
            up.dstBuffer->setWriteSync(syncPoint);
        }
        for (const auto& up : state.pendingImageUploads) {
            up.dstImage->setWriteSync(syncPoint);
        }

        // VkTimelineSemaphoreSubmitInfo を追加して提出
        VkTimelineSemaphoreSubmitInfo timelineInfo{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
        timelineInfo.signalSemaphoreValueCount = 1;
        timelineInfo.pSignalSemaphoreValues = &syncPoint.value;

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.pNext = &timelineInfo;
        VkCommandBuffer cmdBuf = ctx->cmdList->getCommandBuffer();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &timelineSemaphore;

        if (vkQueueSubmit(m_device.getQueue(QueueType::Transfer), 1, &submitInfo, ctx->syncFence) != VK_SUCCESS) {
            std::cerr << "Failed to submit transfer command buffer!" << std::endl;
        }

        state.pendingUploads.clear();
        state.pendingImageUploads.clear();

        return syncPoint;
    }

    void VulkanUploadManager::waitUploads() {
        retireCompletedAsyncContexts(true);
    }

    std::vector<SyncPoint> VulkanUploadManager::consumeAsyncSyncPoints() {
        std::vector<SyncPoint> sps = std::move(m_pendingAsyncSyncPoints);
        m_pendingAsyncSyncPoints.clear();
        return sps;
    }

    std::unique_ptr<VulkanBuffer> VulkanUploadManager::getOrCreateRingBuffer(size_t size) {
        if (!m_freeRingBuffers.empty()) {
            auto buf = std::move(m_freeRingBuffers.back());
            m_freeRingBuffers.pop_back();
            return buf;
        }
        // プールが空なら新規作成
        return std::make_unique<VulkanBuffer>(
            m_device, m_device.getAllocator(), size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
    }

    void VulkanUploadManager::resetRingBufferState(UploadState& state, std::unique_ptr<VulkanBuffer> newBuffer) {
        state.ringBuffer = std::move(newBuffer);
        state.ringMappedPtr = static_cast<uint8_t*>(state.ringBuffer->map());
        state.ringBufferOffset = 0;
    }
}