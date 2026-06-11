#pragma once
#include <vulkan/vulkan.h>
#include "rhi/RHIcommon.hpp"

namespace rhi::vk {

class VulkanDevice;

class VulkanTimelineSemaphore {
public:
    VulkanTimelineSemaphore(VulkanDevice& device, QueueType queueType);
    ~VulkanTimelineSemaphore();

    // コピー・ムーブ禁止
    VulkanTimelineSemaphore(const VulkanTimelineSemaphore&) = delete;
    VulkanTimelineSemaphore& operator=(const VulkanTimelineSemaphore&) = delete;

    // CPU側で現在の完了値をクエリ
    uint64_t getCompletedValue() const;

    // CPU用：指定した値に達するまで待機 (timeoutNs はナノ秒)
    bool wait(uint64_t waitValue, uint64_t timeoutNs = UINT64_MAX);

    // GPU用：次の Submit でインクリメントするための値を確保
    uint64_t advanceAndGetNextValue() { return ++m_currentValue; }

    // 現在の確保済み値を取得 (次にSignal予定の値)
    uint64_t getCurrentValue() const { return m_currentValue; }

    // GPU用：Wait / Signal 用の VkSemaphoreSubmitInfo 構造体を生成 (Sync2準拠)
    VkSemaphoreSubmitInfo getWaitInfo(uint64_t waitValue, VkPipelineStageFlags2 stageMask) const;
    VkSemaphoreSubmitInfo getSignalInfo(uint64_t signalValue, VkPipelineStageFlags2 stageMask) const;

    QueueType getQueueType() const { return m_queueType; }
    VkSemaphore getHandle() const { return m_semaphore; }

private:
    VulkanDevice& m_device;
    QueueType m_queueType;
    VkSemaphore m_semaphore{VK_NULL_HANDLE};
    uint64_t m_currentValue{0}; // 次にSignalされる予定の値
};

} // namespace rhi::vk