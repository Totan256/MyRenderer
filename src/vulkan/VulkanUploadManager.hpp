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
        std::vector<rhi::ImageUploadRequest> pendingImageUploads;
    };

    class VulkanUploadManager : public UploadManager {
    public:
        VulkanUploadManager(VulkanDevice& device, uint32_t framesInFlight, size_t ringBufferSize);
        ~VulkanUploadManager() override;

        void enqueueBufferUpload(Buffer* dstBuffer, const void* data, size_t size, size_t dstOffset = 0) override;
        void enqueueImageUpload(Image* dstImage, const void* data, size_t size, uint32_t width, uint32_t height, uint32_t mipLevels) override;

        // 非同期実行と待機
        SemaphoreHandle submitUploadsAsync() override;
        void waitUploads() override;

        // RenderGraph実行時に未回収のセマフォを全て回収する
        std::vector<SemaphoreHandle> consumeAsyncSemaphores() override;


        
        std::vector<UploadRequest> getAndClearPendingUploads() override;
        std::vector<rhi::ImageUploadRequest> getAndClearPendingImageUploads() override;
        void beginFrame(uint64_t currentFrameIndex) override;
    private:
        VulkanDevice& m_device;
        uint32_t m_framesInFlight;
        uint32_t m_currentFrameIndex = 0;

        AsyncUploadState m_asyncState; // ImmediateとAsyncで共用
        std::vector<PerFrameUploadState> m_frameStates;
        std::vector<VkSemaphore> m_pendingAsyncSemaphores;

        StagingAllocation allocateStagingSpace(UploadState& state, size_t size, size_t alignment = 4);
        void ensureAsyncReady();
    };
}