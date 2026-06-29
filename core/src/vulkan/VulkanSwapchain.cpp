#include "VulkanSwapchain.hpp"
#include "VulkanDevice.hpp"
#include "VulkanImage.hpp"
#include <stdexcept>
#include <algorithm>
#include <iostream>

namespace rhi::vk {

    VulkanSwapchain::VulkanSwapchain(VulkanDevice& device, VkSurfaceKHR surface, const SwapchainConfig& config, const core::Window& window)
        : m_device(device), m_surface(surface), m_config(config), m_window(window)
    {
        
        // 初回作成時のサイズ取得
        create(static_cast<uint32_t>(m_window.getWidth()), static_cast<uint32_t>(m_window.getHeight()));
        m_presentQueue = m_device.getQueue(rhi::QueueType::Graphics); // todo Present用のキュー（通常はGraphicsと同じ）
    }

    VulkanSwapchain::~VulkanSwapchain() {
        vkDeviceWaitIdle(m_device.getDevice());
        cleanup();
        for (auto sem : m_acquireSemaphores) {
            if (sem != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_device.getDevice(), sem, nullptr);
            }
        }
        for (auto sem : m_presentSemaphores) {
            if (sem != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_device.getDevice(), sem, nullptr);
            }
        }
        m_acquireSemaphores.clear();
        m_presentSemaphores.clear();
        if (m_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_device.getInstance(), m_surface, nullptr);
        }
    }

    void VulkanSwapchain::cleanup() {
        // ラッパー画像の解放 (内部のVkImageはSwapchainが破棄するため解放しないよう注意)
        m_images.clear();

        if (m_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_device.getDevice(), m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
    }

    void VulkanSwapchain::recreate(uint32_t width, uint32_t height) {
        // 最小化時はサイズが0になるため、復帰するまで待機する
        int w = 0, h = 0;
        // glfwGetFramebufferSize(m_window, &w, &h);
        // while (w == 0 || h == 0) {
        //     glfwGetFramebufferSize(m_window, &w, &h);
        //     glfwWaitEvents();
        // }

        vkDeviceWaitIdle(m_device.getDevice());

        // 既存のスワップチェーンを oldSwapchain として再利用する
        create(width, height);
    }

    void VulkanSwapchain::create(uint32_t width, uint32_t height) {
        VkPhysicalDevice physicalDevice = m_device.getPhysicalDevice();
        VkDevice nativeDevice = m_device.getDevice();

        // 1. サーフェスがサポートする機能をクエリ
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_surface, &capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        if (formatCount != 0) {
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount, formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        if (presentModeCount != 0) {
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_surface, &presentModeCount, presentModes.data());
        }

        // 2. 最適な設定の選択
        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(presentModes);
        VkExtent2D extent = chooseSwapExtent(capabilities, width, height);

        // 3. バッファ数の決定
        uint32_t imageCount = std::max(capabilities.minImageCount, m_config.desiredBufferCount);
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }

        // 4. SwapchainCreateInfoの構築
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // UI描画や画面クリア用

        // ※キューファミリの扱いは、現在単一のキュー(Graphics兼Present)を想定しています
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

        // Compute Shader (Storage Image) として利用できるように STORAGE_BIT を追加
        if (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_STORAGE_BIT) {
            createInfo.imageUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
        } else {
            // モバイル環境など、サーフェスへの直接のStorage書き込みがサポートされていない場合への警告
            std::cerr << "Warning: Swapchain does not support VK_IMAGE_USAGE_STORAGE_BIT. Direct Compute Shader write may fail." << std::endl;
        }

        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        // リサイズ時の最適化 (古いスワップチェーンを渡す)
        VkSwapchainKHR oldSwapchain = m_swapchain;
        createInfo.oldSwapchain = oldSwapchain;

        if (vkCreateSwapchainKHR(nativeDevice, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swap chain!");
        }

        // 古いスワップチェーンは不要になったのでここで破棄
        if (oldSwapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(nativeDevice, oldSwapchain, nullptr);
        }

        // 5. 画像ハンドルの取得とラッパーの作成
        vkGetSwapchainImagesKHR(nativeDevice, m_swapchain, &imageCount, nullptr);
        m_vkImages.resize(imageCount);
        vkGetSwapchainImagesKHR(nativeDevice, m_swapchain, &imageCount, m_vkImages.data());

        m_format = surfaceFormat.format;
        m_extent = extent;
        m_images.clear();

        for (size_t i = 0; i < m_vkImages.size(); i++) {
            // ※ VulkanImageクラスに、既存のVkImageをラップするコンストラクタが
            // 必要になる場合があります (所有権を持たず、破棄時に vkDestroyImage を呼ばない設定)
            auto img = std::make_unique<VulkanImage>(
                m_device, m_vkImages[i], m_format, 
                VkExtent3D{m_extent.width, m_extent.height, 1},
                this
            );
            m_images.push_back(std::move(img));
        }

        // 初回作成時のみセマフォを確保
        if (m_acquireSemaphores.empty()) {
            m_acquireSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
            m_presentSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
            VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
                vkCreateSemaphore(m_device.getDevice(), &semInfo, nullptr, &m_acquireSemaphores[i]);
                vkCreateSemaphore(m_device.getDevice(), &semInfo, nullptr, &m_presentSemaphores[i]);
            }
        }
    }

    // --- ヘルパー関数群 ---
    VkSurfaceFormatKHR VulkanSwapchain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        for (const auto& availableFormat : availableFormats) {
            // SRGBが利用可能なら優先
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        return availableFormats[0];
    }

    VkPresentModeKHR VulkanSwapchain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        if (m_config.enableLowLatency) {
            for (const auto& availablePresentMode : availablePresentModes) {
                if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                    return availablePresentMode;
                }
            }
        }
        // MAILBOXが要求されていない、または非対応の場合は安全なFIFO(V-Sync)を使用
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VulkanSwapchain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        } else {
            VkExtent2D actualExtent = { width, height };
            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            return actualExtent;
        }
    }

    // --- メインループから呼ばれる関数 ---
    bool VulkanSwapchain::acquireNextImage() {
        uint32_t frameIdx = m_device.getCurrentFrame() % MAX_FRAMES_IN_FLIGHT;
        
        m_currentAcquireSemaphore = m_acquireSemaphores[frameIdx];
        m_currentPresentSemaphore = m_presentSemaphores[frameIdx];

        VkResult result = vkAcquireNextImageKHR(
            m_device.getDevice(), m_swapchain, 
            std::numeric_limits<uint64_t>::max(), 
            m_currentAcquireSemaphore, VK_NULL_HANDLE, &m_currentImageIndex
        );

        if (result == VK_ERROR_OUT_OF_DATE_KHR) return false;
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) throw std::runtime_error("Failed to acquire!");
        return true;
    }

    bool VulkanSwapchain::present() {
        VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        
        // acquireNextImageで設定した現在のセマフォを待機
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_currentPresentSemaphore;

        VkSwapchainKHR swapchains[] = {m_swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &m_currentImageIndex;
        VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) return false;
        else if (result != VK_SUCCESS) throw std::runtime_error("Failed to present!");
        
        return true;
    }

} // namespace rhi