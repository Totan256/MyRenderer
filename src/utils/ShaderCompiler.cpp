#include "ShaderCompiler.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>



std::vector<uint32_t> ShaderCompiler::readFile(const std::string& filename) {
    // バイナリモードで末尾から開く（サイズ取得のため）
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
