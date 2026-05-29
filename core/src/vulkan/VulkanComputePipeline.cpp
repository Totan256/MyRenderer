#include "VulkanComputePipeline.hpp"
#include "utils/ShaderCompiler.hpp"
#include <stdexcept>
#include <iostream>

namespace rhi::vk{
    VulkanComputePipeline::VulkanComputePipeline(VulkanDevice& device, const std::string& shaderPath, uint32_t pushContentsSize, VkPipelineCache cache)
        : m_device(device), m_pushContentsSize(pushContentsSize) {
        
        createPipeline(shaderPath, cache);
    }

    VulkanComputePipeline::~VulkanComputePipeline() {
        VkDevice device = m_device.getDevice();

        if (m_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, m_pipeline, nullptr);
        }
        if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        }
        if (m_shaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, m_shaderModule, nullptr);
        }
    }

    void VulkanComputePipeline::createPipeline(const std::string& shaderPath, VkPipelineCache cache) {
        // --- 1. シェーダーモジュールの作成 ---
        std::vector<uint32_t> shaderCode;
        if(shaderPath.ends_with(".spv")) {
            shaderCode = ShaderCompiler::readFile(shaderPath);
        } else if(shaderPath.ends_with(".comp")) {
            shaderCode = ShaderCompiler::compileGLSLToSPIRV(shaderPath, shaderc_compute_shader);
        } else {
            throw std::runtime_error("Unsupported shader file extension: " + shaderPath);
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size() * sizeof(uint32_t);
        createInfo.pCode = shaderCode.data();

        if (vkCreateShaderModule(m_device.getDevice(), &createInfo, nullptr, &m_shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shader module!");
        }

        VkDescriptorSetLayout layouts[] = { m_device.getBindlessLayout() };

        // --- 2. パイプラインレイアウトの作成 ---
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = m_pushContentsSize;
        
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = layouts;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(m_device.getDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        // --- 3. Compute Pipeline の作成 ---
        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = m_shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = m_pipelineLayout;

        // PipelineCache を適用
        if (vkCreateComputePipelines(m_device.getDevice(), cache, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create compute pipeline!");
        }
    }
}