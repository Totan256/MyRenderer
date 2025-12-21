#include "Renderer.hpp"
#include "ImageExporter.hpp"
#include "rhi/RHI.hpp"
#include <glm/glm.hpp>
#include <iostream>
struct SceneData {
    glm::vec4 resolution; // width, height, 0, 0
    glm::vec4 params;     // time, frame, 0, 0
    glm::vec4 cameraPos;
};
struct PushConstants {
    uint32_t outputImageIndex;
    uint32_t sceneBufferIndex;
};

Renderer::Renderer(rhi::Device& device, uint32_t width, uint32_t height)
    : m_device(device), m_width(width), m_height(height) {
    setupResources();
    setupPipeline();
}

Renderer::~Renderer() {
    // std::unique_ptr が自動的にリソースを解放
}

void Renderer::setupResources() {
    // 1. 出力用Image
    m_outputImage = std::make_unique<rhi::Image>(m_device, m_width, m_height);

    // 2. 読み戻し用Staging Buffer (RGBA8 = 4 bytes per pixel)
    VkDeviceSize imageSize = m_width * m_height * 4;
    m_stagingBuffer = std::make_unique<rhi::Buffer>(m_device, m_device.getAllocator(), imageSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, // TRANSFER_DSTを追加
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
    // 3. Uniform Buffer (SceneData)
    m_sceneBuffer = std::make_unique<rhi::Buffer>(m_device, m_device.getAllocator(), sizeof(SceneData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, // 修正
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
}

void Renderer::setupPipeline() {
    m_pipeline = std::make_unique<rhi::ComputePipeline>(m_device, "test.spv");
}

void Renderer::render(float time) {
    // シーン情報の更新
    SceneData scene{};
    scene.resolution = glm::vec4(m_width, m_height, 0, 0);
    scene.params = glm::vec4(time, 0, 0, 0);
    m_sceneBuffer->writeData(&scene, sizeof(SceneData));

    rhi::CommandList cmd(m_device);
    cmd.begin();

    // 1. Layout遷移: Undefined -> General
    m_outputImage->transitionLayout(cmd.getCommandBuffer(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    // 2. Dispatch
    cmd.bindPipeline(*m_pipeline);
    cmd.bindGlobalDescriptorSet(*m_pipeline);
    // インデックスだけをシェーダに渡す
    PushConstants pc;
    pc.outputImageIndex = m_outputImage->getBindlessIndex();
    pc.sceneBufferIndex = m_sceneBuffer->getBindlessIndex();
    vkCmdPushConstants(cmd.getCommandBuffer(), m_pipeline->getPipelineLayout(),
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);
    cmd.dispatch((uint32_t)ceil(m_width / 16.0), (uint32_t)ceil(m_height / 16.0), 1);

    // 3. Layout遷移: General -> Transfer Source
    m_outputImage->transitionLayout(cmd.getCommandBuffer(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // 4. Image -> Buffer コピー
    m_outputImage->copyToBuffer(cmd.getCommandBuffer(), m_stagingBuffer->getNativeBuffer());

    cmd.end();
    cmd.submitAndWait();
}

void Renderer::saveResult(const std::string& filename) {
    // GPUの書き込みをCPUから見えるようにする
    vmaInvalidateAllocation(m_device.getAllocator(), m_stagingBuffer->getAllocation(), 0, VK_WHOLE_SIZE);
    
    void* data = m_stagingBuffer->map();
    ImageExporter::savePngUint8(filename, m_width, m_height, data);
    m_stagingBuffer->unmap();
    
    std::cout << "Render result saved to " << filename << std::endl;
}