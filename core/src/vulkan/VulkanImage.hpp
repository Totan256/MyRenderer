#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "VulkanDevice.hpp"
#include "rhi/RHIcommon.hpp"
#include "rhi/RHIForward.hpp"
#include "rhi/Swapchain.hpp"

namespace rhi::vk{
    class VulkanImage : public rhi::Image {
    public:
        VulkanImage(VulkanDevice& device, const ImageDesc& desc, VkImageUsageFlags usage);
        VulkanImage(VulkanDevice& device, VkImage existingImage, VkFormat format, VkExtent3D extent, rhi::Swapchain* swapchain = nullptr);
        ~VulkanImage() override;
        // コピー禁止
        VulkanImage(const VulkanImage&) = delete;
        VulkanImage& operator=(const VulkanImage&) = delete;

        VkImage getImage() const {return m_image;}
        VkImageView getView() const {return m_view;}
        uint32_t getBindlessIndex() const override { return m_bindlessIndex; }
        uint32_t getBindlessSampledIndex() const { return m_bindlessSampledIndex; }

        rhi::ResourceState getCurrentState() const override { return m_state; }
        rhi::ShaderStage   getCurrentStage() const override { return m_stage; }
        
        void setState(rhi::ResourceState state, rhi::ShaderStage stage) override {
            m_state = state;
            m_stage = stage;
        }

        bool isSwapchainImage() const override { return m_swapchain != nullptr; }
        rhi::Swapchain* getSwapchain() const override { return m_swapchain; }

        bool isImage() const override { return true; }
       
        void recordMipmapGenerationCmds(VkCommandBuffer cmd);
        void registerAsSampledImage();
        VkImageView getMipView(uint32_t mipLevel);
        
    private:
        VulkanDevice& m_device;
        VkImage m_image = VK_NULL_HANDLE;
        VkImageView m_view = VK_NULL_HANDLE;
        VmaAllocation m_allocation = VK_NULL_HANDLE;
        rhi::Swapchain* m_swapchain = nullptr;

        
        rhi::ResourceState m_state = rhi::ResourceState::Undefined;
        rhi::ShaderStage m_stage = rhi::ShaderStage::None;

        uint32_t m_bindlessIndex = 0;
        uint32_t m_bindlessSampledIndex = 0;
        std::map<uint32_t, VkImageView> m_mipViews;
        bool m_isOwned = true;
    };

}