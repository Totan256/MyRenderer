#include "VulkanGPUProfiler.hpp"
#include <iostream>

namespace rhi::vk {
    VulkanGPUProfiler::VulkanGPUProfiler(VulkanDevice& device, uint32_t maxFramesInFlight)
        : m_device(device), m_maxFramesInFlight(maxFramesInFlight) {
        
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(m_device.getPhysicalDevice(), &properties);
        m_timestampPeriod = properties.limits.timestampPeriod;

        VkQueryPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        poolInfo.queryCount = MAX_QUERIES_PER_FRAME * m_maxFramesInFlight;

        if (vkCreateQueryPool(m_device.getDevice(), &poolInfo, nullptr, &m_queryPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create query pool for profiling!");
        }
    }

    VulkanGPUProfiler::~VulkanGPUProfiler() {
        if (m_queryPool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(m_device.getDevice(), m_queryPool, nullptr);
        }
    }

    void VulkanGPUProfiler::reset() {
        m_passNames.clear();
    }

    uint32_t VulkanGPUProfiler::registerPass(const std::string& passName) {
        uint32_t index = static_cast<uint32_t>(m_passNames.size());
        m_passNames.push_back(passName);
        return index * 2; // 開始と終了で2つ消費
    }

    uint32_t VulkanGPUProfiler::getFrameQueryOffset(uint64_t currentFrame) const {
        return (currentFrame % m_maxFramesInFlight) * MAX_QUERIES_PER_FRAME;
    }

    void VulkanGPUProfiler::resolveResults(uint64_t currentFrame) {
        if (m_passNames.empty() || currentFrame < m_maxFramesInFlight) {
            m_hasNewResults = false;
            return;
        }

        // FIF分前のフレームのデータを取得（GPU側は確実に完了しているためWAIT_BITは不要）
        uint64_t targetFrame = currentFrame - m_maxFramesInFlight;
        uint32_t frameOffset = getFrameQueryOffset(targetFrame);
        uint32_t queryCount = static_cast<uint32_t>(m_passNames.size()) * 2;

        std::vector<uint64_t> results(queryCount);
        VkResult res = vkGetQueryPoolResults(
            m_device.getDevice(),
            m_queryPool,
            frameOffset,
            queryCount,
            queryCount * sizeof(uint64_t),
            results.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT
        );

        if (res == VK_SUCCESS) {
            if (m_stats.size() != m_passNames.size()) m_stats.resize(m_passNames.size());
            if (m_latestResults.size() != m_passNames.size()) m_latestResults.resize(m_passNames.size());

            for (size_t i = 0; i < m_passNames.size(); ++i) {
                uint64_t start = results[i * 2];
                uint64_t end = results[i * 2 + 1];
                float durationMs = (start != 0 && end != 0 && end > start) 
                                ? static_cast<float>(end - start) * m_timestampPeriod / 1000000.0f 
                                : 0.0f;

                if (m_stats[i].count == 0) {
                    m_stats[i].firstFrameTime = durationMs;
                }
                m_stats[i].totalTime += durationMs;
                m_stats[i].count++;
            }
            m_hasNewResults = true;
            m_latestFrame = targetFrame;
        } else {
            m_hasNewResults = false;
        }
    }

    void VulkanGPUProfiler::dumpToConsole() {
        // データの整合性チェック
        if (!m_hasNewResults || m_latestResults.size() < m_passNames.size() || m_stats.size() < m_passNames.size()) {
            return;
        }
        
        std::cout << "--- Profiling Results (Frame " << m_latestFrame << ") ---" << std::endl;
        for (size_t i = 0; i < m_passNames.size(); ++i) {
            float avg = m_stats[i].totalTime / m_stats[i].count;
            
            std::cout << "  [ " << m_passNames[i] << " ]\n"
                    << "    Current: " << m_latestResults[i] << " ms\n"
                    << "    1st Frame: " << m_stats[i].firstFrameTime << " ms\n"
                    << "    Average: " << avg << " ms (" << m_stats[i].count << " frames)" << std::endl;
        }
        std::cout << "---------------------------------------" << std::endl;
        m_hasNewResults = false;
    }
}