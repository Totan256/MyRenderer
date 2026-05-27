#include "VulkanGraphicsPipeline.hpp"
#include "VulkanComputePipeline.hpp" // シェーダーコンパイル用のユーティリティを借用
#include <stdexcept>
#include <iostream>

namespace rhi::vk {

    VulkanGraphicsPipeline::VulkanGraphicsPipeline(
        VulkanDevice& device,
        const std::string& vertShaderPath,
        const std::string& fragShaderPath,
        const std::vector<VkFormat>& colorFormats,
        VkFormat depthFormat,
        uint32_t pushContentsSize)
        : m_device(device), m_pushContentsSize(pushContentsSize) 
    {
        createPipeline(vertShaderPath, fragShaderPath, colorFormats, depthFormat);
    }

    VulkanGraphicsPipeline::~VulkanGraphicsPipeline() {
        VkDevice device = m_device.getDevice();
        if (m_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, m_pipeline, nullptr);
        if (m_pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        if (m_vertShaderModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, m_vertShaderModule, nullptr);
        if (m_fragShaderModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, m_fragShaderModule, nullptr);
    }

    void VulkanGraphicsPipeline::createPipeline(const std::string& vertPath, const std::string& fragPath, const std::vector<VkFormat>& colorFormats, VkFormat depthFormat) {
        VkDevice device = m_device.getDevice();

        // 1. シェーダーモジュールの作成 (ここではCompute用関数を流用しますが、Shadercの引数変更が必要になる場合があります)
        auto vertCode = VulkanComputePipeline::compileGLSLToSPIRV(vertPath); // TODO: Vertex用コンパイルに対応
        auto fragCode = VulkanComputePipeline::compileGLSLToSPIRV(fragPath); // TODO: Fragment用コンパイルに対応
        
        VkShaderModuleCreateInfo vertInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        vertInfo.codeSize = vertCode.size() * sizeof(uint32_t);
        vertInfo.pCode = vertCode.data();
        VK_CHECK(vkCreateShaderModule(device, &vertInfo, nullptr, &m_vertShaderModule));

        VkShaderModuleCreateInfo fragInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        fragInfo.codeSize = fragCode.size() * sizeof(uint32_t);
        fragInfo.pCode = fragCode.data();
        VK_CHECK(vkCreateShaderModule(device, &fragInfo, nullptr, &m_fragShaderModule));

        VkPipelineShaderStageCreateInfo shaderStages[2] = {};
        shaderStages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, m_vertShaderModule, "main", nullptr };
        shaderStages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, m_fragShaderModule, "main", nullptr };

        // 2. Programmable Vertex Pulling のため、頂点入力は「完全に空」にする
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // 3. Dynamic State の設定 (PSO爆発を防ぐため、変動しうるものは全て動的にする)
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_CULL_MODE,             // EDS
            VK_DYNAMIC_STATE_FRONT_FACE,            // EDS
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,    // EDS
            VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,     // EDS
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,    // EDS
            VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,      // EDS
            // 必要に応じてブレンドなども EDS3 で動的化可能
        };
        VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        // 初期値(動的ステートで上書きされるため適当でよい)
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        // 初期値(動的ステートで上書きされる)
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        // カラーブレンド (一旦シンプルな上書き設定)
        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorFormats.size());
        for (auto& attach : colorBlendAttachments) {
            attach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            attach.blendEnable = VK_FALSE;
        }
        VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        colorBlending.pAttachments = colorBlendAttachments.data();

        // 4. パイプラインレイアウト (バインドレス)
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = m_pushContentsSize;

        VkDescriptorSetLayout layouts[] = { m_device.getBindlessLayout() };
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = layouts;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout));

        // 5. Dynamic Rendering 用の連携情報 (RenderPassは使わない)
        VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
        renderingInfo.pColorAttachmentFormats = colorFormats.data();
        renderingInfo.depthAttachmentFormat = depthFormat;

        // 6. Graphics Pipeline の生成
        VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipelineInfo.pNext = &renderingInfo; // Dynamic Rendering を指定
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE; // RenderPassは不要
        pipelineInfo.subpass = 0;

        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));
    }
}