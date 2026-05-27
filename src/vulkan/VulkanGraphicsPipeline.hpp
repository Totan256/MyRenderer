#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include "VulkanDevice.hpp"

namespace rhi::vk {
    class VulkanGraphicsPipeline {
    public:
        // コンストラクタ: PSOを一意に決める最小限のパラメータのみを受け取る
        VulkanGraphicsPipeline(
            VulkanDevice& device,
            const std::string& vertShaderPath,
            const std::string& fragShaderPath,
            const std::vector<VkFormat>& colorFormats,
            VkFormat depthFormat,
            uint32_t pushContentsSize
        );
        ~VulkanGraphicsPipeline();

        VkPipeline getPipeline() const { return m_pipeline; }
        VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }

    private:
        VulkanDevice& m_device;
        uint32_t m_pushContentsSize;

        VkPipeline m_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
        VkShaderModule m_vertShaderModule = VK_NULL_HANDLE;
        VkShaderModule m_fragShaderModule = VK_NULL_HANDLE;

        void createPipeline(const std::string& vertPath, const std::string& fragPath, const std::vector<VkFormat>& colorFormats, VkFormat depthFormat);
    };
}