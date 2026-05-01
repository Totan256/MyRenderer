#include "Renderer.hpp"
#include "vulkan/VulkanRenderGraph.hpp"
#include "vulkan/VulkanDevice.hpp"
#include <iostream>
#include <cstddef>
#include <vector>
#include <chrono>
#include <random>

#include <fstream>
#include <memory>

struct ScanPushConstants {
    uint32_t inputIndex;
    uint32_t outputIndex;
    uint32_t blockSumIndex;
    uint32_t elementCount;
};
struct AddPushConstants {
    uint32_t dataIndex;
    uint32_t blockSumIndex;
    uint32_t errorFlagIndex;
    uint32_t elementCount;
};

void Renderer::runFileParenthesesCheck(const std::string& filepath) {
    auto& vkDevice = static_cast<rhi::vk::VulkanDevice&>(m_device);

    std::ifstream file(filepath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        return;
    }
    size_t fileSize = file.tellg();
    file.seekg(0);
    std::string content(fileSize, '\0');
    file.read(&content[0], fileSize);

    std::cout << "file size :" << fileSize << std::endl;

    // ディスパッチサイズ分で区切って必要なパスの数を計算
    uint32_t baseElementCount = ((fileSize + 1023) / 1024) * 1024;
    if (baseElementCount == 0) baseElementCount = 1024;
    std::vector<int32_t> mappedData(baseElementCount, 0);
    for (size_t i = 0; i < fileSize; ++i) {
        if (content[i] == '(') mappedData[i] = 1;
        else if (content[i] == ')') mappedData[i] = -1;
    }
    // ツリー構造の計算とバッファ確保
    std::vector<uint32_t> levelSizes;
    uint32_t curr = baseElementCount;
    levelSizes.push_back(curr);
    while (curr > 1) {
        curr = (curr + 1023) / 1024;
        levelSizes.push_back(curr);
    }

    std::vector<std::unique_ptr<rhi::vk::VulkanBuffer>> levelBuffers;
    for (uint32_t size : levelSizes) {
        levelBuffers.push_back(std::make_unique<rhi::vk::VulkanBuffer>(
            vkDevice, vkDevice.getAllocator(), size * sizeof(int32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST));
    }

    // 初期データをlevel0に転送
    void* mappedMem = levelBuffers[0]->map();
    std::memcpy(mappedMem, mappedData.data(), baseElementCount * sizeof(int32_t));
    levelBuffers[0]->unmap();

    // 最小値格納バッファ
    rhi::vk::VulkanBuffer errorFlagBuffer(
        vkDevice, vkDevice.getAllocator(), sizeof(int32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
    int32_t* flagMem = static_cast<int32_t*>(errorFlagBuffer.map());
    *flagMem = 1;
    errorFlagBuffer.unmap();


    rhi::vk::VulkanRenderGraph graph(vkDevice);
    
    std::vector<rhi::ResourceHandle> hLevels;
    for (auto& buf : levelBuffers) {
        hLevels.push_back(graph.importResource(buf.get()));
    }
    auto hErrorFlag = graph.importResource(&errorFlagBuffer);

    auto& scanBindGroup = graph.createBindGroup({
        {offsetof(ScanPushConstants, inputIndex), rhi::ResourceUsage::StorageRead},
        {offsetof(ScanPushConstants, outputIndex), rhi::ResourceUsage::StorageWrite},
        {offsetof(ScanPushConstants, blockSumIndex), rhi::ResourceUsage::StorageWrite}
    });
    auto& addBindGroup = graph.createBindGroup({
        {offsetof(AddPushConstants, dataIndex), rhi::ResourceUsage::StorageWrite},
        {offsetof(AddPushConstants, errorFlagIndex), rhi::ResourceUsage::StorageWrite},
        {offsetof(AddPushConstants, blockSumIndex), rhi::ResourceUsage::StorageRead}
    });

    int numLevels = levelSizes.size();

    for (int i = 0; i < numLevels - 1; ++i) {
        uint32_t size = levelSizes[i];
        uint32_t numWorkgroups = levelSizes[i + 1];

        // 最後のスキャン階層だけblockSum不要
        uint32_t nextBlockHandle = (i == numLevels - 2) ? 0xFFFFFFFF : hLevels[i + 1];

        graph.addPass("ScanLevel_" + std::to_string(i), "shaders/scan.comp").bind(scanBindGroup)
            .dispatch(numWorkgroups, 1, 1)
            .updateResource(offsetof(ScanPushConstants, inputIndex), hLevels[i])
            .updateResource(offsetof(ScanPushConstants, outputIndex), hLevels[i])
            .updateResource(offsetof(ScanPushConstants, blockSumIndex), nextBlockHandle)
            .updateConstant(offsetof(ScanPushConstants, elementCount), size);
    }
    
    for (int i = numLevels - 3; i >= 0; --i) {
        uint32_t size = levelSizes[i];
        uint32_t numWorkgroups = levelSizes[i + 1];

        graph.addPass("AddLevel_" + std::to_string(i), "shaders/scan_add.comp").bind(addBindGroup)
            .dispatch(numWorkgroups, 1, 1)
            .updateResource(offsetof(AddPushConstants, dataIndex), hLevels[i])
            .updateResource(offsetof(AddPushConstants, blockSumIndex), hLevels[i + 1])
            .updateResource(offsetof(AddPushConstants, errorFlagIndex), hErrorFlag)
            .updateConstant(offsetof(AddPushConstants, elementCount), size);
    }

    graph.compile();

    rhi::vk::VulkanCommandList cmd(vkDevice);
    cmd.begin();
    auto gpu_start = std::chrono::high_resolution_clock::now();
    graph.execute(cmd);
    cmd.end();
    cmd.submitAndWait();
    auto gpu_end = std::chrono::high_resolution_clock::now();
    double gpu_time = std::chrono::duration<double, std::milli>(gpu_end - gpu_start).count();

    int32_t* outData = static_cast<int32_t*>(levelBuffers[0]->map());
    int32_t* finalFlag = static_cast<int32_t*>(errorFlagBuffer.map());
    
    // 全要素が 0 以上かつ、最後の要素が 0 なら対応が取れている
    bool isDropBelowZero = (*finalFlag == 0);
    int finalSum = outData[fileSize - 1]; // 配列の最後尾だけ確認

    if (!isDropBelowZero && finalSum == 0) {
        std::cout << "Parentheses Check: VALID" << std::endl;
    } else {
        std::cout << "Parentheses Check: INVALID" << std::endl;
    }

    std::cout << " (Final Sum: " << outData[fileSize - 1] << ")" << std::endl;
    std::cout << "GPU Time  : " << gpu_time << " ms" << std::endl;

    levelBuffers[0]->unmap();
    errorFlagBuffer.unmap();
}