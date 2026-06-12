#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <optional>
#include <memory>
#include <string>
#include "Resource.hpp"
#include "RHIcommon.hpp" // SyncPoint のため追加

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

    class UploadManager {
    public:
        virtual ~UploadManager() = default;

        virtual void enqueueBufferUpload(Buffer* dstBuffer, const void* data, size_t size, size_t dstOffset = 0) = 0;
        virtual void enqueueImageUpload(Image* dstImage, const void* data, size_t size, uint32_t width, uint32_t height, uint32_t mipLevels) = 0;

        // 非同期実行と待機 (戻り値を SyncPoint に変更)
        virtual SyncPoint submitUploadsAsync() = 0;
        virtual void waitUploads() = 0;

        // RenderGraph実行時に未回収の同期ポイントを全て回収する
        virtual std::vector<SyncPoint> consumeAsyncSyncPoints() = 0;

        virtual std::vector<UploadRequest> getAndClearPendingUploads() = 0;
        virtual std::vector<rhi::ImageUploadRequest> getAndClearPendingImageUploads() = 0;
        virtual void beginFrame(uint64_t currentFrameIndex) = 0;
    };
}