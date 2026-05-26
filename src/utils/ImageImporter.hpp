#pragma once
#include <string>
#include <cstdint>
#include <cstddef>
#include "rhi/RHIcommon.hpp"

struct ImageData {
    void* pixels = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    rhi::Format format = rhi::Format::R8G8B8A8_Unorm;
    uint32_t mipLevels = 1;
    size_t imageSize = 0;
};

class ImageImporter {
public:
    static ImageData loadFromFile(const std::string& filepath);
    static void freeData(ImageData& data);
};
