#include <iostream>

// Vulkanのインクルード
#include <vulkan/vulkan.h>

// VMAのインクルード (実装も定義)
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

// GLMのインクルード
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

// LodePNGのインクルード
#include <lodepng.h>

int main() {
    // 1. GLMのテスト
    glm::mat4 matrix;
    glm::vec4 vec;
    auto test = matrix * vec;
    std::cout << "GLM Include Success!" << std::endl;

    // 2. LodePNGのテスト
    const char* filename = "test.png";
    // (中身はないので呼び出すだけ)
    std::cout << "LodePNG Include Success!" << std::endl;

    // 3. Vulkanのバージョン確認テスト
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::cout << "Vulkan extensions supported: " << extensionCount << std::endl;

    std::cout << "All libraries linked successfully!" << std::endl;

    return 0;
}