#include "ComputePipeline.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

ComputePipeline::ComputePipeline(GraphicsDevice& device, const std::string& shaderPath)
    : m_device(device) {
    
    // 1. ディスクリプタセットレイアウトの作成（Binding=0にStorageBufferがあることを定義）
    createDescriptorSetLayout();

    // 2. パイプラインの作成（シェーダー読み込み -> パイプラインレイアウト -> パイプライン）
    createPipeline(shaderPath);
}

ComputePipeline::~ComputePipeline() {
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
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    }
}

void ComputePipeline::createDescriptorSetLayout() {
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

    if(vkCreateDescriptorSetLayout(m_device.getDevice(), &layoutInfo,
            nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void ComputePipeline::createPipeline(const std::string& shaderPath) {
    // --- 1. シェーダーモジュールの作成 ---
    auto shaderCode = readFile(shaderPath);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = shaderCode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

    if (vkCreateShaderModule(m_device.getDevice(), &createInfo, nullptr, &m_shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

    // --- 2. パイプラインレイアウトの作成 ---
    // (DescriptorSetLayout をまとめるもの)
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout; // さっき作ったレイアウトをセット

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

std::vector<char> ComputePipeline::readFile(const std::string& filename) {
    // バイナリモードで末尾から開く（サイズ取得のため）
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}