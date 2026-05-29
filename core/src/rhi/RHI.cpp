// src/rhi/RHI.cpp
#include "RHI.hpp"

#ifdef RHI_USE_VULKAN
#include "vulkan/VulkanDevice.hpp"
#endif

namespace rhi {
    std::unique_ptr<Device> createDevice(GraphicsAPI api) {
        switch (api) {
            case GraphicsAPI::Vulkan: {
#ifdef RHI_USE_VULKAN
                auto device = std::make_unique<vk::VulkanDevice>();
                device->initialize(); 
                return device;
#else
                throw std::runtime_error("Vulkan is not enabled!");
#endif
            }
            case GraphicsAPI::DX12:{
                // 将来 DX12Device を返す
                throw std::runtime_error("DX12 is not supported yet!");
            default:
                throw std::runtime_error("Unknown Graphics API!");
            }
        }
    }
}