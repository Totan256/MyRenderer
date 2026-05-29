// src/rhi/RHI.hpp
#pragma once
#include <memory>
#include "Device.hpp"

namespace rhi {
    enum class GraphicsAPI {
        Vulkan,
        DX12 // 将来用
    };

    // デバイス生成用のファクトリ関数
    std::unique_ptr<Device> createDevice(GraphicsAPI api);
}