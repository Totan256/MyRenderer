#pragma once

#include <vulkan/vulkan.h>
#include "VulkanDevice.hpp"
#include "VulkanComputePipeline.hpp"

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
    void bindPipeline(const VulkanComputePipeline& pipeline);

    // ディスクリプタセット（リソース）をセットする
    void bindDescriptorSet(const VulkanComputePipeline& pipeline, VkDescriptorSet descriptorSet);
    void bindGlobalDescriptorSet(const VulkanComputePipeline& pipeline);

    // 計算を実行する (Dispatch)
    void dispatch(uint32_t x, uint32_t y, uint32_t z);

    VkCommandBuffer getCommandBuffer()const {return m_commandBuffer;}

private:
    VulkanDevice& m_device;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE; // 完了待ちのための信号機
};