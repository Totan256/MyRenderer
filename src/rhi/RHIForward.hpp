// src/rhi/RHIForward.hpp
#pragma once
#include "RHIconfig.hpp"

namespace rhi {
#if defined(RHI_USE_VULKAN)
    class Resource;
    class Buffer;
    class Image;
    class Device;
    
    // Vulkan実装側の前方宣言
    namespace vk {
        class VulkanDevice;
        class VulkanBuffer;
        class VulkanImage;
        class VulkanCommandList;
        class VulkanComputePipeline;
        class VulkanRenderGraph;
    }

    // using Device = vk::VulkanDevice;
    // using Buffer = vk::VulkanBuffer;
    // using Image = vk::VulkanImage;
    // using CommandList = vk::VulkanCommandList;
    using ComputePipeline = vk::VulkanComputePipeline;
    // using RenderGraph = vk::VulkanRenderGraph;
#elif defined(RHI_USE_DX12)
    // DX12用の前方宣言とalias
#endif
}