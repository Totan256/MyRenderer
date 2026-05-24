#include "ShaderReflection.hpp"
#include <spirv_cross/spirv_cross.hpp>
#include <iostream>

namespace rhi::vk {
    ShaderReflectionData ShaderReflection::reflect(const std::vector<uint32_t>& spirvCode) {
        try{
            ShaderReflectionData data;
            std::cout << "Starting shader reflection..." << std::endl; //debug
            if (spirvCode.empty()) {
                std::cerr << "Error: spirvCode is empty." << std::endl;
                return {};
            }
            if (spirvCode[0] != 0x07230203) {
                std::cerr << "Error: Invalid SPIR-V magic number: " << std::hex << spirvCode[0] << std::endl;
                return {};
            }
            std::cout << "SPIR-V Code Size: " << spirvCode.size() << " words (" 
               << spirvCode.size() * 4 << " bytes)" << std::endl;
            spirv_cross::Compiler compiler(spirvCode.data(), spirvCode.size());

            // Execution Model (Computeか判定)
            std::cout << "Reflecting shader..." << std::endl; //debug
            auto executionModel = compiler.get_execution_model();
            std::cout << "Execution Model: " << executionModel << std::endl; //debug
            if (executionModel == spv::ExecutionModelGLCompute) {
                data.passType = rhi::PassType::Compute;
                data.queueType = rhi::QueueType::Compute;
                
                const spirv_cross::SPIREntryPoint& entry = compiler.get_entry_point("main", executionModel);
                data.localSizeX = entry.workgroup_size.x;
                data.localSizeY = entry.workgroup_size.y;
                data.localSizeZ = entry.workgroup_size.z;
                std::cout << "Local Size: (" << data.localSizeX << ", " << data.localSizeY << ", " << data.localSizeZ << ")" << std::endl; //debug
            }

            // Push Constants の変数名とオフセットを取得
            spirv_cross::ShaderResources resources = compiler.get_shader_resources();
            for (const auto& resource : resources.push_constant_buffers) {
                const auto& type = compiler.get_type(resource.base_type_id);
                for (uint32_t i = 0; i < type.member_types.size(); ++i) {
                    std::string memberName = compiler.get_member_name(resource.base_type_id, i);
                    uint32_t offset = compiler.type_struct_member_offset(type, i);
                    
                    std::cout << "  PushConstant Member: " << memberName << " (Offset: " << offset << ")" << std::endl;   // デバッグ用: 取得できた名前を出力してみる
                    
                    // ハッシュ化してマップに登録
                    if (!memberName.empty()) {
                        data.pushConstantOffsets[hashString(memberName.c_str())] = offset;
                    }
                }
            }
            return data;
        }catch (const std::runtime_error& e) {
            std::cerr << "SPIRV-Cross Runtime Error: " << e.what() << std::endl;
            return {};
        }
        catch (const std::exception& e) {
            std::cerr << "Standard Exception: " << e.what() << std::endl;
            return {};
        }
        catch (...) {
            std::cerr << "Unknown Exception caught!" << std::endl;
            return {};
        }
    }
}