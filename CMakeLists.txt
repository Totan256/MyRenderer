cmake_minimum_required(VERSION 3.18)

project(MyVulkanEngine LANGUAGES CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- vcpkg パッケージの検索 ---

find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)
find_package(unofficial-shaderc CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

add_subdirectory(core)
add_subdirectory(apps)
