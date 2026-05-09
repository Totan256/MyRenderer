// main.cpp
#include <iostream>
#include "rhi/RHI.hpp"
#include "Renderer.hpp"

int main() {
    try {
        std::unique_ptr<rhi::Device> device = rhi::createDevice(rhi::GraphicsAPI::Vulkan);

        Renderer renderer(*device, 800, 600);

        std::cout << "Rendering..." << std::endl;
        renderer.render(1.5f);

        std::cout << "Success!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}