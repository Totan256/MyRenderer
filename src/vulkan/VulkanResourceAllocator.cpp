#include "VulkanResourceAllocator.hpp"
#include "VulkanImage.hpp"
#include "VulkanSync.hpp"
#include "VulkanBuffer.hpp"
#include <algorithm>

namespace rhi::vk{
    void VulkanResourceAllocator::allocate(
        const std::vector<ResourceRegistration>& registry,
        const std::vector<ResourceLifetime>& lifetimes) 
    {
        m_imageMap.clear();
        m_bufferMap.clear();

        // 1. 生存期間の開始順（firstPass）にハンドルをソートして処理する
        std::vector<ResourceHandle> sortedHandles;
        for (ResourceHandle i = 0; i < (ResourceHandle)registry.size(); ++i){
            if (lifetimes[i].firstPass == 0xFFFFFFFF) continue;
            sortedHandles.push_back(i);
        }

        std::sort(sortedHandles.begin(), sortedHandles.end(), [&](ResourceHandle a, ResourceHandle b) {
            return lifetimes[a].firstPass < lifetimes[b].firstPass;
        });

        // 2. 各ハンドルの割り当て
        for (ResourceHandle h : sortedHandles) {
            const auto& reg = registry[h];
            const auto& life = lifetimes[h];

            if (reg.isImported) {
                if (reg.physicalResource->isImage()) {
                    m_imageMap[h] = static_cast<VulkanImage*>(reg.physicalResource);
                } else {
                    m_bufferMap[h] = static_cast<VulkanBuffer*>(reg.physicalResource);
                }
                continue;
            }
            
            if (reg.isImage()) {
                VulkanImage* assignedImage = nullptr;
                // プールから再利用可能なものを探す
                auto* imgDesc = std::get_if<ImageDesc>(&reg.desc);
                for (auto& entry : m_imagePool) {
                    // 条件1: 仕様（サイズ、フォーマット）が一致
                    // 条件2: 前の使用者の終了時間 < 自分の開始時間
                    if (entry.image->getDesc().isCompatible(*imgDesc) && 
                        entry.lastUsedPass < life.firstPass) 
                    {
                        assignedImage = entry.image.get();
                        entry.lastUsedPass = life.lastPass; // 使用中（予約）状態に更新
                        break;
                    }
                }
                if (!assignedImage) {// 見つからなければ新規作成
                    VkImageUsageFlags vkUsage = mapImageUsage(imgDesc->usageFlags);
                    auto newImg = std::make_unique<VulkanImage>(m_device, *imgDesc, vkUsage);
                    assignedImage = newImg.get();
                    m_imagePool.push_back({ std::move(newImg), life.lastPass });
                }
                m_imageMap[h] = assignedImage;
            }else {
                VulkanBuffer* assignedBuffer = nullptr;
                auto* bufferDesc = std::get_if<BufferDesc>(&reg.desc);
                for (auto& entry : m_bufferPool) {
                    if (entry.buffer->getDesc().isCompatible(*bufferDesc) && 
                        entry.lastUsedPass < life.firstPass) 
                    {
                        assignedBuffer = entry.buffer.get();
                        entry.lastUsedPass = life.lastPass;
                        break;
                    }
                }

                if (!assignedBuffer) {
                    VkBufferUsageFlags vkUsage = mapBufferUsage(bufferDesc->usageFlags);
                    auto newBuf = std::make_unique<VulkanBuffer>(
                        m_device, 
                        m_device.getAllocator(), 
                        bufferDesc->size,
                        vkUsage,                  // 変換したフラグを渡す
                        bufferDesc->isCpuVisible ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                    );
                    assignedBuffer = newBuf.get();
                    m_bufferPool.push_back({ std::move(newBuf), life.lastPass });
                }
                m_bufferMap[h] = assignedBuffer;
            }
        }
    }
}