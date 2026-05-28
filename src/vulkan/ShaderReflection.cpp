#include "ShaderReflection.hpp"
#include <spirv_cross/spirv_cross.hpp>
#include <iostream>

namespace rhi::vk {
    ShaderReflectionData ShaderReflection::reflect(const std::vector<uint32_t>& spirvCode) {
        ShaderReflectionData data;
        if (spirvCode.empty() || spirvCode[0] != 0x07230203) return data;

        spirv_cross::Compiler compiler(spirvCode.data(), spirvCode.size());
        auto executionModel = compiler.get_execution_model();

        if (executionModel == spv::ExecutionModelGLCompute) {
            data.passType = rhi::PassType::Compute;
            data.queueType = rhi::QueueType::Compute;
            const auto& entry = compiler.get_entry_point("main", executionModel);
            data.localSizeX = entry.workgroup_size.x;
            data.localSizeY = entry.workgroup_size.y;
            data.localSizeZ = entry.workgroup_size.z;
        } else {
            // Graphics の場合
            data.passType = rhi::PassType::Graphics;
            data.queueType = rhi::QueueType::Graphics;
        }

        spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        for (const auto& resource : resources.push_constant_buffers) {
            const auto& type = compiler.get_type(resource.base_type_id);
            for (uint32_t i = 0; i < type.member_types.size(); ++i) {
                std::string memberName = compiler.get_member_name(resource.base_type_id, i);
                uint32_t offset = compiler.type_struct_member_offset(type, i);
                if (!memberName.empty()) {
                    data.pushConstantOffsets[hashString(memberName.c_str())] = offset;
                }
            }
        }
        for (const auto& resource : resources.stage_outputs) {
            uint32_t location = compiler.get_decoration(resource.id, spv::DecorationLocation);
            data.outputLocations[hashString(resource.name.c_str())] = location;
        }
        return data;
    }
}