
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "VulkanDevice.hpp"
// #include "VulkanCommandList.hpp"
#include "VulkanImage.hpp"
#include "VulkanSync.hpp"
namespace rhi::vk{
    VulkanImage::VulkanImage(VulkanDevice& device, const ImageDesc& desc, VkImageUsageFlags usage)
        : m_device(device), m_desc(desc) {
        
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_desc.width;
        imageInfo.extent.height = m_desc.height;
        imageInfo.extent.depth = m_desc.depth;
        imageInfo.mipLevels = m_desc.mipLevels;
        imageInfo.arrayLayers = m_desc.arrayLayers;
        imageInfo.format = mapFormat(m_desc.format); // RGBA 8bit
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;  // GPU最適化配置
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        
        // USAGE: Storage(書き込み), TransferSrc(コピー元), Sampled(テクスチャとして読む)
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        // VMA割り当て
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE; // VRAMに配置
        if(vmaCreateImage(device.getAllocator(), &imageInfo, &allocInfo,
            &m_image, &m_allocation, nullptr) != VK_SUCCESS){
            throw std::runtime_error("failed to create image");
        }

        // image view　shaderから見た設定
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = mapFormat(m_desc.format);
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if(vkCreateImageView(device.getDevice(), &viewInfo,
            nullptr, &m_view) != VK_SUCCESS){
            throw std::runtime_error("failed to create image view");
        }

        m_bindlessIndex = device.registerImage(m_view);
    }

    VulkanImage::~VulkanImage(){
        VkImage image = m_image;
        VkImageView view = m_view;
        VmaAllocation alloc = m_allocation;
        uint32_t bindlessIdx = m_bindlessIndex;
        uint32_t bindlessSampledIdx = m_bindlessSampledIndex;
        
        std::vector<VkImageView> mipViewsToDestroy;
        for (auto const& [mip, mipView] : m_mipViews) {
            if (mipView != VK_NULL_HANDLE) mipViewsToDestroy.push_back(mipView);
        }
        // デバイスの参照をキャプチャし、遅延破棄を登録
        m_device.enqueueDeletion([&device = m_device, image, view, alloc, bindlessIdx, bindlessSampledIdx, mipViewsToDestroy]() {
            VkDevice logicalDevice = device.getDevice();

            for (VkImageView mipView : mipViewsToDestroy) {
                vkDestroyImageView(logicalDevice, mipView, nullptr);
            }
            if (view != VK_NULL_HANDLE) {
                device.unregisterIndex(bindlessIdx);
                vkDestroyImageView(device.getDevice(), view, nullptr);
            }
            if (image != VK_NULL_HANDLE) {
                vmaDestroyImage(device.getAllocator(), image, alloc);
            }
        });
    }

    void VulkanImage::registerAsSampledImage() {
        if (m_bindlessSampledIndex == 0) {
            m_bindlessSampledIndex = m_device.registerSampledImage(m_view);
        }
    }

    void VulkanImage::recordMipmapGenerationCmds(VkCommandBuffer cmd) {
        if (m_desc.mipLevels <= 1) {
            // ミップマップ生成が不要な場合でも、最終的なテクスチャ読み込みレイアウトへ遷移させる
            VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.image = m_image;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
                0, 0, nullptr, 0, nullptr, 1, &barrier);
                
            setState(rhi::ResourceState::SampledTexture, rhi::ShaderStage::All);
            return;
        }
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = m_image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;

        int32_t mipWidth = m_desc.width;
        int32_t mipHeight = m_desc.height;

        for (uint32_t i = 1; i < m_desc.mipLevels; i++) {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr, 0, nullptr, 1, &barrier);

            VkImageBlit blit{};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;

            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(cmd,
                m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        barrier.subresourceRange.baseMipLevel = m_desc.mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);
        setState(rhi::ResourceState::SampledTexture, rhi::ShaderStage::All);
    }

    VkImageView VulkanImage::getMipView(uint32_t mipLevel) {
        if (mipLevel >= m_desc.mipLevels) {
            throw std::runtime_error("Requested mip level out of bounds!");
        }
        if (m_mipViews.count(mipLevel)) {
            return m_mipViews[mipLevel];
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = mapFormat(m_desc.format);
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = mipLevel;
        viewInfo.subresourceRange.levelCount = 1;      // 1レベルだけを対象にする
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView mipView;
        if (vkCreateImageView(m_device.getDevice(), &viewInfo, nullptr, &mipView) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create mip level image view!");
        }
        m_mipViews[mipLevel] = mipView;
        
        return mipView;
    }
}