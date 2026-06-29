#pragma once
#include "rhi/GPUProfiler.hpp"
#include "VulkanDevice.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace rhi::vk {
    class VulkanGPUProfiler : public GPUProfiler {
    public:
        static constexpr uint32_t MAX_QUERIES_PER_FRAME = 256;

        VulkanGPUProfiler(VulkanDevice& device, uint32_t maxFramesInFlight);
        ~VulkanGPUProfiler() override;

        void reset() override;
        uint32_t registerPass(const std::string& passName) override;
        uint32_t getFrameQueryOffset(uint64_t currentFrame) const override;
        
        void resolveResults(uint64_t currentFrame) override;
        bool hasNewResults() const override { return m_hasNewResults; }
        void dumpToConsole() override;
        void resetStats() {m_stats.clear();}

        VkQueryPool getQueryPool() const { return m_queryPool; }

    private:
        VulkanDevice& m_device;
        VkQueryPool m_queryPool = VK_NULL_HANDLE;
        float m_timestampPeriod = 1.0f;
        uint32_t m_maxFramesInFlight;
        
        std::vector<std::string> m_passNames;
        
        bool m_hasNewResults = false;
        std::vector<float> m_latestResults;
        uint64_t m_latestFrame = 0;
        struct PassStats {
            float totalTime = 0.0f;
            float firstFrameTime = 0.0f; // 1フレーム目の計測値
            uint32_t count = 0;          // 現在までのフレーム数
        };
        std::vector<PassStats> m_stats; // パスごとの統計を保持
    };
}