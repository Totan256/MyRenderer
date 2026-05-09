#include "Renderer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "RenderGraph.hpp"
#include "CommandList.hpp"
#include <iostream>
#include <cstddef>
#include <vector>

struct ScanPushConstants {
    uint32_t inputIndex;
    uint32_t outputIndex;
    uint32_t elementCount;
};

void Renderer::render(float time) {
    auto& vkDevice = static_cast<rhi::vk::VulkanDevice&>(m_device);

    const uint32_t ELEMENT_COUNT = 1024; // 1ワークグループに収まる要素数
    size_t bufferSize = ELEMENT_COUNT * sizeof(uint32_t);

    // 1. 入出力バッファの作成
    auto inputBuffer = m_device.createBuffer({bufferSize, rhi::BufferUsageFlags::StorageBuffer, true});
    auto outputBuffer = m_device.createBuffer({bufferSize, rhi::BufferUsageFlags::StorageBuffer, true});

    // 入力データの初期化（すべて 1 で埋める）
    uint32_t* inData = static_cast<uint32_t*>(inputBuffer->map());
    for(uint32_t i = 0; i < ELEMENT_COUNT; ++i) {
        inData[i] = 1; 
    }
    inputBuffer->unmap();

    // 2. RenderGraph の構築
    auto graph = m_device.createRenderGraph();
    
    auto hInput = graph->importResource(inputBuffer.get());
    auto hOutput = graph->importResource(outputBuffer.get());

    auto& bindGroup = graph->createBindGroup({
        {offsetof(ScanPushConstants, inputIndex), rhi::ResourceUsage::StorageRead},
        {offsetof(ScanPushConstants, outputIndex), rhi::ResourceUsage::StorageWrite}
    });

    auto& scanPass = graph->addPass("PrefixSum", "shaders/scan.comp")
        .bind(bindGroup);

    // 1024要素なので、ディスパッチは x=1 (1ワークグループのみ)
    scanPass.dispatch(1, 1, 1)
        .updateResource(offsetof(ScanPushConstants, inputIndex), hInput)
        .updateResource(offsetof(ScanPushConstants, outputIndex), hOutput)
        .updateConstant(offsetof(ScanPushConstants, elementCount), ELEMENT_COUNT);

    graph->compile();

    // 3. 実行
    auto cmd = m_device.createCommandList();
    cmd->begin();
    graph->execute(*cmd);
    cmd->end();
    cmd->submitAndWait(); // 同期的に待機

    // 4. 結果の検証
    uint32_t* outData = static_cast<uint32_t*>(outputBuffer->map());
    
    std::cout << "--- Parallel Prefix Sum Results ---" << std::endl;
    std::cout << "First 10 elements: ";
    for(int i = 0; i < 10; ++i) std::cout << outData[i] << " ";
    std::cout << std::endl;
    
    std::cout << "Last 3 elements: " 
              << outData[ELEMENT_COUNT - 3] << " " 
              << outData[ELEMENT_COUNT - 2] << " " 
              << outData[ELEMENT_COUNT - 1] << std::endl;
    
    outputBuffer->unmap();
}