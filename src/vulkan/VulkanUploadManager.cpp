#include "VulkanUploadManager.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>

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

    void VulkanUploadManager::flushAsync(Buffer* dstBuffer, UploadMode mode) {
        m_asyncState.cmdList->end();
        vkResetFences(m_device.getDevice(), 1, &m_asyncState.syncFence);
        
        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkCommandBuffer cmdBuf = m_asyncState.cmdList->getCommandBuffer();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        
        if (mode == UploadMode::Async) {
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &m_asyncState.syncSemaphore;
            // マップに記録し、あとでレンダーグラフに回収させる
            m_pendingAsyncSemaphores[dstBuffer] = m_asyncState.syncSemaphore;
        }

        vkQueueSubmit(m_device.getQueue(QueueType::Transfer), 1, &submitInfo, m_asyncState.syncFence);
        m_asyncState.isSubmitted = true;

        if (mode == UploadMode::Immediate) {
            ensureAsyncReady(); // Immediateならその場でCPU待機
        }
    }

    void VulkanUploadManager::uploadBuffer(Buffer* dstBuffer, const void* data, size_t size, UploadMode mode) {
        if (mode == UploadMode::Deferred) {
            auto& state = m_frameStates[m_currentFrameIndex];
            StagingAllocation alloc = allocateStagingSpace(state, size);
            std::memcpy(alloc.mappedPtr, data, size);
            state.pendingUploads.push_back({alloc.buffer, alloc.offset, dstBuffer, size});
        } else {
            ensureAsyncReady();
            StagingAllocation alloc = allocateStagingSpace(m_asyncState, size);
            std::memcpy(alloc.mappedPtr, data, size);
            m_asyncState.cmdList->copyBuffer(alloc.buffer, dstBuffer, size, alloc.offset, 0);
            flushAsync(dstBuffer, mode);
        }
    }

    void* VulkanUploadManager::mapForDeferredUpload(Buffer* dstBuffer, size_t size) {
        auto& state = m_frameStates[m_currentFrameIndex];
        StagingAllocation alloc = allocateStagingSpace(state, size);
        state.pendingUploads.push_back({alloc.buffer, alloc.offset, dstBuffer, size});
        return alloc.mappedPtr;
    }

    std::vector<SemaphoreHandle> VulkanUploadManager::consumeAsyncSemaphores(const std::vector<Buffer*>& buffers) {
        std::vector<SemaphoreHandle> sems;
        for (auto buf : buffers) {
            auto it = m_pendingAsyncSemaphores.find(buf);
            if (it != m_pendingAsyncSemaphores.end()) {
                sems.push_back(it->second);
                m_pendingAsyncSemaphores.erase(it); // 回収したら消す
            }
        }
        // 重複セマフォの除去
        std::sort(sems.begin(), sems.end());
        sems.erase(std::unique(sems.begin(), sems.end()), sems.end());
        return sems;
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
}