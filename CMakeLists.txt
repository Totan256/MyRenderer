cmake_minimum_required(VERSION 3.18)

project(MyOfflineRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- vcpkg パッケージの検索 ---

find_package(Vulkan REQUIRED)

find_package(VulkanMemoryAllocator CONFIG REQUIRED)

find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)

# SPIRV-Cross関連
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)

find_package(assimp CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)
find_package(unofficial-shaderc CONFIG REQUIRED)

# --- 実行ファイルの作成 ---
file(GLOB_RECURSE SRC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rhi/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rhi/vulkan/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)
add_executable(RendererApp
    main.cpp
    ${SRC_FILES}
)

if(STB_INCLUDE_DIR)
    target_include_directories(RendererApp PRIVATE ${STB_INCLUDE_DIR})
endif()

target_include_directories(RendererApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rhi
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rhi/vulkan
)

# --- ライブラリのリンク ---
target_link_libraries(RendererApp PRIVATE
    Vulkan::Vulkan
    
    GPUOpen::VulkanMemoryAllocator

    glm::glm
    
    spirv-cross-core
    spirv-cross-glsl

    assimp::assimp
    lodepng
    unofficial::shaderc::shaderc
)