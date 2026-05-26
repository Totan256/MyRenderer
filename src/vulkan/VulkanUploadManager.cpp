#include "VulkanUploadManager.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>
#include "vulkan/VulkanSync.hpp"


namespace rhi::vk {
    VulkanUploadManager::VulkanUploadManager(VulkanDevice& device, uint32_t framesInFlight, size_t ringBufferSize)
        : m_device(device), m_framesInFlight(framesInFlight) {
        
        m_asyncState.ringBufferSize = ringBufferSize;
        m_asyncState.ringBuffer = std::make_unique<VulkanBuffer>(
            m_device, m_device.getAllocator(), ringBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        m_asyncState.ringMappedPtr = static_cast<uint8_t*>(m_asyncState.ringBuffer->map());
        m_asyncState.cmdList = std::make_unique<VulkanCommandList>(m_device, QueueType::Transfer);
        
        m_asyncState.syncSemaphore = m_device.createSemaphore();
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(m_device.getDevice(), &fenceInfo, nullptr, &m_asyncState.syncFence);

        m_asyncState.cmdList->reset();
        m_asyncState.cmdList->begin();

        m_frameStates.resize(m_framesInFlight);
        for (uint32_t i = 0; i < m_framesInFlight; ++i) {
            auto& state = m_frameStates[i];
            state.ringBufferSize = ringBufferSize;
            state.ringBuffer = std::make_unique<VulkanBuffer>(
                m_device, m_device.getAllocator(), ringBufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU
            );
            state.ringMappedPtr = static_cast<uint8_t*>(state.ringBuffer->map());
        }
    }

    VulkanUploadManager::~VulkanUploadManager() {
        ensureAsyncReady();
        m_device.destroySemaphore(m_asyncState.syncSemaphore);
        vkDestroyFence(m_device.getDevice(), m_asyncState.syncFence, nullptr);
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

    void VulkanUploadManager::ensureAsyncReady() {
        if (m_asyncState.isSubmitted) {
            vkWaitForFences(m_device.getDevice(), 1, &m_asyncState.syncFence, VK_TRUE, UINT64_MAX);
            vkResetFences(m_device.getDevice(), 1, &m_asyncState.syncFence);
            m_asyncState.pendingTemporaryBuffers.clear();
            m_asyncState.ringBufferOffset = 0;
            m_asyncState.cmdList->reset();
            m_asyncState.cmdList->begin();
            m_asyncState.isSubmitted = false;
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

    rhi::SemaphoreHandle VulkanUploadManager::submitUploadsAsync() {
        auto& state = m_frameStates[m_currentFrameIndex];
        
        // 溜まったリクエストがない場合は何もしない
        if (state.pendingUploads.empty() && state.pendingImageUploads.empty()) {
            return nullptr;
        }

        ensureAsyncReady();
        
        // stateに溜まったリクエストを m_asyncState の cmdList に記録する処理
        for (const auto& up : state.pendingUploads) {
            m_asyncState.cmdList->copyBuffer(up.stagingBuffer, up.dstBuffer, up.size, up.stagingOffset, 0);
        }
        for (const auto& up : state.pendingImageUploads) {
            VulkanImage* physDstImg = static_cast<VulkanImage*>(up.dstImage);
            VulkanBuffer* physSrcBuf = static_cast<VulkanBuffer*>(up.stagingBuffer);
            
            // Image用のバリアとコピーコマンド（以前の uploadImageFromFile 内のコマンド発行部を移植）
            VkCommandBuffer cmd = m_asyncState.cmdList->getCommandBuffer();
            
            VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.image = physDstImg->getImage();
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, up.mipLevels, 0, 1 };
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region{};
            region.bufferOffset = up.stagingOffset;
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageExtent = { up.width, up.height, 1 };
            vkCmdCopyBufferToImage(cmd, physSrcBuf->getNativeBuffer(), physDstImg->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            
            physDstImg->recordMipmapGenerationCmds(cmd);
        }

        m_asyncState.cmdList->end();
        vkResetFences(m_device.getDevice(), 1, &m_asyncState.syncFence);
        
        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkCommandBuffer cmdBuf = m_asyncState.cmdList->getCommandBuffer();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &m_asyncState.syncSemaphore;

        vkQueueSubmit(m_device.getQueue(QueueType::Transfer), 1, &submitInfo, m_asyncState.syncFence);
        m_asyncState.isSubmitted = true;

        // 転送が終わったリクエストリストをクリア
        state.pendingUploads.clear();
        state.pendingImageUploads.clear();
        
        m_pendingAsyncSemaphores.push_back(m_asyncState.syncSemaphore);
        return m_asyncState.syncSemaphore;
    }

    void VulkanUploadManager::waitUploads() {
        ensureAsyncReady(); // CPU待機
    }

    std::vector<rhi::SemaphoreHandle> VulkanUploadManager::consumeAsyncSemaphores() {
        std::vector<rhi::SemaphoreHandle> sems;
        for (auto sem : m_pendingAsyncSemaphores) {
            sems.push_back(sem);
        }
        m_pendingAsyncSemaphores.clear();
        return sems;
    }
}