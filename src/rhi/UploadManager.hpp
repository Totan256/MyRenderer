#pragma once
#include <cstddef>
#include <vector>
#include "Resource.hpp"

namespace rhi {
    struct UploadRequest {
        Buffer* stagingBuffer;
        size_t stagingOffset;
        Buffer* dstBuffer;
        size_t size;
    };

    enum class UploadMode {
        Deferred,    // RenderGraphのコンパイル時にCopyパスとして組み込む
        Async,       // 即座にTransfer Queueに発行し、グラフが自動でセマフォ待機する
        Immediate    // 即座にTransfer Queueに発行し、CPUで完了を待機する（ロード画面等）
    };

    class UploadManager {
    public:
        virtual ~UploadManager() = default;

        virtual void uploadBuffer(Buffer* dstBuffer, const void* data, size_t size, UploadMode mode = UploadMode::Deferred) = 0;
        
        // ※Deferredモードのみサポート (Immediate/Asyncでマップを使う場合は別途flush/unmapの設計が必要なため)
        virtual void* mapForDeferredUpload(Buffer* dstBuffer, size_t size) = 0;
        
        // RenderGraphがコンパイル時に、使用されるバッファの非同期セマフォを回収するための関数
        virtual std::vector<SemaphoreHandle> consumeAsyncSemaphores(const std::vector<Buffer*>& buffers) = 0;

        virtual std::vector<UploadRequest> getAndClearPendingUploads() = 0;
        virtual void beginFrame(uint64_t currentFrameIndex) = 0;
    };
}