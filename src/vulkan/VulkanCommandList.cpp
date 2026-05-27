#include "VulkanCommandList.hpp"
// #include "RHI.hpp"
#include <stdexcept>
#include <iostream>
namespace rhi::vk {
    VulkanCommandList::VulkanCommandList(VulkanDevice& device, QueueType queueType)
        : m_device(device), m_queueType(queueType) {

        VkDevice logicalDevice = m_device.getDevice();

        // 1. コマンドプールの作成
        // RESET_COMMAND_BUFFER_BIT: バッファを個別にリセット可能にする
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_device.getQueueFamilyIndex(queueType);

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
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
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

    void VulkanCommandList::submit(SemaphoreHandle waitSemaphore, SemaphoreHandle signalSemaphore) {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffer;

        VkSemaphore vkWaitSem = static_cast<VkSemaphore>(waitSemaphore);
        VkSemaphore vkSignalSem = static_cast<VkSemaphore>(signalSemaphore);
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT}; 
        
        if (vkWaitSem != VK_NULL_HANDLE) {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &vkWaitSem;
            submitInfo.pWaitDstStageMask = waitStages;
        }

        if (vkSignalSem != VK_NULL_HANDLE) {
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &vkSignalSem;
        }

        // m_fence を渡して実行。完了するとフェンスがシグナル状態になるが、CPUでは待たない
        if (vkQueueSubmit(m_device.getQueue(m_queueType), 1, &submitInfo, m_fence) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit command buffer!");
        }
    }

    void VulkanCommandList::wait() {
        vkWaitForFences(m_device.getDevice(), 1, &m_fence, VK_TRUE, UINT64_MAX);
    }
    void VulkanCommandList::reset() {
        vkResetFences(m_device.getDevice(), 1, &m_fence); 
    }

    void VulkanCommandList::submitAndWait() {
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffer;

        // フェンスを渡して実行。完了するとフェンスがシグナル状態になる
        if (vkQueueSubmit(m_device.getQueue(m_queueType), 1, &submitInfo, m_fence) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        vkWaitForFences(m_device.getDevice(), 1, &m_fence, VK_TRUE, UINT64_MAX);
    }

    void VulkanCommandList::beginRendering(const std::vector<VkImageView>& colorViews, VkImageView depthView, uint32_t width, uint32_t height) {
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        for (auto view : colorViews) {
            VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            colorAttachment.imageView = view;
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Todo 実験用。後でRenderGraphから設定可能にする
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };
            colorAttachments.push_back(colorAttachment);
        }

        VkRenderingAttachmentInfo depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        if (depthView != VK_NULL_HANDLE) {
            depthAttachment.imageView = depthView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil = { 1.0f, 0 };
        }

        VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        renderingInfo.renderArea = { {0, 0}, {width, height} };
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        renderingInfo.pColorAttachments = colorAttachments.data();
        if (depthView != VK_NULL_HANDLE) {
            renderingInfo.pDepthAttachment = &depthAttachment;
        }

        vkCmdBeginRendering(m_commandBuffer, &renderingInfo);

        // Viewport / Scissor は画面サイズ全体をデフォルトとする (必要なら後で外に出す)
        VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{ {0, 0}, {width, height} };
        vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
    }

    void VulkanCommandList::endRendering() { vkCmdEndRendering(m_commandBuffer); }

    // Extended Dynamic States (Vulkan 1.3 / EDS拡張)
    void VulkanCommandList::setCullMode(VkCullModeFlags cullMode) { vkCmdSetCullMode(m_commandBuffer, cullMode); }
    void VulkanCommandList::setDepthTestEnable(bool enable) { vkCmdSetDepthTestEnable(m_commandBuffer, enable ? VK_TRUE : VK_FALSE); }
    void VulkanCommandList::setDepthWriteEnable(bool enable) { vkCmdSetDepthWriteEnable(m_commandBuffer, enable ? VK_TRUE : VK_FALSE); }
    void VulkanCommandList::setDepthCompareOp(VkCompareOp op) { vkCmdSetDepthCompareOp(m_commandBuffer, op); }

    void VulkanCommandList::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) { vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance); }

    void VulkanCommandList::drawIndexedIndirectCount(VkBuffer indirectBuffer, VkDeviceSize indirectOffset, VkBuffer countBuffer, VkDeviceSize countOffset, uint32_t maxDrawCount) {
        // ※ Vulkan 1.2 コア機能
        uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
        vkCmdDrawIndexedIndirectCount(m_commandBuffer, indirectBuffer, indirectOffset, countBuffer, countOffset, maxDrawCount, stride);
    }

    void VulkanCommandList::bindPipeline(VulkanComputePipeline& pipeline) {
        m_pipeline = &pipeline;
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getPipeline());
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

    void VulkanCommandList::copyBuffer(rhi::Buffer* src, rhi::Buffer* dst, size_t size, size_t srcOffset, size_t dstOffset) {
        auto& vkSrc = static_cast<VulkanBuffer&>(*src);
        auto& vkDst = static_cast<VulkanBuffer&>(*dst);

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = dstOffset;
        copyRegion.size = size;

        vkCmdCopyBuffer(m_commandBuffer, vkSrc.getNativeBuffer(), vkDst.getNativeBuffer(), 1, &copyRegion);
    }
}