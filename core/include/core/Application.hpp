#pragma once

#include <string>
#include <memory>
#include <cstdint>
#include <optional>

namespace rhi {
    class Device;
    class Swapchain;
    class Image;
}

namespace core {

enum class AppMode { RealTime, OnDemand };

// フレームの開始時に開発者に渡される同期・リソース情報
struct FrameInfo {
    uint64_t frameIndex;  // 何番目のフレームか（定数バッファの切り替え等に使用）
    uint32_t imageIndex;  // スワップチェーンのどの画像か（テクスチャバインディング等に使用）
};

class Application {
public:
    struct Config {
        std::string title = "Vulkan Application";
        uint32_t width = 1280;
        uint32_t height = 720;
        AppMode mode = AppMode::RealTime;
    };

    Application(const Config& config);
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool isRunning() const;
    void processEvents();
    void requestRedraw();
    
    // --- 変更後のフレーム管理 ---
    // 描画不可能な状態（最小化やリサイズ中）の場合は std::nullopt を返す
    std::optional<FrameInfo> beginFrame();
    void endFrame(uint32_t imageIndex);

    // イメージインデックスから対応するバックバッファを取得する
    rhi::Image* getBackImage(uint32_t imageIndex);
    rhi::Device& getDevice() { return *m_device; }
    rhi::Swapchain& getSwapchain() { return *m_swapchain; }

    float getDeltaTime() const { return m_deltaTime; }
    uint32_t getWidth() const;
    uint32_t getHeight() const;

protected:
    virtual void onResize(uint32_t width, uint32_t height) {}

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::unique_ptr<rhi::Device> m_device;
    std::unique_ptr<rhi::Swapchain> m_swapchain;
    AppMode m_mode;
    bool m_needsRedraw = true;
    float m_deltaTime = 0.0f;
    uint64_t m_lastTimestamp = 0;
};

} // namespace core