#pragma once
#include "rhi/RHIForward.hpp"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>




class Renderer {
public:
    Renderer(rhi::Device& device, uint32_t width, uint32_t height)
        : m_device(device), m_width(width), m_height(height) {
    }
    ~Renderer(){
        
    }

    // レンダリングの実行
    void render(float time);

    // 結果保存
    //void saveResult(const std::string& filename, rhi::Buffer& stagingBuffer);

private:
    rhi::Device& m_device;
    uint32_t m_width;
    uint32_t m_height;

};