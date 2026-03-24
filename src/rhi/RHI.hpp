#pragma once
#include "RHIconfig.hpp"
#if defined(RHI_USE_VULKAN)
    #include "vulkan/VulkanDevice.hpp"
    #include "vulkan/VulkanBuffer.hpp"
    #include "vulkan/VulkanCommandList.hpp"
    #include "vulkan/VulkanComputePipeline.hpp"
    #include "vulkan/VulkanDescriptorManager.hpp"
    #include "vulkan/VulkanImage.hpp"
    #include "vulkan/VulkanRenderGraph.hpp"
    
    namespace rhi {
        using Device = VulkanDevice;
        using Buffer = VulkanBuffer;
        using CommandList = VulkanCommandList;
        using Image = VulkanImage;
        using ComputePipeline = VulkanComputePipeline;
        using RenderGraph = VulkanRenderGraph;
    }
#elif defined(RHI_USE_DX12)
    #include "dx12/DX12Device.hpp"
    #include "dx12/DX12Buffer.hpp"
    namespace rhi {
        using Device = DX12Device;
        using Buffer = DX12Buffer;
        using CommandList = DX12CommandList;
    }
#endif