#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include "rhi/RHIconfig.hpp" // SwapchainConfig用
#include "vulkan/VulkanImage.hpp"
#include "rhi/Swapchain.hpp" // rhi::Swapchain用

struct GLFWwindow;

namespace rhi::vk {
    struct SwapchainConfig {
        bool enableLowLatency = true;        // true = MAILBOX(Fast V-Sync), false = FIFO(V-Sync ON)
        uint32_t desiredBufferCount = 3;     // 希望するバッファ数 (デフォルトはトリプルバッファリング)
    };

    // 前方宣言
    class VulkanDevice;

    class VulkanSwapchain : public rhi::Swapchain {
    public:
        VulkanSwapchain(VulkanDevice& device, GLFWwindow* window, const SwapchainConfig& config);
        ~VulkanSwapchain();

        // コピー・ムーブ禁止
        VulkanSwapchain(const VulkanSwapchain&) = delete;
        VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

        // リサイズ時に呼ばれる再構築メソッド
        void recreate(uint32_t width, uint32_t height);

        // 次の描画用画像を取得する。再構築が必要な場合(リサイズ時等)は false を返す
        bool acquireNextImage(VkSemaphore acquireSem, VkSemaphore presentSem, uint32_t& imageIndex);

        // 描画結果を画面に表示する。再構築が必要な場合は false を返す
        bool present(VkQueue presentQueue, uint32_t imageIndex);

        // ゲッター群
        std::shared_ptr<rhi::Image> getCurrentImage(uint32_t index) const { return m_images[index]; }
        // rhi::Format getFormat() const { return static_cast<rhi::Format>(m_format); }
        VkExtent2D getExtent() const { return m_extent; }
        uint32_t getImageCount() const { return static_cast<uint32_t>(m_images.size()); }
        uint32_t getWidth() const { return m_extent.width; }
        uint32_t getHeight() const { return m_extent.height; }

        SemaphoreHandle getCurrentAcquireSemaphore() const {
            return reinterpret_cast<SemaphoreHandle>(m_currentAcquireSemaphore);
        }
        SemaphoreHandle getCurrentPresentSemaphore() const {
            return reinterpret_cast<SemaphoreHandle>(m_currentPresentSemaphore);
        }

    private:
        void initSurface();
        void create(uint32_t width, uint32_t height);
        void cleanup();
        
        // スワップチェーンの最適設定を選択するヘルパー
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height);


    private:
        VulkanDevice& m_device;
        GLFWwindow* m_window;
        SwapchainConfig m_config;

        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        VkFormat m_format;
        VkExtent2D m_extent;
        VkSemaphore m_currentAcquireSemaphore = VK_NULL_HANDLE;
        VkSemaphore m_currentPresentSemaphore = VK_NULL_HANDLE;

        std::vector<VkImage> m_vkImages;
        std::vector<std::shared_ptr<rhi::Image>> m_images; // RenderGraph等で扱うためのラッパー
    };

} // namespace rhi