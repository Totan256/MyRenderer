// main.cpp
#include <iostream>
#include "rhi/RHI.hpp"
#include "core/Window.hpp"
#include "Renderer.hpp"

int main() {
    std::cout << "Initializing renderer..." << std::endl;
    try {
        core::Window window("Sandbox", 800, 600);

        std::unique_ptr<rhi::Device> device = rhi::createDevice(rhi::GraphicsAPI::Vulkan);
        device->initialize(window.getVulkanProvider()); 

        Renderer renderer(*device, 800, 600);

        std::cout << "Rendering..." << std::endl;
        renderer.render(1.5f);

        while (!window.shouldClose()) {
            window.pollEvents();
            
            // ※スワップチェーン導入後、ここに renderer.render() を移動させ、
            // Renderer.cpp 側のPNG保存処理を削除します。
        }

        std::cout << "Success!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}