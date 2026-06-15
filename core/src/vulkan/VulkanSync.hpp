#pragma once
#include <vulkan/vulkan.h>
#include "rhi/RHIcommon.hpp"

namespace rhi::vk{
    inline VkCullModeFlags mapCullMode(rhi::CullMode mode) {
        switch (mode) {
            case rhi::CullMode::None: return VK_CULL_MODE_NONE;
            case rhi::CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
            case rhi::CullMode::Back: return VK_CULL_MODE_BACK_BIT;
            case rhi::CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
        }
        return VK_CULL_MODE_NONE;
    }

    inline VkAttachmentLoadOp mapLoadOp(rhi::LoadOp op) {
        switch (op) {
            case rhi::LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
            case rhi::LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case rhi::LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    inline VkAttachmentStoreOp mapStoreOp(rhi::StoreOp op) {
        switch (op) {
            case rhi::StoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
            case rhi::StoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    inline VkPrimitiveTopology mapTopology(rhi::Topology topology) {
        switch(topology) {
            case rhi::Topology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case rhi::Topology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case rhi::Topology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case rhi::Topology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case rhi::Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        }
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }

    inline VkFrontFace mapFrontFace(rhi::FrontFace face) {
        return face == rhi::FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }

    inline VkCompareOp mapCompareOp(rhi::CompareOp op) {
        switch (op) {
            case rhi::CompareOp::Less: return VK_COMPARE_OP_LESS;
            case rhi::CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
            case rhi::CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case rhi::CompareOp::Greater: return VK_COMPARE_OP_GREATER;
            case rhi::CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
            case rhi::CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case rhi::CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
        }
        return VK_COMPARE_OP_LESS;
    }

    inline VkBufferUsageFlags mapBufferUsage(rhi::BufferUsageFlags flags) {
        VkBufferUsageFlags vkFlags = 0;

        if ((flags & rhi::BufferUsageFlags::TransferSrc) != rhi::BufferUsageFlags::None)   
            vkFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if ((flags & rhi::BufferUsageFlags::TransferDst) != rhi::BufferUsageFlags::None)   
            vkFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if ((flags & rhi::BufferUsageFlags::UniformBuffer) != rhi::BufferUsageFlags::None) 
            vkFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if ((flags & rhi::BufferUsageFlags::StorageBuffer) != rhi::BufferUsageFlags::None) 
            vkFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if ((flags & rhi::BufferUsageFlags::VertexBuffer) != rhi::BufferUsageFlags::None)  
            vkFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if ((flags & rhi::BufferUsageFlags::IndexBuffer) != rhi::BufferUsageFlags::None)   
            vkFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        return vkFlags;
    }

    inline VkFormat mapFormat(rhi::Format format) {
        switch (format) {
        case rhi::Format::R8G8B8A8_Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case rhi::Format::R32G32B32A32_Sfloat: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case rhi::Format::R16G16B16A16_Unorm: return VK_FORMAT_R16G16B16A16_UNORM;
        case rhi::Format::D32_Sfloat: return VK_FORMAT_D32_SFLOAT;
        case rhi::Format::D24_Unorm: return VK_FORMAT_X8_D24_UNORM_PACK32;
        case rhi::Format::B8G8R8A8_Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
        case rhi::Format::B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
        default:
            throw std::runtime_error("vkAsync: Unsupported format!");
        }
    }

    inline rhi::Format mapToRHIFormat(VkFormat vkFormat) {
        switch (vkFormat) {
        case VK_FORMAT_R8G8B8A8_UNORM: return rhi::Format::R8G8B8A8_Unorm;
        case VK_FORMAT_B8G8R8A8_UNORM: return rhi::Format::B8G8R8A8_Unorm;
        case VK_FORMAT_B8G8R8A8_SRGB: return rhi::Format::B8G8R8A8_SRGB;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return rhi::Format::R32G32B32A32_Sfloat;
        case VK_FORMAT_R16G16B16A16_UNORM: return rhi::Format::R16G16B16A16_Unorm;
        case VK_FORMAT_D32_SFLOAT: return rhi::Format::D32_Sfloat;
        case VK_FORMAT_X8_D24_UNORM_PACK32: return rhi::Format::D24_Unorm;
        default:
            throw std::runtime_error("vkAsync: Unsupported VkFormat!");
        }
    }

    inline VkImageAspectFlags mapAspectFlags(rhi::Format format) {
        if (format == rhi::Format::D32_Sfloat || format == rhi::Format::D24_Unorm) {
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }

    inline VkImageUsageFlags mapImageUsage(rhi::ImageUsageFlags flags) {
        VkImageUsageFlags vkFlags = 0;
        
        if ((flags & rhi::ImageUsageFlags::TransferSrc) != rhi::ImageUsageFlags::None)   
            vkFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if ((flags & rhi::ImageUsageFlags::TransferDst) != rhi::ImageUsageFlags::None)   
            vkFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if ((flags & rhi::ImageUsageFlags::Sampled) != rhi::ImageUsageFlags::None)       
            vkFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if ((flags & rhi::ImageUsageFlags::Storage) != rhi::ImageUsageFlags::None)       
            vkFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
        if ((flags & rhi::ImageUsageFlags::ColorAttachment) != rhi::ImageUsageFlags::None) 
            vkFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if ((flags & rhi::ImageUsageFlags::DepthStencilAttachment) != rhi::ImageUsageFlags::None) 
            vkFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            
        return vkFlags;
    }
    
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
    inline VulkanResourceState MapResourceState(rhi::ResourceState state, 
            rhi::ShaderStage stage) {

        VulkanResourceState result{};
        result.stageMask = MapToVulkanStage(stage);
        
        switch (state) {
        case rhi::ResourceState::Undefined:
            result.accessMask = VK_ACCESS_2_NONE;
            result.layout     = VK_IMAGE_LAYOUT_UNDEFINED;
            break;

        case rhi::ResourceState::SampledTexture:
            result.accessMask = VK_ACCESS_2_SHADER_READ_BIT;
            result.layout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            break;

        case rhi::ResourceState::StorageRead:
            result.accessMask = VK_ACCESS_2_SHADER_READ_BIT;
            result.layout     = VK_IMAGE_LAYOUT_GENERAL;
            break;

        case rhi::ResourceState::StorageWrite:
            result.accessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            result.layout     = VK_IMAGE_LAYOUT_GENERAL;
            break;

        case rhi::ResourceState::ColorAttachment:
            // ColorAttachmentの場合は強制的に出力ステージを含める
            result.stageMask |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            result.accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            result.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            break;

        case rhi::ResourceState::DepthStencilWrite:
            result.stageMask |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            result.accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            result.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            break;

        case rhi::ResourceState::TransferSrc:
            result.stageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            result.accessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            result.layout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            break;

        case rhi::ResourceState::TransferDst:
            result.stageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            result.accessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            result.layout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            break;

        case rhi::ResourceState::Present:
            result.stageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            result.accessMask = VK_ACCESS_2_NONE;
            result.layout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            break;

        // Buffer専用の状態
        case rhi::ResourceState::ConstantBuffer:
            result.accessMask = VK_ACCESS_2_UNIFORM_READ_BIT;
            result.layout     = VK_IMAGE_LAYOUT_UNDEFINED;
            break;
            
        case rhi::ResourceState::VertexBuffer:
        case rhi::ResourceState::IndexBuffer:
            result.accessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
            result.layout     = VK_IMAGE_LAYOUT_UNDEFINED;
            break;
        }

        return result;
    }
}