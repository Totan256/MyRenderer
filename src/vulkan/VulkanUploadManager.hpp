#pragma once
#include "rhi/UploadManager.hpp"
#include "VulkanDevice.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanCommandList.hpp"
#include <memory>
#include <vector>
#include <unordered_map>

namespace rhi::vk {
    struct StagingAllocation {
        VulkanBuffer* buffer;
        size_t offset;
        void* mappedPtr;
        bool isTemporary; 
    };

    struct UploadState {
        std::unique_ptr<VulkanBuffer> ringBuffer;
        size_t ringBufferSize = 0;
        size_t ringBufferOffset = 0;
        uint8_t* ringMappedPtr = nullptr;
        std::vector<std::unique_ptr<VulkanBuffer>> pendingTemporaryBuffers;
    };

    struct AsyncUploadState : public UploadState {
        std::unique_ptr<VulkanCommandList> cmdList;
        VkSemaphore syncSemaphore = VK_NULL_HANDLE;
        VkFence syncFence = VK_NULL_HANDLE;
        bool isSubmitted = false; 
    };

    struct PerFrameUploadState : public UploadState {
        std::vector<UploadRequest> pendingUploads;
    };

    class VulkanUploadManager : public UploadManager {
    public:
        VulkanUploadManager(VulkanDevice& device, uint32_t framesInFlight, size_t ringBufferSize);
        ~VulkanUploadManager() override;

        void* mapForDeferredUpload(Buffer* dstBuffer, size_t size) override;
        void uploadBuffer(Buffer* dstBuffer, const void* data, size_t size, UploadMode mode = UploadMode::Deferred) override;
        
        std::vector<SemaphoreHandle> consumeAsyncSemaphores(const std::vector<Buffer*>& buffers) override;
        
        std::vector<UploadRequest> getAndClearPendingUploads() override;
        void beginFrame(uint64_t currentFrameIndex) override;

    private:
        VulkanDevice& m_device;
        uint32_t m_framesInFlight;
        uint32_t m_currentFrameIndex = 0;

        AsyncUploadState m_asyncState; // ImmediateとAsyncで共用
        std::vector<PerFrameUploadState> m_frameStates;
        std::unordered_map<Buffer*, VkSemaphore> m_pendingAsyncSemaphores; // バッファとセマフォの紐付け

        StagingAllocation allocateStagingSpace(UploadState& state, size_t size, size_t alignment = 4);
        void ensureAsyncReady(); 
        void flushAsync(Buffer* dstBuffer, UploadMode mode);
    };
}