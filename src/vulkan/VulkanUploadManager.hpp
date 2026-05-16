#pragma once
#include "rhi/UploadManager.hpp"
#include "VulkanDevice.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanCommandList.hpp"
#include <memory>
#include <vector>

namespace rhi::vk {
    struct StagingAllocation {
        VulkanBuffer* buffer;
        size_t offset;
        void* mappedPtr;
        bool isTemporary; 
    };

    class VulkanUploadManager : public UploadManager {
    public:
        VulkanUploadManager(VulkanDevice& device, size_t ringBufferSize);
        ~VulkanUploadManager() override;

        void* mapForUploadImmediate(Buffer* dstBuffer, size_t size) override;
        void uploadImmediate(Buffer* dstBuffer, const void* data, size_t size) override;
        void waitForImmediateUploads() override;

        void* mapForUploadDeferred(Buffer* dstBuffer, size_t size) override;
        void requestUploadDeferred(Buffer* dstBuffer, const void* data, size_t size) override;

        SemaphoreHandle flushDeferredUploads() override;
        void garbageCollect(uint64_t completedFrameId) override;

    private:
        VulkanDevice& m_device;
        
        std::unique_ptr<VulkanBuffer> m_ringBuffer;
        size_t m_ringBufferSize;
        size_t m_ringBufferOffset = 0;
        uint8_t* m_ringMappedPtr = nullptr;

        std::unique_ptr<VulkanCommandList> m_immediateCmd;
        std::unique_ptr<VulkanCommandList> m_deferredCmd;
        bool m_hasDeferredCommands = false;

        // 同期用のVulkan Semaphore
        VkSemaphore m_transferSemaphore = VK_NULL_HANDLE;

        std::vector<std::unique_ptr<VulkanBuffer>> m_pendingTemporaryBuffers;

        StagingAllocation allocateStagingSpace(size_t size);
    };
}