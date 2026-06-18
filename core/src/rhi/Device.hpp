#pragma once
#include <memory>
#include <string>
#include <functional>
#include "RHIcommon.hpp"
#include "rhi/Resource.hpp"
#include "RHIForward.hpp"
#include "RHIcommon.hpp"
#include "core/Window.hpp"
#include "core/RenderGraph.hpp"
#include "CommandList.hpp"
#include "UploadManager.hpp"

namespace rhi {
    class Device {
    public:
        virtual ~Device() = default;

        virtual void initialize(const core::VulkanProvider provider = {}) = 0;
        
        // フレーム管理
        virtual void beginFrame() = 0;
        virtual void endFrame() = 0;
        virtual void waitForFrame(uint64_t frameIndex) = 0;
        virtual void waitForIdle() = 0;
        virtual uint64_t getCurrentFrame() const = 0;

        // timeline semaphore 関連の抽象メソッド
        virtual SyncPoint advanceTimeline(QueueType type) = 0;
        // 指定したキューでGPUが完了した最新のタイムライン値を取得する
        virtual uint64_t getCompletedTimelineValue(QueueType type) = 0;
        // CPU側で指定した SyncPoint が全てGPUで完了するまで待機する
        virtual void waitTimeline(const std::vector<SyncPoint>& syncPoints, uint64_t timeoutNs = UINT64_MAX) = 0;

        // リソース作成ファクトリ
        virtual std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc) = 0;
        virtual std::unique_ptr<Image> createImage(const ImageDesc& desc) = 0;
        virtual std::unique_ptr<RenderGraph> createRenderGraph() = 0; // 必要に応じて追加
        virtual std::unique_ptr<CommandList> createCommandList(QueueType queueType = QueueType::Compute) = 0;
        virtual std::unique_ptr<Swapchain> createSwapchain(const core::Window& window, const SwapchainConfig& config = {}) = 0;
        // アップロードマネージャーの取得
        virtual UploadManager* getUploadManager() = 0;

        // 削除キューへの登録 (内部用)
        virtual void enqueueDeletion(std::function<void()>&& deletionFunc) = 0;
    };
}