#include "VulkanUploadManager.hpp"
#include <cstring>
#include <iostream>

namespace rhi::vk {
    VulkanUploadManager::VulkanUploadManager(VulkanDevice& device, size_t ringBufferSize)
        : m_device(device), m_ringBufferSize(ringBufferSize) {
        
        // 常時マップ済みの巨大なリングバッファを作成
        m_ringBuffer = std::make_unique<VulkanBuffer>(
            m_device, m_device.getAllocator(), ringBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU // 永続マッピングされる
        );
        m_ringMappedPtr = static_cast<uint8_t*>(m_ringBuffer->map());

        // コマンドリストの作成
        m_immediateCmd = std::make_unique<VulkanCommandList>(m_device);
        m_deferredCmd = std::make_unique<VulkanCommandList>(m_device);
        
        m_immediateCmd->begin();
        m_deferredCmd->begin();
    }

    VulkanUploadManager::~VulkanUploadManager() {
        waitForImmediateUploads();
        // m_ringBufferはunique_ptrにより自動破棄
    }

    StagingAllocation VulkanUploadManager::allocateStagingSpace(size_t size) {
        // 閾値：リングバッファの4分の1を超える場合は専用バッファを作成
        const size_t THRESHOLD = m_ringBufferSize / 4;

        // 1. データが巨大、またはリングバッファに空きがない場合
        if (size > THRESHOLD || m_ringBufferOffset + size > m_ringBufferSize) {
            auto tempBuf = std::make_unique<VulkanBuffer>(
                m_device, m_device.getAllocator(), size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU
            );
            void* ptr = tempBuf->map();
            VulkanBuffer* rawPtr = tempBuf.get();
            m_pendingTemporaryBuffers.push_back(std::move(tempBuf));
            
            return { rawPtr, 0, ptr, true };
        }

        // 2. リングバッファを使用する場合
        size_t allocOffset = m_ringBufferOffset;
        void* ptr = m_ringMappedPtr + allocOffset;
        
        // オフセットを進める（Vulkanのコピーアライメントを考慮して4の倍数に切り上げ）
        m_ringBufferOffset = (m_ringBufferOffset + size + 3) & ~3;

        return { m_ringBuffer.get(), allocOffset, ptr, false };
    }

    void VulkanUploadManager::uploadImmediate(Buffer* dstBuffer, const void* data, size_t size) {
        StagingAllocation alloc = allocateStagingSpace(size);
        std::memcpy(alloc.mappedPtr, data, size);

        m_immediateCmd->copyBuffer(alloc.buffer, dstBuffer, size, alloc.offset, 0);
    }

    void VulkanUploadManager::waitForImmediateUploads() {
        m_immediateCmd->end();
        m_immediateCmd->submitAndWait(); // 同期待機

        m_pendingTemporaryBuffers.clear(); // 一時バッファの破棄
        m_ringBufferOffset = 0;            // リングバッファのリセット
        
        m_immediateCmd->begin(); // 次の記録に備える
    }

    void VulkanUploadManager::requestUploadDeferred(Buffer* dstBuffer, const void* data, size_t size) {
        StagingAllocation alloc = allocateStagingSpace(size);
        std::memcpy(alloc.mappedPtr, data, size);
        m_deferredCmd->copyBuffer(alloc.buffer, dstBuffer, size, alloc.offset, 0);
        m_hasDeferredCommands = true;
    }

    void VulkanUploadManager::requestUploadDeferred(Buffer* dstBuffer, std::vector<uint8_t>&& data) {
        // Zero-Copyアプローチ（ただしGPUからは読めないので、今回は単純化のためにコピーにフォールバックします）
        // ※厳密なZero-Copyを行うには、アロケータをバイパスして直接Vulkanバッファ化する必要があります
        requestUploadDeferred(dstBuffer, data.data(), data.size());
        m_pendingDataKeepAlive.push_back(std::move(data)); // 寿命だけ保持
    }

    void VulkanUploadManager::flushDeferredUploads() {
        if (!m_hasDeferredCommands) return;

        m_deferredCmd->end();
        
        // ToDo: 本格的な非同期化時は、ここで Semaphore を Signal し、RenderGraph で Wait する。
        // 現状はシンプルに SubmitAndWait で動作確認できるようにします。
        m_deferredCmd->submitAndWait();

        m_hasDeferredCommands = false;
        m_deferredCmd->begin();
    }

    void VulkanUploadManager::garbageCollect(uint64_t completedFrameId) {
        // RenderGraphの実行完了後に呼ばれる
        m_pendingTemporaryBuffers.clear();
        m_pendingDataKeepAlive.clear();
        m_ringBufferOffset = 0; // リングバッファリセット
    }
}