#pragma once
#include <vector>
#include <map>
#include <string>
#include "RHIcommon.hpp"
#include "../utils/StringHash.hpp"

namespace rhi::vk {
    struct ShaderReflectionData {
        rhi::PassType passType = rhi::PassType::Compute;
        rhi::QueueType queueType = rhi::QueueType::Compute;
        uint32_t localSizeX = 1;
        uint32_t localSizeY = 1;
        uint32_t localSizeZ = 1;
        std::map<StringHash, uint32_t> pushConstantOffsets;
    };

    class ShaderReflection {
    public:
        static ShaderReflectionData reflect(const std::vector<uint32_t>& spirvCode);
    };
}