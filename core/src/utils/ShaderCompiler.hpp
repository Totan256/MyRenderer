#pragma once

#include <vector>
#include <string>
#include <shaderc/shaderc.hpp>

class ShaderCompiler {
public:
    // ファイルからバイナリとして読み込む (SPIR-V用)
    static std::vector<uint32_t> readFile(const std::string& filename);
    
    // GLSLからSPIR-Vへコンパイル
    static std::vector<uint32_t> compileGLSLToSPIRV(const std::string& shaderPath, shaderc_shader_kind kind);
};
