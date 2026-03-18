#include "VulkanCommandList.hpp"
#include "RHI.hpp"
#include <stdexcept>
#include <iostream>

VulkanCommandList::VulkanCommandList(VulkanDevice& device) : m_device(device) {
    VkDevice logicalDevice = m_device.getDevice();

    // 1. コマンドプールの作成
    // RESET_COMMAND_BUFFER_BIT: バッファを個別にリセット可能にする
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_device.getComputeQueueFamilyIndex();

    if (vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }

    // 2. コマンドバッファの確保
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, &m_commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }

    // 3. フェンス（同期用）の作成
    // SIGNALED_BIT: 最初から「完了状態」にしておく（初回waitで詰まらないため）
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateFence(logicalDevice, &fenceInfo, nullptr, &m_fence) != VK_SUCCESS) {
        throw std::runtime_error("failed to create fence!");
    }
}

VulkanCommandList::~VulkanCommandList() {
    VkDevice logicalDevice = m_device.getDevice();
    
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(logicalDevice, m_fence, nullptr);
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(logicalDevice, m_commandPool, nullptr);
    }
    // コマンドバッファはプール破棄時に一緒に消えるので解放不要
}

void VulkanCommandList::begin() {
    // フェンスがシグナル状態（前回の実行完了）になるまで待つ
    // タイムアウトは最大値
    vkWaitForFences(m_device.getDevice(), 1, &m_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device.getDevice(), 1, &m_fence); // 非シグナル状態に戻す

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    // ワンタイムサブミット（1回記録して1回実行して捨てる使い方）のヒント
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }
}

void VulkanCommandList::end() {
    if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void VulkanCommandList::submitAndWait() {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;

    // フェンスを渡して実行。完了するとフェンスがシグナル状態になる
    if (vkQueueSubmit(m_device.getComputeQueue(), 1, &submitInfo, m_fence) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    // 完了を待つ（オフラインレンダリングなので）
    // ToDo：りあうタイムレンダリング用に書き換え
    vkWaitForFences(m_device.getDevice(), 1, &m_fence, VK_TRUE, UINT64_MAX);
}

void VulkanCommandList::bindPipeline(VulkanComputePipeline& pipeline) {
    m_pipeline = &pipeline;
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getPipeline());
}

void VulkanCommandList::bindDescriptorSet(VkDescriptorSet descriptorSet) {
    vkCmdBindDescriptorSets(
        m_commandBuffer, 
        VK_PIPELINE_BIND_POINT_COMPUTE, 
        m_pipeline->getPipelineLayout(), 
        0, 1, &descriptorSet, 0, nullptr
    );
}

void VulkanCommandList::bindGlobalDescriptorSet() {
    VkDescriptorSet globalSet = m_device.getBindlessDescriptorSet();
    vkCmdBindDescriptorSets(
        m_commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipeline->getPipelineLayout(),
        0, 1, &globalSet, 0, nullptr);
}

void VulkanCommandList::dispatch(uint32_t x, uint32_t y, uint32_t z) {
    vkCmdDispatch(m_commandBuffer, x, y, z);
}
