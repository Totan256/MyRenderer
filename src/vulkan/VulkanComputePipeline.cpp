#include "VulkanComputePipeline.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace rhi::vk{
    VulkanComputePipeline::VulkanComputePipeline(VulkanDevice& device, const std::string& shaderPath, uint32_t pushContentsSize)
        : m_device(device), m_pushContentsSize(pushContentsSize){
        
        // 1. ディスクリプタセットレイアウトの作成（Binding=0にStorageBufferがあることを定義）
        createDescriptorSetLayout();

        // 2. パイプラインの作成（シェーダー読み込み -> パイプラインレイアウト -> パイプライン）
        createPipeline(shaderPath);
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

    void VulkanComputePipeline::createDescriptorSetLayout() {
        std::vector<VkDescriptorSetLayoutBinding> bindings;

        // Binding 0: Storage Image (画像出力)
        VkDescriptorSetLayoutBinding outputBinding{};
        outputBinding.binding = 0;
        outputBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outputBinding.descriptorCount = 1;
        outputBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(outputBinding);

        // Binding 1: Uniform Buffer (シーンデータ)
        VkDescriptorSetLayoutBinding sceneBinding{};
        sceneBinding.binding = 1;
        sceneBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sceneBinding.descriptorCount = 1;
        sceneBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(sceneBinding);

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
    }

    void VulkanComputePipeline::createPipeline(const std::string& shaderPath) {
        // --- 1. シェーダーモジュールの作成 ---
        std::vector<uint32_t> shaderCode;
        if(shaderPath.ends_with(".spv")) {
            shaderCode = readFile(shaderPath);
        } else if(shaderPath.ends_with(".comp")) {
            shaderCode = compileGLSLToSPIRV(shaderPath);
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
        // (DescriptorSetLayout をまとめるもの)
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = m_pushContentsSize;// PushConstants 構造体のサイズ
        
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
        shaderStageInfo.pName = "main"; // エントリーポイント関数名

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = m_pipelineLayout;

        if (vkCreateComputePipelines(m_device.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create compute pipeline!");
        }
    }

    std::vector<uint32_t> VulkanComputePipeline::readFile(const std::string& filename) {
        // バイナリモードで末尾から開く（サイズ取得のため）
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file: " + filename);
        }

        size_t fileSize = (size_t)file.tellg();
        // SPIR-Vバイナリは4バイト単位でい場合はエラー
        if (fileSize % 4 != 0) {
            throw std::runtime_error("SPIR-V file size must be a multiple of 4: " + filename);
        }
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
        file.close();

        return buffer;
    }

    std::vector<uint32_t> VulkanComputePipeline::compileGLSLToSPIRV(const std::string& shaderPath) {
        // 1. ソースコードをテキストとして読み込む
        std::ifstream file(shaderPath);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open shader file: " + shaderPath);
        }
        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // 2. shaderc コンパイラの設定
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        // デバッグのしやすさのために最適化レベルやデバッグ情報を設定可能
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        // 3. コンパイル実行
        shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
            source, 
            shaderc_compute_shader, 
            shaderPath.c_str(), 
            options
        );

        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            std::cerr << "Shader Compilation Error: " << result.GetErrorMessage() << std::endl;
            throw std::runtime_error("failed to compile shader: " + shaderPath);
        }

        return { result.cbegin(), result.cend() };
    }
}