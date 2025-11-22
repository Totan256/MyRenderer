#include "ImageExporter.hpp"
#include <iostream>
#include <vector>
#include <algorithm> // std::clamp用

// stb_image_writeの実装をここで定義
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// ピクセル構造体（シェーダーと合わせる）
struct Pixel {
    float r, g, b, a;
};

void ImageExporter::savePng(const std::string& filename, int width, int height, const void* rawData) {
    const Pixel* pixels = static_cast<const Pixel*>(rawData);
    
    // 1. float(0.0-1.0) から byte(0-255) へ変換
    // 出力用バッファ: RGBA (4チャンネル) * 幅 * 高さ
    std::vector<unsigned char> imageData(width * height * 4);

    for (int i = 0; i < width * height; ++i) {
        // シェーダーからの出力は範囲外の値を含む可能性があるので clamp で 0.0-1.0 に収める
        float r = std::clamp(pixels[i].r, 0.0f, 1.0f);
        float g = std::clamp(pixels[i].g, 0.0f, 1.0f);
        float b = std::clamp(pixels[i].b, 0.0f, 1.0f);
        float a = std::clamp(pixels[i].a, 0.0f, 1.0f);

        // 0-255 に変換
        imageData[4 * i + 0] = static_cast<unsigned char>(r * 255.99f);
        imageData[4 * i + 1] = static_cast<unsigned char>(g * 255.99f);
        imageData[4 * i + 2] = static_cast<unsigned char>(b * 255.99f);
        imageData[4 * i + 3] = static_cast<unsigned char>(a * 255.99f);
    }

    // 2. PNGとして書き出し (stb_image_write)
    // stride_in_bytes: 1行あたりのバイト数 = width * 4
    int result = stbi_write_png(filename.c_str(), width, height, 4, imageData.data(), width * 4);

    if (result) {
        std::cout << "Saved image to: " << filename << std::endl;
    } else {
        std::cerr << "Failed to save image: " << filename << std::endl;
    }
}