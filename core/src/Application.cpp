#include "core/Application.hpp"
#include "core/Window.hpp"
#include "rhi/Device.hpp"
#include "rhi/Swapchain.hpp"
#include "rhi/RHI.hpp"
#include "rhi/RHIcommon.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <vector>

namespace core {

    // Application.hpp の m_impl の実体定義
    struct Application::Impl {
        std::unique_ptr<core::Window> window;
        std::unique_ptr<rhi::Swapchain> swapchain;
                
        uint32_t width = 1280;
        uint32_t height = 720;
        uint32_t currentImageIndex = 0;
        bool resizeRequested = false;
    };

    Application::Application(const Config& config) {
        m_impl = std::make_unique<Impl>();
        m_impl->width = config.width;
        m_impl->height = config.height;
        m_mode = config.mode;
        
        // 1. ウィンドウの作成
        m_impl->window = std::make_unique<core::Window>(config.title, config.width, config.height);
        
        // リサイズイベントのコールバックを設定
        m_impl->window->setEventCallback([this](Event& e) {
            if (e.type == EventType::WindowResize) {
                auto& resizeEvent = static_cast<WindowResizeEvent&>(e);
                m_impl->width = resizeEvent.width;
                m_impl->height = resizeEvent.height;
                m_impl->resizeRequested = true;
                this->onResize(resizeEvent.width, resizeEvent.height);
            }
        });
        
        // 2. RHIデバイスの生成と初期化
        m_device = rhi::createDevice(rhi::GraphicsAPI::Vulkan);
        m_device->initialize(m_impl->window->getVulkanProvider());
        
        // 3. スワップチェーンの生成
        rhi::SwapchainConfig swapchainConfig{};
        swapchainConfig.enableLowLatency = true; // MAILBOX (Fast V-Sync)
        swapchainConfig.desiredBufferCount = 3;  // トリプルバッファリング
        
        m_impl->swapchain = m_device->createSwapchain(*m_impl->window, swapchainConfig);
        
        m_lastTimestamp = glfwGetTime();
    }

    Application::~Application() {
        m_impl->swapchain.reset();
        m_device.reset();
        m_impl->window.reset();
        m_impl.reset();
    }

    bool Application::isRunning() const {
        return !m_impl->window->shouldClose();
    }

    void Application::processEvents() {
        m_impl->window->pollEvents();
        
        // デルタタイム（フレーム間経過時間）の計算
        double currentTimestamp = glfwGetTime();
        m_deltaTime = static_cast<float>(currentTimestamp - m_lastTimestamp);
        m_lastTimestamp = currentTimestamp;
    }

    uint32_t Application::getWidth() const {
        return m_impl->window->getWidth();
    }

    uint32_t Application::getHeight() const {
        return m_impl->window->getHeight();
    }

    void Application::requestRedraw() {
        m_needsRedraw = true;
    }

    std::optional<FrameInfo> Application::beginFrame() {
        m_impl->window->pollEvents();
        
        // 最小化時は描画をスキップ
        if (m_impl->window->getWidth() == 0 || m_impl->window->getHeight() == 0) {
            return std::nullopt;
        }
        m_device->beginFrame();

        uint32_t imageIndex;
        
        if (!m_impl->swapchain->acquireNextImage(imageIndex)) {
            m_impl->swapchain->recreate(m_impl->window->getWidth(), m_impl->window->getHeight());
            return std::nullopt; 
        }

        // ここで前フレームのGPU実行完了をフェンスで待機 (CPUがGPUを追い越さないようにする)

        return FrameInfo{m_device->getCurrentFrame(), imageIndex};
    }

    void Application::endFrame(uint32_t imageIndex) {
        // フレームカウンタを進める等の処理
        m_device->endFrame();

        // 画面に表示 (内部でPresentSemaphoreを待つ)
        if (!m_impl->swapchain->present(imageIndex)) {
            m_impl->swapchain->recreate(m_impl->window->getWidth(), m_impl->window->getHeight());
        }
    }

    rhi::Image* Application::getBackImage(uint32_t imageIndex) {
        // RenderGraphへインポートするために、指定されたインデックスのRawポインタを返す
        return m_impl->swapchain->getCurrentImage(imageIndex).get();
    }

} // namespace core