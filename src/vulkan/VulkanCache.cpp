#include "VulkanCache.hpp"

namespace rhi::vk {
    const ShaderData& VulkanShaderCache::getOrCreateShader(const std::string& path, shaderc_shader_kind kind) {
        if (m_cache.find(path) == m_cache.end()) {
            ShaderData data;
            // ※ VulkanComputePipeline::compileGLSLToSPIRV などを再利用
            data.spirv = VulkanComputePipeline::compileGLSLToSPIRV(path, kind);
            data.reflection = ShaderReflection::reflect(data.spirv);
            m_cache[path] = std::move(data);
        }
        return m_cache[path];
    }

    VulkanComputePipeline* VulkanPipelineCache::getOrCreateComputePipeline(const std::string& shaderPath, uint32_t pushContentsSize){
        if (m_computePipelines.find(shaderPath) == m_computePipelines.end()) {
            m_computePipelines[shaderPath] = std::make_unique<VulkanComputePipeline>(m_device, shaderPath, pushContentsSize);
        }
        return m_computePipelines[shaderPath].get();
    }

    
    VulkanGraphicsPipeline* VulkanPipelineCache::getOrCreateGraphicsPipeline(
        const std::string& vertPath, const std::string& fragPath,
        const std::vector<VkFormat>& colorFormats, VkFormat depthFormat, uint32_t pushContentsSize){
        // フォーマットを含めた一意のキャッシュキーを生成
        std::string key = vertPath + "|" + fragPath + "|C:";
        for (auto fmt : colorFormats) key += std::to_string(fmt) + ",";
        key += "D:" + std::to_string(depthFormat);

        if (m_graphicsPipelines.find(key) == m_graphicsPipelines.end()) {
            m_graphicsPipelines[key] = std::make_unique<VulkanGraphicsPipeline>(
                m_device, vertPath, fragPath, colorFormats, depthFormat, pushContentsSize);
        }
        return m_graphicsPipelines[key].get();
    }
}