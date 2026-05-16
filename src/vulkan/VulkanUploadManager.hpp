#pragma once
#include "rhi/UploadManager.hpp"
#include "VulkanDevice.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanCommandList.hpp"
#include <memory>
#include <vector>

namespace rhi::vk {
    // ステージングメモリの割り当て結果
    struct StagingAllocation {
        VulkanBuffer* buffer;
        size_t offset;
        void* mappedPtr;
        bool isTemporary; // trueならリングバッファ外の専用バッファ
    };

    class VulkanUploadManager : public UploadManager {
    public:
        VulkanUploadManager(VulkanDevice& device, size_t ringBufferSize);
        ~VulkanUploadManager() override;

        void uploadImmediate(Buffer* dstBuffer, const void* data, size_t size) override;
        void waitForImmediateUploads() override;

        void requestUploadDeferred(Buffer* dstBuffer, std::vector<uint8_t>&& data) override;
        void requestUploadDeferred(Buffer* dstBuffer, const void* data, size_t size) override;

        void flushDeferredUploads() override;
        void garbageCollect(uint64_t completedFrameId) override;

    private:
        VulkanDevice& m_device;
        
        // リングバッファ関連
        std::unique_ptr<VulkanBuffer> m_ringBuffer;
        size_t m_ringBufferSize;
        size_t m_ringBufferOffset = 0;
        uint8_t* m_ringMappedPtr = nullptr;

        // コマンドリスト
        std::unique_ptr<VulkanCommandList> m_immediateCmd;
        std::unique_ptr<VulkanCommandList> m_deferredCmd;
        bool m_hasDeferredCommands = false;

        // 破棄待ちリソース
        std::vector<std::unique_ptr<VulkanBuffer>> m_pendingTemporaryBuffers;
        std::vector<std::vector<uint8_t>> m_pendingDataKeepAlive;

        // メモリ割り当ての分岐ロジック
        StagingAllocation allocateStagingSpace(size_t size);
    };
}