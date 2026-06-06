#pragma once

#include <memory>
#include <cstdint>
#include "rhi/RHIForward.hpp"
#include "rhi/RHIcommon.hpp"
#include "rhi/Resource.hpp" // rhi::Image用

namespace rhi {

/**
 * @brief スワップチェーンの抽象化インターフェース
 * * OSのウィンドウシステムと連携し、描画結果を画面に表示するためのバッファ群を管理します。
 * API固有の同期処理（Acquire/Present）はDevice側で隠蔽し、
 * RHIユーザーには「現在の描画先Image」と「リサイズ機能」のみを公開します。
 */
class Swapchain {
public:
    virtual ~Swapchain() = default;

    virtual void recreate(uint32_t width, uint32_t height) = 0;
    virtual std::shared_ptr<rhi::Image> getCurrentImage(uint32_t index) const = 0;
    virtual uint32_t getImageCount() const = 0;
    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;

    // 現在のフレームにおける描画開始待ち(Acquire)用セマフォハンドルを取得
    virtual SemaphoreHandle getCurrentAcquireSemaphore() const = 0;
    // 現在のフレームにおける画面表示(Present)用セマフォハンドルを取得
    virtual SemaphoreHandle getCurrentPresentSemaphore() const = 0;
};

} // namespace rhi