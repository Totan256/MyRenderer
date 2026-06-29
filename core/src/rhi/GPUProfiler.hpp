#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace rhi {
    class CommandList;

    class GPUProfiler {
    public:
        virtual ~GPUProfiler() = default;
        
        // RenderGraphコンパイル時にリセットとパス登録を行う
        virtual void reset() = 0;
        virtual uint32_t registerPass(const std::string& passName) = 0;
        
        // フレームオフセット計算用
        virtual uint32_t getFrameQueryOffset(uint64_t currentFrame) const = 0;
        
        // デバイスのbeginFrameフェンス待機後に呼び出し、完了した過去フレームのデータを回収
        virtual void resolveResults(uint64_t currentFrame) = 0;
        
        // 外部への結果出力用
        virtual bool hasNewResults() const = 0;
        virtual void dumpToConsole() = 0;
        virtual void resetStats() = 0;
    };
}