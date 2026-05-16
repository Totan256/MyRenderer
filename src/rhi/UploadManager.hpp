#pragma once
#include <cstddef>
#include <vector>
#include "Resource.hpp"

namespace rhi {
    class UploadManager {
    public:
        virtual ~UploadManager() = default;

        // 1. 即時インポート（ロード画面等で使用。完了するまでCPUが待機する）
        virtual void uploadImmediate(Buffer* dstBuffer, const void* data, size_t size) = 0;
        
        // 連続した即時インポートの後に、一括で完了を待機して一時リソースを破棄する
        virtual void waitForImmediateUploads() = 0;

        // 2. 遅延インポート（ゲームループで使用。CPUは待機せず、GPUタイムラインで同期）
        // パターンA: 所有権の移動（大容量データ向け、Zero-Copy）
        virtual void requestUploadDeferred(Buffer* dstBuffer, std::vector<uint8_t>&& data) = 0;
        
        // パターンB: バッファへの直接コピー（小容量データ向け、Fire and Forget）
        virtual void requestUploadDeferred(Buffer* dstBuffer, const void* data, size_t size) = 0;

        // 3. システムからの呼び出し用
        // 溜まった遅延タスクをTransferキューに送信（RenderGraph実行前に呼ぶ）
        virtual void flushDeferredUploads() = 0;
        
        // フレーム終了時に古い一時リソースをガベージコレクトする
        virtual void garbageCollect(uint64_t completedFrameId) = 0;
    };
}