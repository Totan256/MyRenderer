#include "VulkanSemaphore.hpp"
#include "VulkanDevice.hpp"
#include <stdexcept>

namespace rhi::vk {

VulkanTimelineSemaphore::VulkanTimelineSemaphore(VulkanDevice& device, QueueType queueType)
    : m_device(device), m_queueType(queueType)
{
    VkSemaphoreTypeCreateInfo timelineCreateInfo{};
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.pNext = nullptr;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = 0;

    VkSemaphoreCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &timelineCreateInfo;
    createInfo.flags = 0;

    if (vkCreateSemaphore(m_device.getDevice(), &createInfo, nullptr, &m_semaphore) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create timeline semaphore");
    }
}

VulkanTimelineSemaphore::~VulkanTimelineSemaphore() {
    if (m_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device.getDevice(), m_semaphore, nullptr);
    }
}

uint64_t VulkanTimelineSemaphore::getCompletedValue() const {
    uint64_t value = 0;
    vkGetSemaphoreCounterValue(m_device.getDevice(), m_semaphore, &value);
    return value;
}

bool VulkanTimelineSemaphore::wait(uint64_t waitValue, uint64_t timeoutNs) {
    if (getCompletedValue() >= waitValue) {
        return true; // 既に完了済み
    }

    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.pNext = nullptr;
    waitInfo.flags = 0;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_semaphore;
    waitInfo.pValues = &waitValue;

    VkResult result = vkWaitSemaphores(m_device.getDevice(), &waitInfo, timeoutNs);
    if (result == VK_SUCCESS) {
        return true;
    } else if (result == VK_TIMEOUT) {
        return false;
    } else {
        throw std::runtime_error("Failed to wait on timeline semaphore");
    }
}

VkSemaphoreSubmitInfo VulkanTimelineSemaphore::getWaitInfo(uint64_t waitValue, VkPipelineStageFlags2 stageMask) const {
    VkSemaphoreSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    submitInfo.semaphore = m_semaphore;
    submitInfo.value = waitValue;
    submitInfo.stageMask = stageMask;
    submitInfo.deviceIndex = 0;
    return submitInfo;
}

VkSemaphoreSubmitInfo VulkanTimelineSemaphore::getSignalInfo(uint64_t signalValue, VkPipelineStageFlags2 stageMask) const {
    VkSemaphoreSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    submitInfo.semaphore = m_semaphore;
    submitInfo.value = signalValue;
    submitInfo.stageMask = stageMask;
    submitInfo.deviceIndex = 0;
    return submitInfo;
}

} // namespace rhi::vk