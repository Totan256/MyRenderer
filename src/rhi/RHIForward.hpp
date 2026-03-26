// src/rhi/RHIForward.hpp
#pragma once
#include "RHIconfig.hpp"

namespace rhi {
#if defined(RHI_USE_VULKAN)
    class VulkanDevice;
    class VulkanBuffer;
    class VulkanImage;
    class VulkanCommandList;
    class VulkanComputePipeline;
    class VulkanRenderGraph;

    using Device = VulkanDevice;
    using Buffer = VulkanBuffer;
    using Image = VulkanImage;
    using CommandList = VulkanCommandList;
    using ComputePipeline = VulkanComputePipeline;
    using RenderGraph = VulkanRenderGraph;
#elif defined(RHI_USE_DX12)
    // DX12用の前方宣言とalias
#endif
}