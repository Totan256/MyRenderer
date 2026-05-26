#include "VulkanUploadManager.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "vulkan/VulkanSync.hpp"


namespace rhi::vk {
    VulkanUploadManager::VulkanUploadManager(VulkanDevice& device, uint32_t framesInFlight, size_t ringBufferSize)
        : m_device(device), m_framesInFlight(framesInFlight) {
        
        m_asyncState.ringBufferSize = ringBufferSize;
        m_asyncState.ringBuffer = std::make_unique<VulkanBuffer>(
            m_device, m_device.getAllocator(), ringBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        m_asyncState.ringMappedPtr = static_cast<uint8_t*>(m_asyncState.ringBuffer->map());
        m_asyncState.cmdList = std::make_unique<VulkanCommandList>(m_device, QueueType::Transfer);
        
        m_asyncState.syncSemaphore = m_device.createSemaphore();
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(m_device.getDevice(), &fenceInfo, nullptr, &m_asyncState.syncFence);

        m_asyncState.cmdList->reset();
        m_asyncState.cmdList->begin();

        m_frameStates.resize(m_framesInFlight);
        for (uint32_t i = 0; i < m_framesInFlight; ++i) {
            auto& state = m_frameStates[i];
            state.ringBufferSize = ringBufferSize;
            state.ringBuffer = std::make_unique<VulkanBuffer>(
                m_device, m_device.getAllocator(), ringBufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU
            );
            state.ringMappedPtr = static_cast<uint8_t*>(state.ringBuffer->map());
        }
    }

    VulkanUploadManager::~VulkanUploadManager() {
        ensureAsyncReady();
        m_device.destroySemaphore(m_asyncState.syncSemaphore);
        vkDestroyFence(m_device.getDevice(), m_asyncState.syncFence, nullptr);
    }

    StagingAllocation VulkanUploadManager::allocateStagingSpace(UploadState& state, size_t size, size_t alignment) {
        const size_t THRESHOLD = state.ringBufferSize / 4;
        size_t allocOffset = (state.ringBufferOffset + alignment - 1) & ~(alignment - 1);

        if (size > THRESHOLD || allocOffset + size > state.ringBufferSize) {
            auto tempBuf = std::make_unique<VulkanBuffer>(
                m_device, m_device.getAllocator(), size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU
            );
            void* ptr = tempBuf->map();
            VulkanBuffer* rawPtr = tempBuf.get();
            state.pendingTemporaryBuffers.push_back(std::move(tempBuf));
            return { rawPtr, 0, ptr, true };
        }

        void* ptr = state.ringMappedPtr + allocOffset;
        state.ringBufferOffset = allocOffset + size;
        return { state.ringBuffer.get(), allocOffset, ptr, false };
    }

    void VulkanUploadManager::ensureAsyncReady() {
        if (m_asyncState.isSubmitted) {
            vkWaitForFences(m_device.getDevice(), 1, &m_asyncState.syncFence, VK_TRUE, UINT64_MAX);
            vkResetFences(m_device.getDevice(), 1, &m_asyncState.syncFence);
            m_asyncState.pendingTemporaryBuffers.clear();
            m_asyncState.ringBufferOffset = 0;
            m_asyncState.cmdList->reset();
            m_asyncState.cmdList->begin();
            m_asyncState.isSubmitted = false;
        }
    }

    void VulkanUploadManager::flushAsync(Buffer* dstBuffer, UploadMode mode) {
        m_asyncState.cmdList->end();
        vkResetFences(m_device.getDevice(), 1, &m_asyncState.syncFence);
        
        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkCommandBuffer cmdBuf = m_asyncState.cmdList->getCommandBuffer();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        
        if (mode == UploadMode::Async) {
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &m_asyncState.syncSemaphore;
            // マップに記録し、あとでレンダーグラフに回収させる
            m_pendingAsyncSemaphores[dstBuffer] = m_asyncState.syncSemaphore;
        }

        vkQueueSubmit(m_device.getQueue(QueueType::Transfer), 1, &submitInfo, m_asyncState.syncFence);
        m_asyncState.isSubmitted = true;

        if (mode == UploadMode::Immediate) {
            ensureAsyncReady(); // Immediateならその場でCPU待機
        }
    }

    void VulkanUploadManager::uploadBuffer(Buffer* dstBuffer, const void* data, size_t size, UploadMode mode) {
        if (mode == UploadMode::Deferred) {
            auto& state = m_frameStates[m_currentFrameIndex];
            StagingAllocation alloc = allocateStagingSpace(state, size);
            std::memcpy(alloc.mappedPtr, data, size);
            state.pendingUploads.push_back({alloc.buffer, alloc.offset, dstBuffer, size});
        } else {
            ensureAsyncReady();
            StagingAllocation alloc = allocateStagingSpace(m_asyncState, size);
            std::memcpy(alloc.mappedPtr, data, size);
            m_asyncState.cmdList->copyBuffer(alloc.buffer, dstBuffer, size, alloc.offset, 0);
            flushAsync(dstBuffer, mode);
        }
    }

    void* VulkanUploadManager::mapForDeferredUpload(Buffer* dstBuffer, size_t size) {
        auto& state = m_frameStates[m_currentFrameIndex];
        StagingAllocation alloc = allocateStagingSpace(state, size);
        state.pendingUploads.push_back({alloc.buffer, alloc.offset, dstBuffer, size});
        return alloc.mappedPtr;
    }

    std::vector<SemaphoreHandle> VulkanUploadManager::consumeAsyncSemaphores(const std::vector<Buffer*>& buffers) {
        std::vector<SemaphoreHandle> sems;
        for (auto buf : buffers) {
            auto it = m_pendingAsyncSemaphores.find(buf);
            if (it != m_pendingAsyncSemaphores.end()) {
                sems.push_back(it->second);
                m_pendingAsyncSemaphores.erase(it); // 回収したら消す
            }
        }
        // 重複セマフォの除去
        std::sort(sems.begin(), sems.end());
        sems.erase(std::unique(sems.begin(), sems.end()), sems.end());
        return sems;
    }

    std::vector<SemaphoreHandle> VulkanUploadManager::consumeImageSemaphores(const std::vector<Image*>& images) {
        std::vector<SemaphoreHandle> sems;
        for (auto img : images) {
            auto it = m_pendingImageSemaphores.find(img);
            if (it != m_pendingImageSemaphores.end()) {
                sems.push_back(it->second);
                m_pendingImageSemaphores.erase(it); // 回収したら消す
            }
        }
        // 重複セマフォの除去
        std::sort(sems.begin(), sems.end());
        sems.erase(std::unique(sems.begin(), sems.end()), sems.end());
        return sems;
    }

    void VulkanUploadManager::beginFrame(uint64_t currentFrameIndex) {
        m_currentFrameIndex = currentFrameIndex % m_framesInFlight;
        auto& state = m_frameStates[m_currentFrameIndex];
        state.pendingTemporaryBuffers.clear();
        state.ringBufferOffset = 0;
        state.pendingUploads.clear();
    }

    std::vector<UploadRequest> VulkanUploadManager::getAndClearPendingUploads() {
        auto& state = m_frameStates[m_currentFrameIndex];
        auto res = std::move(state.pendingUploads);
        state.pendingUploads.clear();
        return res;
    }

    std::unique_ptr<rhi::Image> VulkanUploadManager::uploadImageFromFile(
        const std::string& filepath, std::optional<rhi::ImageDesc> overrideDesc, UploadMode mode) {
        
        // 1. 画像ファイルの読み込み (stb_image)
        int texWidth, texHeight, texChannels;
        void* pixels = nullptr;
        rhi::Format autoFormat;
        size_t bytesPerPixel = 0;
        if (!pixels) {
            throw std::runtime_error("Failed to load image: " + filepath);
        }
        if (stbi_is_hdr(filepath.c_str())) {
            pixels = stbi_loadf(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            autoFormat = rhi::Format::R32G32B32A32_Sfloat;
            bytesPerPixel = 4 * sizeof(float);
        } else if (stbi_is_16_bit(filepath.c_str())) {
            pixels = stbi_load_16(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            autoFormat = rhi::Format::R16G16B16A16_Unorm;
            bytesPerPixel = 4 * sizeof(uint16_t);
        } else {
            pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            autoFormat = rhi::Format::R8G8B8A8_Unorm;
            bytesPerPixel = 4 * sizeof(stbi_uc);
        }
        if (!pixels) {
            throw std::runtime_error("Failed to load image: " + filepath);
        }

        VkDeviceSize imageSize = texWidth * texHeight * 4;
        // ミップレベルの計算: floor(log2(max(width, height))) + 1
        uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

        // 2. ImageDescの確定とVulkanImageの作成
        rhi::ImageDesc desc = overrideDesc.value_or(rhi::ImageDesc{
            (uint32_t)texWidth, (uint32_t)texHeight, 1,
            mipLevels, 1,
            autoFormat,
            rhi::ImageUsageFlags::TransferSrc | rhi::ImageUsageFlags::TransferDst | rhi::ImageUsageFlags::Sampled
        });

        // 物理イメージの作成
        auto vkUsage = mapImageUsage(desc.usageFlags);
        auto image = std::make_unique<VulkanImage>(m_device, desc, vkUsage);

        if (mode == rhi::UploadMode::Immediate){
            ensureAsyncReady();
            StagingAllocation alloc = allocateStagingSpace(m_asyncState, imageSize);
            std::memcpy(alloc.mappedPtr, pixels, imageSize);
            stbi_image_free(pixels); // メモリ解放
            VkCommandBuffer cmd = m_asyncState.cmdList->getCommandBuffer();
            // 4. Mip 0 を Copy 可能なレイアウト (TRANSFER_DST_OPTIMAL) へ遷移
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image->getImage();
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(
                cmd, 
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier
            );
            // 5. Buffer to Image Copy (Mip 0 に対するデータ転送)
            VkBufferImageCopy region{};
            region.bufferOffset = alloc.offset;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0; // Mip 0のみ
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = { (uint32_t)texWidth, (uint32_t)texHeight, 1 };

            vkCmdCopyBufferToImage(
                cmd, 
                alloc.buffer->getNativeBuffer(), 
                image->getImage(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                1, &region
            );
            image->recordMipmapGenerationCmds(cmd);
            // 7. コマンドの送信と完了待機
            flushImmediate();// todo セマフォ利用に変更して、RenderGraph側で待機させるようにする
        }else {
            // Deferred モード
            auto& state = m_frameStates[m_currentFrameIndex];
            StagingAllocation alloc = allocateStagingSpace(state, imageSize);
            std::memcpy(alloc.mappedPtr, pixels, imageSize);
            stbi_image_free(pixels);
            // RenderGraphで処理するためにリクエストを積むだけ
            state.pendingImageUploads.push_back({
                alloc.buffer, alloc.offset, image.get(), 
                static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), mipLevels
            });
        }
        // 8. バインドレスの Sampled Image (Binding 3) として登録
        image->registerAsSampledImage();
        return image;
    }
    std::vector<rhi::ImageUploadRequest> VulkanUploadManager::getAndClearPendingImageUploads() {
        auto& state = m_frameStates[m_currentFrameIndex];
        auto res = std::move(state.pendingImageUploads);
        state.pendingImageUploads.clear();
        return res;
    }

    void VulkanUploadManager::flushImmediate() {
        m_asyncState.cmdList->end();
        vkResetFences(m_device.getDevice(), 1, &m_asyncState.syncFence);
        
        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkCommandBuffer cmdBuf = m_asyncState.cmdList->getCommandBuffer();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        
        vkQueueSubmit(m_device.getQueue(QueueType::Transfer), 1, &submitInfo, m_asyncState.syncFence);
        m_asyncState.isSubmitted = true;
        ensureAsyncReady(); // CPU待機
    }
}