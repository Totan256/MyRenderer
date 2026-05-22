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

    // アップロード用バッファの状態を管理する基底構造体
    struct UploadState {
        std::unique_ptr<VulkanBuffer> ringBuffer;
        size_t ringBufferSize = 0;
        size_t ringBufferOffset = 0;
        uint8_t* ringMappedPtr = nullptr;
        std::vector<std::unique_ptr<VulkanBuffer>> pendingTemporaryBuffers;
    };

    // ループ中の遅延アップロード用（フレームごとに持つ）
    struct PerFrameUploadState : public UploadState {
        std::unique_ptr<VulkanCommandList> deferredCmd;
        VkSemaphore transferSemaphore = VK_NULL_HANDLE;
        bool hasDeferredCommands = false;
    };

    class VulkanUploadManager : public UploadManager {
    public:
        VulkanUploadManager(VulkanDevice& device, uint32_t framesInFlight, size_t ringBufferSize);
        ~VulkanUploadManager() override;

        void* mapForUploadImmediate(Buffer* dstBuffer, size_t size) override;
        void uploadImmediate(Buffer* dstBuffer, const void* data, size_t size) override;
        void waitForImmediateUploads() override;

        void* mapForUploadDeferred(Buffer* dstBuffer, size_t size) override;
        void requestUploadDeferred(Buffer* dstBuffer, const void* data, size_t size) override;

        SemaphoreHandle flushDeferredUploads() override;
        void beginFrame(uint64_t currentFrameIndex) override;

    private:
        VulkanDevice& m_device;
        uint32_t m_framesInFlight;
        uint32_t m_currentFrameIndex = 0;

        // Immediate (即時) 用の独立したステート
        UploadState m_immediateState;
        std::unique_ptr<VulkanCommandList> m_immediateCmd;

        // Deferred (遅延) 用のフレームごとのステート
        std::vector<PerFrameUploadState> m_frameStates;

        // アライメントを指定してステージング領域を確保する内部関数
        StagingAllocation allocateStagingSpace(UploadState& state, size_t size, size_t alignment = 4);
        
        // std::unique_ptr<VulkanBuffer> m_ringBuffer;
        // size_t m_ringBufferSize;
        // size_t m_ringBufferOffset = 0;
        // uint8_t* m_ringMappedPtr = nullptr;

        // std::unique_ptr<VulkanCommandList> m_deferredCmd;
        // bool m_hasDeferredCommands = false;

        // // 同期用のVulkan Semaphore
        // VkSemaphore m_transferSemaphore = VK_NULL_HANDLE;

        // std::vector<std::unique_ptr<VulkanBuffer>> m_pendingTemporaryBuffers;

        // StagingAllocation allocateStagingSpace(size_t size);
    };
}