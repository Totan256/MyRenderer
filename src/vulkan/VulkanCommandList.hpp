#pragma once

#include <vulkan/vulkan.h>
#include "RHIcommon.hpp"
#include "VulkanDevice.hpp"
#include "VulkanComputePipeline.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanImage.hpp"

class VulkanCommandList {
public:
    VulkanCommandList(VulkanDevice& device);
    ~VulkanCommandList();
    
    // コマンド記録の開始と終了
    void begin();
    void end();

    // コマンドの送信と完了待機（オフラインレンダリング用）
    void submitAndWait();

    // --- コマンド記録用メソッド ---
    
    // パイプラインをセットする
    void bindPipeline(VulkanComputePipeline& pipeline);

    // ディスクリプタセット（リソース）をセットする
    void bindDescriptorSet(VkDescriptorSet descriptorSet);
    void bindGlobalDescriptorSet();

    // 計算を実行する (Dispatch)
    void dispatch(uint32_t x, uint32_t y, uint32_t z);

    VkCommandBuffer getCommandBuffer()const {return m_commandBuffer;}

    void setPushResource(uint32_t offset, const VulkanBuffer& resource) {
        uint32_t index = resource.getBindlessIndex();
        setPushData(offset, sizeof(uint32_t), &index);
    }
    void setPushResource(uint32_t offset, const VulkanImage& resource) {
        uint32_t index = resource.getBindlessIndex();
        setPushData(offset, sizeof(uint32_t), &index);
    }
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
private:
    VulkanDevice& m_device;
    VulkanComputePipeline* m_pipeline = nullptr;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE; // 完了待ちのためのフェンス
};