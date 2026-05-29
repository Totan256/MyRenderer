#pragma once
#include "rhi/RHIForward.hpp"
#include "rhi/Device.hpp"
#include "rhi/ModelBuilder.hpp" // ★追加
#include <memory>
#include <cstdint>

class Renderer {
public:
    Renderer(rhi::Device& device, uint32_t width, uint32_t height); // ★変更
    ~Renderer();

    void render(float time);

private:
    rhi::Device& m_device;
    uint32_t m_width;
    uint32_t m_height;
    std::unique_ptr<GpuModel> m_bunnyModel; // ★追加: ロードしたモデルを保持
};