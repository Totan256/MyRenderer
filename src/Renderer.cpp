#include "Renderer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "RenderGraph.hpp"
#include "CommandList.hpp"
#include "ImageExporter.hpp"
#include "rhi/UploadManager.hpp" 
#include <iostream>
#include <cstddef>
#include <vector>

struct TestPushConstants {
    uint32_t outputBufferIndex;
    uint32_t paletteIndex;
    uint32_t staticUboIndex;
    uint32_t dynamicUboIndex;
    uint32_t dynamicUboOffset;
};

struct StaticUniformData {
    uint32_t resX;
    uint32_t resY;
    float time;
    float padding;
};

struct DynamicUniformData {
    uint32_t offsetX;
    uint32_t offsetY;
    float multiplier;
    uint32_t paletteId;
};

struct ColorVec4 {
    float r, g, b, a;
};

void Renderer::render(float time) {
    auto& vkDevice = static_cast<rhi::vk::VulkanDevice&>(m_device);

    // 毎フレームの開始処理 (Ring Buffer 等のリセット)
    vkDevice.beginFrame();

    std::cout << "--- Initializing Buffers ---" << std::endl;

    // 1. 出力先バッファ
    size_t pixelBufferSize = m_width * m_height * sizeof(ColorVec4);
    auto outputBuffer = m_device.createBuffer({pixelBufferSize, rhi::BufferUsageFlags::StorageBuffer, true});

    // 2. SSBO (パレットデータ)
    std::vector<ColorVec4> palette = {
        {1.0f, 0.2f, 0.2f, 1.0f}, // 0: Red
        {0.2f, 1.0f, 0.2f, 1.0f}, // 1: Green
        {0.2f, 0.2f, 1.0f, 1.0f}, // 2: Blue
        {1.0f, 1.0f, 0.2f, 1.0f}  // 3: Yellow
    };
    
    auto paletteBuffer = m_device.createBuffer({
        palette.size() * sizeof(ColorVec4), 
        rhi::BufferUsageFlags::StorageBuffer | rhi::BufferUsageFlags::TransferDst, 
        false
    });
    
    // ★ 遅延アップロードの実行 (グラフコンパイル時に自動的に Transfer Queue に乗る)
    m_device.getUploadManager()->uploadBuffer(paletteBuffer.get(), palette.data(), palette.size() * sizeof(ColorVec4), rhi::UploadMode::Deferred);

    // 3. 静的 Uniform Buffer
    StaticUniformData staticData = { m_width, m_height, time, 0.0f };
    auto staticUbo = m_device.createBuffer({
        sizeof(StaticUniformData), 
        rhi::BufferUsageFlags::UniformBuffer | rhi::BufferUsageFlags::TransferDst, 
        false
    });
    
    // ★ 遅延アップロードの実行
    m_device.getUploadManager()->uploadBuffer(staticUbo.get(), &staticData, sizeof(StaticUniformData), rhi::UploadMode::Deferred);


    // 4. RenderGraph の構築
    std::cout << "--- Building Render Graph ---" << std::endl;
    auto graph = m_device.createRenderGraph();
    
    auto hOutput    = graph->importResource(outputBuffer.get());
    auto hPalette   = graph->importResource(paletteBuffer.get());
    auto hStaticUbo = graph->importResource(staticUbo.get());

    auto& bindGroup = graph->createBindGroup({
        {offsetof(TestPushConstants, outputBufferIndex), rhi::ResourceState::StorageWrite},
        {offsetof(TestPushConstants, paletteIndex), rhi::ResourceState::StorageRead},
        {offsetof(TestPushConstants, staticUboIndex), rhi::ResourceState::ConstantBuffer}
    });

    auto& pass = graph->addPass("UniformTest", "shaders/uniform_test.comp")
                      .bind(bindGroup);

    uint32_t halfW = m_width / 2;
    uint32_t halfH = m_height / 2;
    uint32_t groupX = (halfW + 15) / 16;
    uint32_t groupY = (halfH + 15) / 16;

    // Dispatch 0
    DynamicUniformData dyn0 = {0, 0, 1.0f, 0};
    pass.dispatch(groupX, groupY, 1)
        .updateResource(offsetof(TestPushConstants, outputBufferIndex), hOutput)
        .updateResource(offsetof(TestPushConstants, paletteIndex), hPalette)
        .setStaticUniform(offsetof(TestPushConstants, staticUboIndex), hStaticUbo)
        .setUniform(offsetof(TestPushConstants, dynamicUboIndex), dyn0);

    // Dispatch 1
    DynamicUniformData dyn1 = {halfW, 0, 1.5f, 1};
    pass.dispatch(groupX, groupY, 1)
        .updateResource(offsetof(TestPushConstants, outputBufferIndex), hOutput)
        .updateResource(offsetof(TestPushConstants, paletteIndex), hPalette)
        .setStaticUniform(offsetof(TestPushConstants, staticUboIndex), hStaticUbo)
        .setUniform(offsetof(TestPushConstants, dynamicUboIndex), dyn1);

    // Dispatch 2
    DynamicUniformData dyn2 = {0, halfH, 0.8f, 2};
    pass.dispatch(groupX, groupY, 1)
        .updateResource(offsetof(TestPushConstants, outputBufferIndex), hOutput)
        .updateResource(offsetof(TestPushConstants, paletteIndex), hPalette)
        .setStaticUniform(offsetof(TestPushConstants, staticUboIndex), hStaticUbo)
        .setUniform(offsetof(TestPushConstants, dynamicUboIndex), dyn2);

    // Dispatch 3
    DynamicUniformData dyn3 = {halfW, halfH, 2.0f, 3};
    pass.dispatch(groupX, groupY, 1)
        .updateResource(offsetof(TestPushConstants, outputBufferIndex), hOutput)
        .updateResource(offsetof(TestPushConstants, paletteIndex), hPalette)
        .setStaticUniform(offsetof(TestPushConstants, staticUboIndex), hStaticUbo)
        .setUniform(offsetof(TestPushConstants, dynamicUboIndex), dyn3);

    // グラフのコンパイル
    graph->compile();

    uint64_t currentFrame = m_device.getCurrentFrame();

    // 5. 実行 (フェンスをシグナルし、CPUは待たずに即座に抜ける)
    std::cout << "--- Executing Render Graph ---" << std::endl;
    graph->execute();

    // フレーム終了処理
    vkDevice.endFrame();

    // 6. 画像保存のため、たった今投げたフレームの完了を待機
    std::cout << "Waiting for frame to complete..." << std::endl;
    vkDevice.waitForFrame(currentFrame);

    std::cout << "Saving result to output_uniform_test.png..." << std::endl;
    ImageExporter::savePng("output_uniform_test.png", m_width, m_height, outputBuffer->map());
    outputBuffer->unmap();
}