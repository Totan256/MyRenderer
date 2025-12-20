#pragma once
#include "GraphicsDevice.hpp"
#include "CommandList.hpp"
#include "DescriptorManager.hpp"
#include "GPUImage.hpp"
#include "GPUBuffer.hpp"
#include <memory>
#include <vector>

// 前方宣言
class Texture; 
class ComputePipeline;
class GpuImage;



class Renderer {
public:
    Renderer(GraphicsDevice& device, uint32_t width, uint32_t height);
    ~Renderer();

    // レンダリングの実行
    void render(float time);

    // 結果保存
    void saveResult(const std::string& filename);

private:
    GraphicsDevice& m_device;
    uint32_t m_width;
    uint32_t m_height;

    // リソース
    std::unique_ptr<GpuImage> m_outputImage;
    std::unique_ptr<GpuBuffer> m_stagingBuffer;
    std::unique_ptr<GpuBuffer> m_sceneBuffer;

    // パイプライン・ディスクリプタ
    std::unique_ptr<ComputePipeline> m_pipeline;
    std::unique_ptr<DescriptorManager> m_descManager;
    VkDescriptorSet m_descriptorSet;

    void setupResources();
    void setupPipeline();
};