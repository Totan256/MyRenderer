#include "GraphicsDevice.hpp"
#include "GpuBuffer.hpp"
#include "ComputePipeline.hpp"
#include "CommandList.hpp"
#include "ImageExporter.hpp" // 追加
#include "DescriptorManager.hpp"
#include <iostream>
#include <cmath>
#include <glm/glm.hpp>
#include "Renderer.hpp"
struct SceneData {
    glm::vec4 resolution; // width, height, 0, 0
    glm::vec4 params;     // time, frame, 0, 0
    glm::vec4 cameraPos;
};

int main() {
    try {
        // --- 設定 ---
        const int WIDTH = 800;
        const int HEIGHT = 600;
        const int WORKGROUP_SIZE = 16; // シェーダーのlocal_sizeに合わせて調整

        GraphicsDevice device;
        device.initialize();

        // 1. 画像用バッファ (StorageBuffer)
        VkDeviceSize imageBufferSize = sizeof(float) * 4 * WIDTH * HEIGHT;
        GpuBuffer imageBuffer(device.getAllocator(), imageBufferSize, 
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST);

        // 2. シーン情報用バッファ (UniformBuffer) 【追加】
        VkDeviceSize sceneBufferSize = sizeof(SceneData);
        GpuBuffer sceneBuffer(device.getAllocator(), sceneBufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, // UNIFORMビットが重要
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST);

        // データの書き込み
        SceneData scene{};
        scene.resolution = glm::vec4(WIDTH, HEIGHT, 0, 0);
        scene.params = glm::vec4(1.5f, 0, 0, 0); // Time = 1.5秒としてテスト
        sceneBuffer.writeData(&scene, sizeof(SceneData));

        // 3. パイプライン作成 (新しい render.spv を読む)
        ComputePipeline pipeline(device, "test.spv");

        // 4. ディスクリプタセットの作成（Managerを使って簡潔に！）
        DescriptorManager descManager(device);
        
        VkDescriptorSet descriptorSet = descManager.createBuilder(pipeline.getDescriptorSetLayout())
            .bindBuffer(0, imageBuffer.getBuffer(), imageBufferSize, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .bindBuffer(1, sceneBuffer.getBuffer(), sceneBufferSize, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            .build();

        // 5. コマンド実行
        CommandList cmd(device);
        cmd.begin();
        cmd.bindPipeline(pipeline);
        cmd.bindDescriptorSet(pipeline, descriptorSet);
        cmd.dispatch((uint32_t)ceil(WIDTH/16.0), (uint32_t)ceil(HEIGHT/16.0), 1);
        cmd.end();
        cmd.submitAndWait();

        // 6. 保存
        void* data = imageBuffer.map();
        ImageExporter::savePng("output_animated.png", WIDTH, HEIGHT, data);
        imageBuffer.unmap();

    } catch (const std::exception& e) {
        std::cerr << "Critical Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}