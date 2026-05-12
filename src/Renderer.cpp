#include "Renderer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "RenderGraph.hpp"
#include "CommandList.hpp"
#include "ImageExporter.hpp"
#include <iostream>
#include <cstddef>
#include <vector>

// --- データ構造体の定義 ---
struct TestPushConstants {
    uint32_t outputBufferIndex;
    uint32_t paletteIndex;
    uint32_t staticUboIndex;
    
    // updateUniform によって書き込まれるインデックスとオフセット(合計8バイト)
    uint32_t dynamicUboIndex;
    uint32_t dynamicUboOffset;
};

// 静的 Uniform (16バイト)
struct StaticUniformData {
    uint32_t resX;
    uint32_t resY;
    float time;
    float padding;
};

// 動的 Uniform (16バイト)
struct DynamicUniformData {
    uint32_t offsetX;
    uint32_t offsetY;
    float multiplier;
    uint32_t paletteId;
};

// カラーピクセル/パレット用
struct ColorVec4 {
    float r, g, b, a;
};

void Renderer::render(float time) {
    auto& vkDevice = static_cast<rhi::vk::VulkanDevice&>(m_device);

    std::cout << "--- Initializing Buffers ---" << std::endl;

    // 1. 出力先バッファ (画像として保存するため CPU から Read 可能にしておく)
    size_t pixelBufferSize = m_width * m_height * sizeof(ColorVec4);
    auto outputBuffer = m_device.createBuffer({pixelBufferSize, rhi::BufferUsageFlags::StorageBuffer, true});

    // 2. SSBO (パレットデータ)
    std::vector<ColorVec4> palette = {
        {1.0f, 0.2f, 0.2f, 1.0f}, // 0: Red
        {0.2f, 1.0f, 0.2f, 1.0f}, // 1: Green
        {0.2f, 0.2f, 1.0f, 1.0f}, // 2: Blue
        {1.0f, 1.0f, 0.2f, 1.0f}  // 3: Yellow
    };
    auto paletteBuffer = m_device.createBuffer({palette.size() * sizeof(ColorVec4), rhi::BufferUsageFlags::StorageBuffer, true});
    std::memcpy(paletteBuffer->map(), palette.data(), palette.size() * sizeof(ColorVec4));
    paletteBuffer->unmap();

    // 3. 静的 Uniform Buffer
    StaticUniformData staticData = { m_width, m_height, time, 0.0f };
    auto staticUbo = m_device.createBuffer({sizeof(StaticUniformData), rhi::BufferUsageFlags::UniformBuffer, true});
    std::memcpy(staticUbo->map(), &staticData, sizeof(StaticUniformData));
    staticUbo->unmap();

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

    // 画面を4分割して描画するため、それぞれのサイズを計算
    uint32_t halfW = m_width / 2;
    uint32_t halfH = m_height / 2;
    uint32_t groupX = (halfW + 15) / 16;
    uint32_t groupY = (halfH + 15) / 16;

    // --- マルチディスパッチの設定 (動的Uniformの適用) ---
    
    // Dispatch 0: 左上
    DynamicUniformData dyn0 = {0, 0, 1.0f, 0};
    pass.dispatch(groupX, groupY, 1)
        .updateResource(offsetof(TestPushConstants, outputBufferIndex), hOutput)
        .updateResource(offsetof(TestPushConstants, paletteIndex), hPalette)
        .setStaticUniform(offsetof(TestPushConstants, staticUboIndex), hStaticUbo)
        .setUniform(offsetof(TestPushConstants, dynamicUboIndex), dyn0);

    // Dispatch 1: 右上
    DynamicUniformData dyn1 = {halfW, 0, 1.5f, 1};
    pass.dispatch(groupX, groupY, 1)
        .updateResource(offsetof(TestPushConstants, outputBufferIndex), hOutput)
        .updateResource(offsetof(TestPushConstants, paletteIndex), hPalette)
        .setStaticUniform(offsetof(TestPushConstants, staticUboIndex), hStaticUbo)
        .setUniform(offsetof(TestPushConstants, dynamicUboIndex), dyn1);

    // Dispatch 2: 左下
    DynamicUniformData dyn2 = {0, halfH, 0.8f, 2};
    pass.dispatch(groupX, groupY, 1)
        .updateResource(offsetof(TestPushConstants, outputBufferIndex), hOutput)
        .updateResource(offsetof(TestPushConstants, paletteIndex), hPalette)
        .setStaticUniform(offsetof(TestPushConstants, staticUboIndex), hStaticUbo)
        .setUniform(offsetof(TestPushConstants, dynamicUboIndex), dyn2);

    // Dispatch 3: 右下
    DynamicUniformData dyn3 = {halfW, halfH, 2.0f, 3};
    pass.dispatch(groupX, groupY, 1)
        .updateResource(offsetof(TestPushConstants, outputBufferIndex), hOutput)
        .updateResource(offsetof(TestPushConstants, paletteIndex), hPalette)
        .setStaticUniform(offsetof(TestPushConstants, staticUboIndex), hStaticUbo)
        .setUniform(offsetof(TestPushConstants, dynamicUboIndex), dyn3);

    graph->compile();

    // 5. 実行
    std::cout << "--- Executing Command List ---" << std::endl;
    auto cmd = m_device.createCommandList();
    cmd->begin();
    graph->execute(*cmd);
    cmd->end();
    cmd->submitAndWait();

    // 6. 画像保存
    std::cout << "Saving result to output_uniform_test.png..." << std::endl;
    ImageExporter::savePng("output_uniform_test.png", m_width, m_height, outputBuffer->map());
    outputBuffer->unmap();
}