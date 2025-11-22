#include "GraphicsDevice.hpp"
#include "GpuBuffer.hpp"
#include "ComputePipeline.hpp"
#include "CommandList.hpp"
#include "ImageExporter.hpp" // 追加
#include <iostream>
#include <cmath>

int main() {
    try {
        // --- 設定 ---
        const int WIDTH = 800;
        const int HEIGHT = 600;
        const int WORKGROUP_SIZE = 16; // シェーダーのlocal_sizeに合わせて調整

        GraphicsDevice device;
        device.initialize();

        // 1. バッファサイズの計算
        // 1ピクセルあたり float 4つ (RGBA)
        VkDeviceSize bufferSize = sizeof(float) * 4 * WIDTH * HEIGHT;
        
        std::cout << "Creating Buffer for " << WIDTH << "x" << HEIGHT << " image..." << std::endl;
        
        GpuBuffer buffer(
            device.getAllocator(),
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST
        );

        // 2. パイプライン作成 (render.spv を読み込む)
        ComputePipeline pipeline(device, "test.spv");

        // 3. ディスクリプタセットまわり (前回と同じ)
        // ※リファクタリングの余地ありですが、まずは動かすことを優先
        VkDescriptorPool descriptorPool;
        VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
        VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &poolSize };
        vkCreateDescriptorPool(device.getDevice(), &poolInfo, nullptr, &descriptorPool);

        VkDescriptorSet descriptorSet;
        VkDescriptorSetLayout layout = pipeline.getDescriptorSetLayout();
        VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptorPool, 1, &layout };
        vkAllocateDescriptorSets(device.getDevice(), &allocInfo, &descriptorSet);

        VkDescriptorBufferInfo bufferInfo = { buffer.getBuffer(), 0, bufferSize };
        VkWriteDescriptorSet descriptorWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bufferInfo, nullptr };
        vkUpdateDescriptorSets(device.getDevice(), 1, &descriptorWrite, 0, nullptr);

        // 4. コマンド実行
        CommandList cmd(device);
        cmd.begin();
        cmd.bindPipeline(pipeline);
        cmd.bindDescriptorSet(pipeline, descriptorSet);

        // グループ数の計算: (画像サイズ / グループサイズ) 切り上げ
        uint32_t groupX = (uint32_t)std::ceil(WIDTH / (float)WORKGROUP_SIZE);
        uint32_t groupY = (uint32_t)std::ceil(HEIGHT / (float)WORKGROUP_SIZE);
        
        std::cout << "Dispatching: " << groupX << " x " << groupY << " groups" << std::endl;
        cmd.dispatch(groupX, groupY, 1);
        
        cmd.end();
        cmd.submitAndWait();

        // 5. 画像保存
        std::cout << "Reading back and saving..." << std::endl;
        void* data = buffer.map();
        ImageExporter::savePng("output.png", WIDTH, HEIGHT, data);
        buffer.unmap();

        // お片付け
        vkDestroyDescriptorPool(device.getDevice(), descriptorPool, nullptr);

    } catch (const std::exception& e) {
        std::cerr << "Critical Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}