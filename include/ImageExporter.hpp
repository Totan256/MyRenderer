#pragma once
#include <string>
#include <vector>

class ImageExporter {
public:
    // float(RGBA)の配列を受け取り、PNGとして保存する
    // width, height: 画像サイズ
    // pixels: バッファからmapして取得した生データ
    static void savePng(const std::string& filename, int width, int height, const void* pixels);
};