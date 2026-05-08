#pragma once
#include <vulkan/vulkan.h>
#include "rhi/RHIcommon.hpp"

namespace rhi::vk{
    
    struct VulkanResourceState {
        VkPipelineStageFlags2 stageMask;
        VkAccessFlags2        accessMask;
        VkImageLayout         layout;
    };

    // RHIのShaderStageをVulkanのPipelineStageFlags2に変換する
    inline VkPipelineStageFlags2 MapToVulkanStage(rhi::ShaderStage stage) {
        if (stage == rhi::ShaderStage::All) return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        
        VkPipelineStageFlags2 flags = 0;
        if (uint64_t(stage) & uint64_t(rhi::ShaderStage::Top))          flags |= VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        if (uint64_t(stage) & uint64_t(rhi::ShaderStage::DrawIndirect)) flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        if (uint64_t(stage) & uint64_t(rhi::ShaderStage::Vertex))       flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        if (uint64_t(stage) & uint64_t(rhi::ShaderStage::Fragment))     flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        if (uint64_t(stage) & uint64_t(rhi::ShaderStage::Compute))      flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        if (uint64_t(stage) & uint64_t(rhi::ShaderStage::Transfer))     flags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        
        return flags != 0 ? flags : VK_PIPELINE_STAGE_2_NONE;
    }

    // セマンティックからVulkanの内部フラグへ変換する
    inline VulkanResourceState MapResourceState(rhi::ResourceUsage usage, 
            rhi::ShaderStage stage) {

        VulkanResourceState result{};
        result.stageMask = MapToVulkanStage(stage);
        
        switch (usage) {
        case rhi::ResourceUsage::Undefined:
            result.accessMask = VK_ACCESS_2_NONE;
            result.layout     = VK_IMAGE_LAYOUT_UNDEFINED;
            break;

        case rhi::ResourceUsage::SampledTexture:
            result.accessMask = VK_ACCESS_2_SHADER_READ_BIT;
            result.layout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            break;

        case rhi::ResourceUsage::StorageRead:
            result.accessMask = VK_ACCESS_2_SHADER_READ_BIT;
            result.layout     = VK_IMAGE_LAYOUT_GENERAL;
            break;

        case rhi::ResourceUsage::StorageWrite:
            result.accessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            result.layout     = VK_IMAGE_LAYOUT_GENERAL;
            break;

        case rhi::ResourceUsage::ColorAttachment:
            // ColorAttachmentの場合は強制的に出力ステージを含める
            result.stageMask |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            result.accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            result.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            break;

        case rhi::ResourceUsage::DepthStencilWrite:
            result.stageMask |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            result.accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            result.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            break;

        case rhi::ResourceUsage::TransferSrc:
            result.stageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            result.accessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            result.layout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            break;

        case rhi::ResourceUsage::TransferDst:
            result.stageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            result.accessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            result.layout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            break;

        case rhi::ResourceUsage::Present:
            result.stageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            result.accessMask = VK_ACCESS_2_NONE;
            result.layout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            break;

        // Buffer専用の状態
        case rhi::ResourceUsage::ConstantBuffer:
            result.accessMask = VK_ACCESS_2_UNIFORM_READ_BIT;
            result.layout     = VK_IMAGE_LAYOUT_UNDEFINED;
            break;
            
        case rhi::ResourceUsage::VertexBuffer:
        case rhi::ResourceUsage::IndexBuffer:
            result.accessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
            result.layout     = VK_IMAGE_LAYOUT_UNDEFINED;
            break;
        }

        return result;
    }
}