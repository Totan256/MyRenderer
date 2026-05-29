# --- コアライブラリのソースを収集 ---
file(GLOB_RECURSE CORE_SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

add_library(RendererCore OBJECT
    ${CORE_SRC_FILES}
)

# --- インクルードディレクトリの設定 ---
# PUBLIC: このライブラリを使うアプリ側にも include パスを教える
# PRIVATE: このライブラリ内部だけで使う include パス
target_include_directories(RendererCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rhi
        ${CMAKE_CURRENT_SOURCE_DIR}/src/rhi/vulkan
)
if(STB_INCLUDE_DIR)
    target_include_directories(RendererCore PRIVATE ${STB_INCLUDE_DIR})
endif()

# --- ライブラリのリンク ---
# アプリ側(main.cpp)でも glm や Vulkan の型を使う場合は PUBLIC にします
target_link_libraries(RendererCore
    PUBLIC
        Vulkan::Vulkan
        glm::glm
        GPUOpen::VulkanMemoryAllocator
    PRIVATE
        spirv-cross-core
        spirv-cross-glsl
        assimp::assimp
        lodepng
        unofficial::shaderc::shaderc
)