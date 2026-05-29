#pragma once
#include <cstdint>
#include <string>
#define RHI_USE_VULKAN
//#define RHI_USE_DX12


const uint32_t MAX_PUSH_CONSTANT_SIZE = 128; // Vulkan最小保証値
const uint32_t MAX_FRAMES_IN_FLIGHT = 2; // ダブルバッファリング
const std::string SHADER_CACHE_FILE_NAME = "shader_cache.bin";