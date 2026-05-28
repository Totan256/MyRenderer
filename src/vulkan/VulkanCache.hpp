#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan.h>
#include "ShaderReflection.hpp"
#include "VulkanComputePipeline.hpp"
#include "VulkanGraphicsPipeline.hpp"

namespace rhi::vk {
    // コンパイル済みSPIR-Vとリフレクション情報のキャッシュ
    struct ShaderData {
        std::vector<uint32_t> spirv;
        ShaderReflectionData reflection;
    };

    class VulkanShaderCache {
    public:
        const ShaderData& getOrCreateShader(const std::string& path, shaderc_shader_kind kind);
    private:
        std::unordered_map<std::string, ShaderData> m_cache;
    };

    class VulkanPipelineCache {
    public:
        VulkanPipelineCache(VulkanDevice& device) : m_device(device) {}

        VulkanComputePipeline* getOrCreateComputePipeline(const std::string& shaderPath, uint32_t pushContentsSize);
        
        VulkanGraphicsPipeline* getOrCreateGraphicsPipeline(
            const std::string& vertPath, const std::string& fragPath,
            const std::vector<VkFormat>& colorFormats, VkFormat depthFormat, uint32_t pushContentsSize);

    private:
        VulkanDevice& m_device;
        std::unordered_map<std::string, std::unique_ptr<VulkanComputePipeline>> m_computePipelines;
        std::unordered_map<std::string, std::unique_ptr<VulkanGraphicsPipeline>> m_graphicsPipelines;
    };
}