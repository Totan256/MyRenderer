#pragma once
#include <cstddef>
#include <vector>
#include "Resource.hpp"

namespace rhi {
    class UploadManager {
    public:
        virtual ~UploadManager() = default;

        // ---------------------------------------------------------
        // 1. 即時インポート (ロード画面・初期化用)
        // ---------------------------------------------------------
        // ステージングメモリを確保し、CPUから直接書き込めるポインタを返す
        virtual void* mapForUploadImmediate(Buffer* dstBuffer, size_t size) = 0;
        
        // データを渡してコピーする便利関数
        virtual void uploadImmediate(Buffer* dstBuffer, const void* data, size_t size) = 0;
        
        // これまでに積んだ即時インポートコマンドを送信し、CPUで完了を待機する
        virtual void waitForImmediateUploads() = 0;

        // ---------------------------------------------------------
        // 2. 遅延インポート (ゲームループ用)
        // ---------------------------------------------------------
        // ステージングメモリを確保し、直接書き込めるポインタを返す
        virtual void* mapForUploadDeferred(Buffer* dstBuffer, size_t size) = 0;
        
        // データコピー版
        virtual void requestUploadDeferred(Buffer* dstBuffer, const void* data, size_t size) = 0;

        // 遅延コマンドをGPUに送信し、完了時にシグナルされるSemaphoreを返す
        // CPUは待機せず、後続の描画コマンドがこのSemaphoreをWaitする
        virtual SemaphoreHandle flushDeferredUploads() = 0;
        
        // ---------------------------------------------------------
        // フレーム管理
        // ---------------------------------------------------------
        // 毎フレームの開始時に呼び出し、そのフレームで使用するリソースをリセットする
        // (呼び出し時点で、このフレームインデックスの過去のGPU実行は完了している前提)
        virtual void beginFrame(uint64_t currentFrameIndex) = 0;
    };
}