// GraphicsDevice.hpp
#pragma once

// vcpkgで入れたライブラリ
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <vector>
#include <string>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <optional>

// エラーチェック用の簡易マクロ
#define VK_CHECK(call) \
    do { \
        VkResult result = call; \
        if (result != VK_SUCCESS) { \
            std::cerr << "Vulkan Error: " << result << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            throw std::runtime_error("Vulkan operation failed: " + std::to_string(result)); \
        } \
    } while (0)

class GraphicsDevice {
public:
    GraphicsDevice() = default;
    ~GraphicsDevice();

    // コピー禁止
    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    // 初期化（失敗したら例外を投げる）
    void initialize();

    // 生のハンドル取得（拡張性のため）
    VkDevice getDevice() const { return m_device; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VmaAllocator getAllocator() const { return m_allocator; }
    
    // 計算用キューの取得（Compute Shader用）
    VkQueue getComputeQueue() const { return m_computeQueue; }
    uint32_t getComputeQueueFamilyIndex() const { return m_computeQueueFamilyIndex; }

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE; // デバッグ用
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    uint32_t m_computeQueueFamilyIndex = 0;
    
    VmaAllocator m_allocator = VK_NULL_HANDLE;

    // 内部ヘルパー関数
    void createInstance();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createAllocator();

    std::optional<uint32_t> findComputeQueueFamilyIndex(VkPhysicalDevice device);
};