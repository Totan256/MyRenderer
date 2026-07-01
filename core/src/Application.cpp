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
        bool resizeRequested = false;
        bool isMinimanized = false;
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
        swapchainConfig.enableComputePresent = true; //computeから直接書き込み
        
        m_impl->swapchain = m_device->createSwapchain(*m_impl->window, swapchainConfig);
        
        m_lastTimestamp = glfwGetTime();
    }

    Application::~Application() {
        m_device->waitForIdle();
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

    void Application::waitWindowEvents() { m_impl->window->waitForEvents(); }

    uint32_t Application::getWidth() const {
        return m_impl->window->getWidth();
    }

    uint32_t Application::getHeight() const {
        return m_impl->window->getHeight();
    }

    void Application::requestRedraw() {
        m_needsRedraw = true;
    }

    rhi::Swapchain* Application::getSwapchain(){
        return m_impl->swapchain.get();
    }

    std::optional<FrameInfo> Application::beginFrame() {
        m_impl->window->pollEvents();
        
        // 最小化時は描画をスキップ
        m_impl->isMinimanized = m_impl->window->getWidth() == 0 || m_impl->window->getHeight() == 0;
        if(m_impl->isMinimanized){
            return std::nullopt;
        }

        // 管理下のグラフをリサイズ
        if (m_impl->resizeRequested) {
            m_device->waitForIdle();
            uint32_t newW = m_impl->window->getWidth();
            uint32_t newH = m_impl->window->getHeight();
            for (auto* graph : m_managedGraphs) {
                if (graph) {
                    graph->resize(newW, newH);
                }
            }
            m_impl->resizeRequested = false;
        }

        m_device->beginFrame();

        
        if (!m_impl->swapchain->acquireNextImage()) {
            m_impl->swapchain->recreate(m_impl->window->getWidth(), m_impl->window->getHeight());
            return std::nullopt; 
        }

        return FrameInfo{m_device->getCurrentFrame()};
    }

    void Application::endFrame() {
        //最小化時はスキップ
        if(m_impl->isMinimanized) return;

        // フレームカウンタを進める等の処理
        m_device->endFrame();

        // 画面に表示 (内部でPresentSemaphoreを待つ)
        if (!m_impl->swapchain->present()) {
            m_impl->swapchain->recreate(m_impl->window->getWidth(), m_impl->window->getHeight());
        }
    }

} // namespace core