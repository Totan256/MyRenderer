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

    struct AsyncUploadContext {
        std::unique_ptr<VulkanCommandList> cmdList;
        VkFence syncFence = VK_NULL_HANDLE;
        
        std::vector<std::unique_ptr<VulkanBuffer>> retainedBuffers;
        std::unique_ptr<VulkanBuffer> retainedRingBuffer;
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

        // SyncPoint を返すように変更
        SyncPoint submitUploadsAsync() override;
        void waitUploads() override;

        // RenderGraph 側に渡すための SyncPoint 回収
        std::vector<SyncPoint> consumeAsyncSyncPoints() override;
        
        std::vector<UploadRequest> getAndClearPendingUploads() override;
        std::vector<rhi::ImageUploadRequest> getAndClearPendingImageUploads() override;
        void beginFrame(uint64_t currentFrameIndex) override;
    private:
        VulkanDevice& m_device;
        uint32_t m_framesInFlight;
        uint32_t m_currentFrameIndex = 0;

        std::vector<std::unique_ptr<AsyncUploadContext>> m_asyncContextPool;
        std::vector<AsyncUploadContext*> m_activeAsyncContexts;
        std::vector<PerFrameUploadState> m_frameStates;
        
        // グラフで処理するための未回収 SyncPoint のリスト
        std::vector<SyncPoint> m_pendingAsyncSyncPoints;

        std::vector<std::unique_ptr<VulkanBuffer>> m_freeRingBuffers;

        // ※ m_frameSemaphoresToRelease はタイムライン採用により不要になったため削除

        AsyncUploadContext* getOrCreateAsyncContext();
        void retireCompletedAsyncContexts(bool waitAll = false);

        StagingAllocation allocateStagingSpace(UploadState& state, size_t size, size_t alignment = 4);
        std::unique_ptr<VulkanBuffer> getOrCreateRingBuffer(size_t size);
        void resetRingBufferState(UploadState& state, std::unique_ptr<VulkanBuffer> newBuffer);
    };
}