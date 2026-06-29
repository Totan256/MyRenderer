#pragma once

#include <vulkan/vulkan.h>
#include "rhi/RHIcommon.hpp"
#include "VulkanDevice.hpp"
#include "VulkanComputePipeline.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanImage.hpp"
#include "rhi/CommandList.hpp"

namespace rhi::vk{

    class VulkanCommandList : public CommandList {
    public:
        struct RenderAttachment {
            VkImageView view;
            VkAttachmentLoadOp loadOp;
            VkAttachmentStoreOp storeOp;
            VkClearValue clearValue;
        };
        VulkanCommandList(VulkanDevice& device, QueueType queueType);
        VulkanCommandList(VulkanDevice& device, QueueType queueType, VkCommandPool pool);
        ~VulkanCommandList() override;
        
        // コマンド記録の開始と終了
        void begin() override;
        void end() override;
        void submit(SemaphoreHandle waitSemaphore = nullptr, SemaphoreHandle signalSemaphore = nullptr) override;
        void wait() override;
        void reset() override;
        // コマンドの送信と完了待機（オフラインレンダリング用）
        void submitAndWait() override;

        // プロファイリング用
        void resetQueryPool(GPUProfiler* profiler, uint32_t firstQuery, uint32_t queryCount) override;
        void writeTimestamp(GPUProfiler* profiler, uint32_t queryIndex, PipelineStage stage) override;

        // Dynamic Rendering の開始・終了
        void beginRendering(const std::vector<RenderAttachment>& colorAtts, const RenderAttachment* depthAtt, uint32_t width, uint32_t height);
        void setTopology(VkPrimitiveTopology topology);
        void setFrontFace(VkFrontFace frontFace);
        void endRendering();
        // Extended Dynamic State の設定
        void setCullMode(VkCullModeFlags cullMode);
        void setDepthTestEnable(bool enable);
        void setDepthWriteEnable(bool enable);
        void setDepthCompareOp(VkCompareOp op);
        // 描画コマンド
        void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
        void drawIndexedIndirectCount(VkBuffer indirectBuffer, VkDeviceSize indirectOffset, VkBuffer countBuffer, VkDeviceSize countOffset, uint32_t maxDrawCount);

        // --- コマンド記録用メソッド ---
        
        void bindPipeline(VulkanComputePipeline& pipeline);
        void bindGlobalDescriptorSet();

        void dispatch(uint32_t x, uint32_t y, uint32_t z);
        VkCommandBuffer getCommandBuffer()const {return m_commandBuffer;}

        void setPushData(uint32_t offset, uint32_t size, const void* data) {
            // 現在のパイプラインの制限をチェック
            if (offset + size > m_pipeline->getPushContentsSize()) {
                throw std::runtime_error("Push constants limit exceeded");
            }
            vkCmdPushConstants(
                m_commandBuffer, 
                m_pipeline->getPipelineLayout(), 
                VK_SHADER_STAGE_COMPUTE_BIT, 
                offset, 
                size,
                data
            );
        }
        void copyBuffer(rhi::Buffer* src, rhi::Buffer* dst, size_t size, size_t srcOffset = 0, size_t dstOffset = 0) override;
    private:
        VulkanDevice& m_device;
        QueueType m_queueType;
        VulkanComputePipeline* m_pipeline = nullptr;
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
        VkFence m_fence = VK_NULL_HANDLE; // 完了待ちのためのフェンス

        bool m_ownsPool = false; // コマンドプールを所有しているか（外部から渡された場合はfalse）
    };
}