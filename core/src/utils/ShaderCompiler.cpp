#include "ShaderCompiler.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <cstring>

// =========================================================
// カスタムインクルーダーの実装
// =========================================================
class FileIncluder : public shaderc::CompileOptions::IncluderInterface {
public:
    shaderc_include_result* GetInclude(const char* requested_source,
                                       shaderc_include_type type,
                                       const char* requesting_source,
                                       size_t include_depth) override {
        
        // 呼び出し元のシェーダーファイルパス（requesting_source）からディレクトリを抽出
        std::string req_source(requesting_source);
        std::string include_path = requested_source;
        size_t last_slash = req_source.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            // "shaders/raytrace.comp" なら "shaders/" をプレフィックスとして結合
            include_path = req_source.substr(0, last_slash + 1) + requested_source;
        }

        // インクルード対象のファイルを開く
        std::ifstream file(include_path);
        auto* result = new shaderc_include_result;

        if (!file.is_open()) {
            // ファイルが見つからなかった場合のエラー処理
            const char* error_msg = "Cannot find inclusion file";
            result->source_name = "";
            result->source_name_length = 0;
            result->content = error_msg;
            result->content_length = std::strlen(error_msg);
            result->user_data = nullptr;
            return result;
        }

        // ファイル内容を読み込む
        std::string* content = new std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string* source_name = new std::string(include_path);

        // shaderc_include_result にポインタを渡す
        result->source_name = source_name->c_str();
        result->source_name_length = source_name->size();
        result->content = content->c_str();
        result->content_length = content->size();
        
        // メモリ解放のためにポインタペア保持
        result->user_data = new std::pair<std::string*, std::string*>(source_name, content);

        return result;
    }

    void ReleaseInclude(shaderc_include_result* data) override {
        if (data) {
            if (data->user_data) {
                auto* pair = static_cast<std::pair<std::string*, std::string*>*>(data->user_data);
                delete pair->first;
                delete pair->second;
                delete pair;
            }
            delete data;
        }
    }
};

// =========================================================
// ShaderCompiler の実装
// =========================================================
std::vector<uint32_t> ShaderCompiler::readFile(const std::string& filename) {
    // バイナリモードで末尾から開くサイズ取得
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "[Error] Could not open shader file: " << filename << std::endl;
        throw std::runtime_error("failed to open file: " + filename);
    }

    size_t fileSize = (size_t)file.tellg();
    // SPIR-Vバイナリは4バイト単位でない場合はエラー
    if (fileSize % 4 != 0) {
        throw std::runtime_error("SPIR-V file size must be a multiple of 4: " + filename);
    }
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    return buffer;
}

std::vector<uint32_t> ShaderCompiler::compileGLSLToSPIRV(const std::string& shaderPath, shaderc_shader_kind kind) {
    // 1. ソースコードをテキストとして読み込む
    std::ifstream file(shaderPath);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open shader file: " + shaderPath);
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // 2. shaderc コンパイラの設定
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    
    // デバッグのしやすさのために最適化レベルやデバッグ情報を設定可能
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetGenerateDebugInfo();

    // インクルードリゾルバをセット
    options.SetIncluder(std::make_unique<FileIncluder>());

    // 3. コンパイル実行
    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        source, 
        kind, 
        shaderPath.c_str(), 
        options
    );

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        std::cerr << "Shader Compilation Error: " << result.GetErrorMessage() << std::endl;
        throw std::runtime_error("failed to compile shader: " + shaderPath);
    }

    return { result.cbegin(), result.cend() };
}