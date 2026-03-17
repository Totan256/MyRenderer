#pragma once

#include <vulkan/vulkan.h>
#include <shaderc/shaderc.hpp>
#include <string>
#include <vector>
#include "VulkanDevice.hpp"

class VulkanComputePipeline {
public:
    // コンストラクタでパイプライン構築まで行う
    VulkanComputePipeline(VulkanDevice& device, const std::string& shaderPath, uint32_t pushContentsSize);
    ~VulkanComputePipeline();

    // Vulkanハンドル取得
    VkPipeline getPipeline() const { return m_pipeline; }
    VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }
    //VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }

    uint32_t getPushContentsSize() const {return m_pushContentsSize;}
private:
    VulkanDevice& m_device; // デバイスへの参照を保持
    const uint32_t m_pushContentsSize;
    
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    //VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkShaderModule m_shaderModule = VK_NULL_HANDLE;

    // ファイル読み込みヘルパー
    static std::vector<uint32_t> readFile(const std::string& filename);
    static std::vector<uint32_t> compileGLSLToSPIRV(const std::string& shaderPath);

    void createDescriptorSetLayout();
    void createPipeline(const std::string& shaderPath);
};