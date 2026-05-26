#include "ImageImporter.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

ImageData ImageImporter::loadFromFile(const std::string& filepath) {
    ImageData data;
    int texWidth, texHeight, texChannels;
    
    if (stbi_is_hdr(filepath.c_str())) {
        data.pixels = stbi_loadf(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        data.format = rhi::Format::R32G32B32A32_Sfloat;
        data.imageSize = texWidth * texHeight * 4 * sizeof(float);
    } else if (stbi_is_16_bit(filepath.c_str())) {
        data.pixels = stbi_load_16(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        data.format = rhi::Format::R16G16B16A16_Unorm;
        data.imageSize = texWidth * texHeight * 4 * sizeof(uint16_t);
    } else {
        data.pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        data.format = rhi::Format::R8G8B8A8_Unorm;
        data.imageSize = texWidth * texHeight * 4 * sizeof(stbi_uc);
    }

    if (!data.pixels) {
        throw std::runtime_error("Failed to load image: " + filepath);
    }

    data.width = static_cast<uint32_t>(texWidth);
    data.height = static_cast<uint32_t>(texHeight);
    data.channels = 4;
    data.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    return data;
}

void ImageImporter::freeData(ImageData& data) {
    if (data.pixels) {
        stbi_image_free(data.pixels);
        data.pixels = nullptr;
    }
}
