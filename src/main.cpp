#include "vulan.hpp"
/*
 * vcpkg と Vulkan SDK の連携を確認するためのテストコードです。
 * これがコンパイル＆リンクできれば、環境設定は成功しています。
 */

// --- 1. 実装の定義 (stb と VMA はヘッダオンリーライブラリとして動作するため) ---
// これらは main.cpp のような「1つの .cpp ファイル」でのみ定義します。
#define STB_IMAGE_IMPLEMENTATION
#define VMA_IMPLEMENTATION

// --- 2. 必要なヘッダのインクルード ---

// 標準ライブラリ
#include <iostream>

// Vulkan SDK (vulkan)
#include <vulkan/vulkan.h>

// Vulkan Memory Allocator (vulkan-memory-allocator)
// (VMA_IMPLEMENTATION より *後* に include する)
#include "vk_mem_alloc.h"

// GLM (glm)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// STB (stb)
// (STB_IMAGE_IMPLEMENTATION より *後* に include する)
#include "stb_image.h"

// SPIRV-Cross (spirv-cross)
#include <spirv_cross/spirv_glsl.hpp>

// Assimp (assimp)
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// LodePNG (lodepng)
#include "lodepng.h"


// --- 3. main 関数 (リンクのテスト) ---
int main() {
    std::cout << "--- ライブラリ連携テスト開始 ---" << std::endl;

    // 1. Vulkan (型が使えるか)
    VkInstance instance = VK_NULL_HANDLE;
    std::cout << "Vulkan: ヘッダ OK." << std::endl;

    // 2. GLM (簡単な計算ができるか)
    glm::vec4 pos(1.0f, 0.0f, 0.0f, 1.0f);
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec4 transformed = model * pos;
    std::cout << "GLM: 計算 OK (vec4 x mat4)." << std::endl;

    // 3. Vulkan Memory Allocator (VMA) (型が使えるか)
    VmaAllocator allocator = VK_NULL_HANDLE;
    std::cout << "VMA: ヘッダ OK." << std::endl;

    // 4. SPIRV-Cross (簡単なインスタンスが作れるか)
    // (ダミーデータで初期化)
    uint32_t dummy_spirv[] = { 0x07230203 }; // 最小限のSPIR-Vマジックナンバー
    spirv_cross::CompilerGLSL glsl_compiler(dummy_spirv, 1);
    std::cout << "SPIRV-Cross: インスタンス OK." << std::endl;

    // 5. Assimp (インポーターが作れるか)
    Assimp::Importer importer;
    std::cout << "Assimp: インスタンス OK." << std::endl;

    // 6. LodePNG (バージョンが取得できるか)
    std::cout << "LodePNG: バージョン " << lodepng_get_version_string() << std::endl;

    // 7. STB (stb_image) (バージョンが定義されているか)
    std::cout << "stb_image: バージョン " << STBI_VERSION << std::endl;


    std::cout << "\n[成功] すべてのライブラリのインクルードとリンクが確認できました。" << std::endl;

    return 0;
}