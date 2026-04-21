// VulkanDevice.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <mutex>
#include <vector>
#include <string>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <optional>
namespace rhi::vk{
    class ConstantBufferManager; // 前方宣言

    // エラーチェック用の簡易マクロ
    #define VK_CHECK(call) \
        do { \
            VkResult result = call; \
            if (result != VK_SUCCESS) { \
                std::cerr << "Vulkan Error: " << result << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
                throw std::runtime_error("Vulkan operation failed: " + std::to_string(result)); \
            } \
        } while (0)

    class VulkanDevice {
    public:
        VulkanDevice() = default;
        ~VulkanDevice();

        // コピー禁止
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        // 初期化（失敗したら例外を投げる）
        void initialize();

        // 生のハンドル取得（拡張性のため）
        VkDevice getDevice() const { return m_device; }
        VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
        VmaAllocator getAllocator() const { return m_allocator; }
        
        // 計算用キューの取得（Compute Shader用）
        VkQueue getComputeQueue() const { return m_computeQueue; }
        uint32_t getComputeQueueFamilyIndex() const { return m_computeQueueFamilyIndex; }

        // Bindless用のセットとレイアウトを取得
        VkDescriptorSetLayout getBindlessLayout() const { return m_bindlessLayout; }
        VkDescriptorSet getBindlessDescriptorSet() const { return m_bindlessDescriptorSet; }

        // UBO用
        uint32_t getMinUniformBufferOffsetAlignment() const { return m_minUniformBufferOffsetAlignment; }
        ConstantBufferManager& getConstantBufferManager() { return *m_constantBufferManager; }

        // 空いているインデックスを割り当ててディスクリプタ更新
        uint32_t registerBuffer(VkBuffer buffer, VkDeviceSize size);
        uint32_t registerImage(VkImageView view);
        uint32_t registerUniformBuffer(VkBuffer buffer, VkDeviceSize size);
        void unregisterIndex(uint32_t index);

    private:
        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE; // デバッグ用
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_computeQueue = VK_NULL_HANDLE;
        uint32_t m_computeQueueFamilyIndex = 0;
        VmaAllocator m_allocator = VK_NULL_HANDLE;
        uint32_t m_minUniformBufferOffsetAlignment = 256;
        std::unique_ptr<ConstantBufferManager> m_constantBufferManager;
        VkDescriptorPool m_bindlessPool= VK_NULL_HANDLE;
        VkDescriptorSetLayout m_bindlessLayout= VK_NULL_HANDLE;
        VkDescriptorSet m_bindlessDescriptorSet= VK_NULL_HANDLE;
        
        // インデックス管理
        std::vector<uint32_t> m_freeIndices; // 再利用可能なインデックス
        uint32_t m_nextIndex = 0;           // 新規発行用カウンタ
        std::mutex m_indexMutex;            // スレッド安全のため

        const uint32_t MAX_BINDLESS_RESOURCES = 100000;
        
        void createBindlessResources(); // 初期化時に呼ぶ

        void createInstance();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createAllocator();
        uint32_t allocateIndex();

        std::optional<uint32_t> findComputeQueueFamilyIndex(VkPhysicalDevice device);
    };
}