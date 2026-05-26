#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <optional>
#include <memory>
#include <string>
#include "Resource.hpp"

namespace rhi {
    struct UploadRequest {
        Buffer* stagingBuffer;
        size_t stagingOffset;
        Buffer* dstBuffer;
        size_t size;
    };
    struct ImageUploadRequest {
        Buffer* stagingBuffer;
        size_t stagingOffset;
        Image* dstImage;
        uint32_t width, height, mipLevels;
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
        virtual std::unique_ptr<Image> uploadImageFromFile(
            const std::string& filepath, 
            std::optional<rhi::ImageDesc> overrideDesc = std::nullopt,
            UploadMode mode = UploadMode::Deferred) = 0;
        
        // RenderGraphがコンパイル時に、使用されるバッファの非同期セマフォを回収するための関数
        virtual std::vector<SemaphoreHandle> consumeAsyncSemaphores(const std::vector<Buffer*>& buffers) = 0;
        virtual std::vector<SemaphoreHandle> consumeImageSemaphores(const std::vector<Image*>& images) = 0;

        virtual std::vector<UploadRequest> getAndClearPendingUploads() = 0;
        virtual std::vector<rhi::ImageUploadRequest> getAndClearPendingImageUploads() = 0;
        virtual void beginFrame(uint64_t currentFrameIndex) = 0;
        virtual void flushImmediate() = 0;
    };
}