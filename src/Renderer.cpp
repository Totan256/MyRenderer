#include "Renderer.hpp"
#include "vulkan/VulkanRenderGraph.hpp"
#include "vulkan/VulkanDevice.hpp"
#include <iostream>
#include <cstddef>

struct ScanPushConstants {
    uint32_t inputIndex;
    uint32_t outputIndex;
    uint32_t blockSumIndex;
    uint32_t elementCount;
};

struct AddPushConstants {
    uint32_t dataIndex;
    uint32_t blockSumIndex;
    uint32_t elementCount;
};

void Renderer::render(float time) {
    auto& vkDevice = static_cast<rhi::vk::VulkanDevice&>(m_device);

    // 最大 1024 * 1024 = 1,048,576 要素をテスト
    const uint32_t ELEMENT_COUNT = 1024 * 1024; 
    const uint32_t WORKGROUP_SIZE = 1024;
    const uint32_t NUM_WORKGROUPS = (ELEMENT_COUNT + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE; // = 1024

    size_t dataBufferSize = ELEMENT_COUNT * sizeof(uint32_t);
    size_t blockSumBufferSize = NUM_WORKGROUPS * sizeof(uint32_t);

    // 1. バッファの作成
    rhi::vk::VulkanBuffer inputBuffer(vkDevice, vkDevice.getAllocator(), dataBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
    rhi::vk::VulkanBuffer outputBuffer(vkDevice, vkDevice.getAllocator(), dataBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
    rhi::vk::VulkanBuffer blockSumBuffer(vkDevice, vkDevice.getAllocator(), blockSumBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);

    // 入力データの初期化（すべて 1 で埋める）
    uint32_t* inData = static_cast<uint32_t*>(inputBuffer.map());
    for(uint32_t i = 0; i < ELEMENT_COUNT; ++i) inData[i] = 1; 
    inputBuffer.unmap();

    // 2. RenderGraph の構築
    rhi::vk::VulkanRenderGraph graph(vkDevice);
    
    auto hInput = graph.importResource(&inputBuffer);
    auto hOutput = graph.importResource(&outputBuffer);
    auto hBlockSum = graph.importResource(&blockSumBuffer);

    // 各パスの要件定義
    auto& scanBindGroup = graph.createBindGroup({
        {offsetof(ScanPushConstants, inputIndex), rhi::ResourceUsage::StorageRead},
        {offsetof(ScanPushConstants, outputIndex), rhi::ResourceUsage::StorageWrite},
        {offsetof(ScanPushConstants, blockSumIndex), rhi::ResourceUsage::StorageWrite}
    });

    auto& addBindGroup = graph.createBindGroup({
        {offsetof(AddPushConstants, dataIndex), rhi::ResourceUsage::StorageWrite}, // Write(Read/Write)
        {offsetof(AddPushConstants, blockSumIndex), rhi::ResourceUsage::StorageRead}
    });

    // --- Pass 1: ローカルスキャン ---
    auto& pass1 = graph.addPass("LocalScan", "shaders/scan.comp").bind(scanBindGroup);
    pass1.dispatch(NUM_WORKGROUPS, 1, 1)
        .updateResource(offsetof(ScanPushConstants, inputIndex), hInput)
        .updateResource(offsetof(ScanPushConstants, outputIndex), hOutput)
        .updateResource(offsetof(ScanPushConstants, blockSumIndex), hBlockSum)
        .updateConstant(offsetof(ScanPushConstants, elementCount), ELEMENT_COUNT);

    // --- Pass 2: ブロック合計値のスキャン ---
    // blockSumBuffer 自身を入力かつ出力として扱う（In-placeスキャン）
    auto& pass2 = graph.addPass("GlobalScan", "shaders/scan.comp").bind(scanBindGroup);
    pass2.dispatch(1, 1, 1) // ブロック数は最大1024なので、1ワークグループで処理可能
        .updateResource(offsetof(ScanPushConstants, inputIndex), hBlockSum)
        .updateResource(offsetof(ScanPushConstants, outputIndex), hBlockSum)
        .updateConstant(offsetof(ScanPushConstants, blockSumIndex), 0xFFFFFFFF) // これ以上の上の階層は作らない
        .updateConstant(offsetof(ScanPushConstants, elementCount), NUM_WORKGROUPS);

    // --- Pass 3: オフセットの加算 ---
    auto& pass3 = graph.addPass("AddOffset", "shaders/scan_add.comp").bind(addBindGroup);
    pass3.dispatch(NUM_WORKGROUPS, 1, 1)
        .updateResource(offsetof(AddPushConstants, dataIndex), hOutput)
        .updateResource(offsetof(AddPushConstants, blockSumIndex), hBlockSum)
        .updateConstant(offsetof(AddPushConstants, elementCount), ELEMENT_COUNT);

    // コンパイル（ここで Pass1->Pass2->Pass3 間のバリアが自動生成される！）
    graph.compile();

    // 3. 実行
    rhi::vk::VulkanCommandList cmd(vkDevice);
    cmd.begin();
    graph.execute(cmd);
    cmd.end();
    cmd.submitAndWait();

    // 4. 結果の検証
    uint32_t* outData = static_cast<uint32_t*>(outputBuffer.map());
    
    std::cout << "--- Huge Parallel Prefix Sum Results (" << ELEMENT_COUNT << " elements) ---" << std::endl;
    std::cout << "First 10: ";
    for(int i = 0; i < 10; ++i) std::cout << outData[i] << " ";
    std::cout << std::endl;
    
    std::cout << "Last 3:   " 
              << outData[ELEMENT_COUNT - 3] << " " 
              << outData[ELEMENT_COUNT - 2] << " " 
              << outData[ELEMENT_COUNT - 1] << std::endl;
    
    outputBuffer.unmap();
}