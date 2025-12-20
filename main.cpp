#include "GraphicsDevice.hpp"
#include "GpuBuffer.hpp"
#include "ComputePipeline.hpp"
#include "CommandList.hpp"
#include "ImageExporter.hpp"
#include "DescriptorManager.hpp"
#include <iostream>
#include <cmath>
#include <glm/glm.hpp>
#include "Renderer.hpp"
#include "GPUImage.hpp"


int main() {
    try {
        GraphicsDevice device;
        device.initialize();

        // レンダラの作成
        Renderer renderer(device, 800, 600);

        // 実行と保存
        std::cout << "Rendering..." << std::endl;
        renderer.render(1.5f); // 1.5秒時点の絵を描画
        renderer.saveResult("output_refactored.png");

        std::cout << "Success!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}