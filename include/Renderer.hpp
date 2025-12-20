#pragma once
#include "rhi/RHI.hpp"
#include <memory>
#include <vector>




class Renderer {
public:
    Renderer(rhi::Device& device, uint32_t width, uint32_t height);
    ~Renderer();

    // レンダリングの実行
    void render(float time);

    // 結果保存
    void saveResult(const std::string& filename);

private:
    rhi::Device& m_device;
    uint32_t m_width;
    uint32_t m_height;

    // リソース
    std::unique_ptr<rhi::Image> m_outputImage;
    std::unique_ptr<rhi::Buffer> m_stagingBuffer;
    std::unique_ptr<rhi::Buffer> m_sceneBuffer;

    // パイプライン・ディスクリプタ
    std::unique_ptr<rhi::ComputePipeline> m_pipeline;
    std::unique_ptr<DescriptorManager> m_descManager;
    VkDescriptorSet m_descriptorSet;

    void setupResources();
    void setupPipeline();
};