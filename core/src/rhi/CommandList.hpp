#pragma once
#include <cstddef>
#include "RHIcommon.hpp"
#include "GPUProfiler.hpp"

namespace rhi {
    class CommandList {
    public:
        virtual ~CommandList() = default;

        virtual void begin() = 0;
        virtual void end() = 0;
        
        virtual void submit(SemaphoreHandle waitSemaphore = nullptr, SemaphoreHandle signalSemaphore = nullptr) = 0;
        virtual void wait() = 0;
        virtual void reset() = 0;

        // オフラインレンダリング用の簡易同期メソッド
        virtual void submitAndWait() = 0; 
        virtual void copyBuffer(rhi::Buffer* src, rhi::Buffer* dst, size_t size, size_t srcOffset = 0, size_t dstOffset = 0) = 0;

        // プロファイリング用
        virtual void resetQueryPool(GPUProfiler* profiler, uint32_t firstQuery, uint32_t queryCount) = 0;
        virtual void writeTimestamp(GPUProfiler* profiler, uint32_t queryIndex, PipelineStage stage) = 0;

    };
}