#include "VulkanUploadManager.hpp"
#include <cstring>
#include <iostream>

namespace rhi::vk {
    VulkanUploadManager::VulkanUploadManager(VulkanDevice& device, uint32_t framesInFlight, size_t ringBufferSize)
        : m_device(device), m_framesInFlight(framesInFlight) {
        
        // --- Immediate用の初期化 ---
        m_immediateState.ringBufferSize = ringBufferSize;
        m_immediateState.ringBuffer = std::make_unique<VulkanBuffer>(
            m_device, m_device.getAllocator(), ringBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        m_immediateState.ringMappedPtr = static_cast<uint8_t*>(m_immediateState.ringBuffer->map());
        m_immediateCmd = std::make_unique<VulkanCommandList>(m_device);
        m_immediateCmd->reset();
        m_immediateCmd->begin();

        // --- Deferred用の初期化 (フレーム数分) ---
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
            
            state.deferredCmd = std::make_unique<VulkanCommandList>(m_device);
            // 最初はリセット済みの状態にしておく（実際のbeginはbeginFrameで行う）

            // セマフォの作成
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            if (vkCreateSemaphore(m_device.getDevice(), &semaphoreInfo, nullptr, &state.transferSemaphore) != VK_SUCCESS) {
                throw std::runtime_error("failed to create transfer semaphore!");
            }
        }
    }

    VulkanUploadManager::~VulkanUploadManager() {
        waitForImmediateUploads();
        // m_ringBufferはunique_ptrにより自動破棄
        for (auto& state : m_frameStates) {
            if (state.transferSemaphore != VK_NULL_HANDLE)
                vkDestroySemaphore(m_device.getDevice(), state.transferSemaphore, nullptr);
        }
    }

    StagingAllocation VulkanUploadManager::allocateStagingSpace(UploadState& state, size_t size, size_t alignment) {
        // 閾値：リングバッファの4分の1を超える場合は専用バッファを作成
        const size_t THRESHOLD = state.ringBufferSize / 4;

        // 指定されたアライメントに合わせてオフセットを切り上げる
        size_t allocOffset = (state.ringBufferOffset + alignment - 1) & ~(alignment - 1);

        // 1. データが巨大、またはリングバッファに空きがない場合
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

        // 2. リングバッファを使用する場合
        void* ptr = state.ringMappedPtr + allocOffset;
        state.ringBufferOffset = allocOffset + size;

        return { state.ringBuffer.get(), allocOffset, ptr, false };
    }

    // =========================================================
    // Immediate Operations
    // =========================================================

    void* VulkanUploadManager::mapForUploadImmediate(Buffer* dstBuffer, size_t size) {
        StagingAllocation alloc = allocateStagingSpace(m_immediateState, size);
        m_immediateCmd->copyBuffer(alloc.buffer, dstBuffer, size, alloc.offset, 0);
        return alloc.mappedPtr;
    }

    void VulkanUploadManager::uploadImmediate(Buffer* dstBuffer, const void* data, size_t size) {
        StagingAllocation alloc = allocateStagingSpace(m_immediateState, size);
        std::memcpy(alloc.mappedPtr, data, size);
        m_immediateCmd->copyBuffer(alloc.buffer, dstBuffer, size, alloc.offset, 0);
    }

    void VulkanUploadManager::waitForImmediateUploads() {
        m_immediateCmd->end();
        m_immediateCmd->submitAndWait(); // 同期待機

        m_immediateState.pendingTemporaryBuffers.clear(); // 一時バッファの破棄
        m_immediateState.ringBufferOffset = 0;            // リングバッファのリセット
        
        m_immediateCmd->reset(); // 次の記録に備える
        m_immediateCmd->begin(); 
    }

    // =========================================================
    // Deferred Operations (Per Frame)
    // =========================================================

    void VulkanUploadManager::beginFrame(uint64_t currentFrameIndex) {
        m_currentFrameIndex = currentFrameIndex % m_framesInFlight;
        auto& state = m_frameStates[m_currentFrameIndex];
        
        // この時点で、このフレームインデックスの過去のGPU実行は完了しているはずなのでクリア
        state.pendingTemporaryBuffers.clear();
        state.ringBufferOffset = 0;
        state.hasDeferredCommands = false;

        // コマンドバッファをリセットして記録開始状態にする
        state.deferredCmd->reset();
        state.deferredCmd->begin();
    }

    void VulkanUploadManager::requestUploadDeferred(Buffer* dstBuffer, const void* data, size_t size) {
        auto& state = m_frameStates[m_currentFrameIndex];
        StagingAllocation alloc = allocateStagingSpace(state, size);
        std::memcpy(alloc.mappedPtr, data, size);
        state.deferredCmd->copyBuffer(alloc.buffer, dstBuffer, size, alloc.offset, 0);
        state.hasDeferredCommands = true;
    }

    void* VulkanUploadManager::mapForUploadDeferred(Buffer* dstBuffer, size_t size) {
        auto& state = m_frameStates[m_currentFrameIndex];
        StagingAllocation alloc = allocateStagingSpace(state, size);
        state.deferredCmd->copyBuffer(alloc.buffer, dstBuffer, size, alloc.offset, 0);
        state.hasDeferredCommands = true;
        return alloc.mappedPtr;
    }

    SemaphoreHandle VulkanUploadManager::flushDeferredUploads() {
        auto& state = m_frameStates[m_currentFrameIndex];
        if (!state.hasDeferredCommands) return nullptr;
        // 記録を終了してGPUへ送信
        state.deferredCmd->end();
        state.deferredCmd->submit(nullptr, state.transferSemaphore);
        state.hasDeferredCommands = false;
        // 抽象ハンドルとしてセマフォを返す
        return static_cast<SemaphoreHandle>(state.transferSemaphore);
    }
    // void VulkanUploadManager::garbageCollect(uint64_t completedFrameId) {
    //     // RenderGraphの実行完了後に呼ばれる
    //     m_pendingTemporaryBuffers.clear();
    //     m_ringBufferOffset = 0; // リングバッファリセット
    // }
}