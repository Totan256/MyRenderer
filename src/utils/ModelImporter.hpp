#pragma once
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "rhi/Device.hpp"
#include "rhi/Resource.hpp"
#include "vulkan/VulkanUploadManager.hpp"

// ストリーム分離した頂点データ
struct VertexPosition {
    glm::vec4 position;
};

struct VertexAttributes {
    glm::vec4 normal;
    glm::vec2 uv;
    glm::vec2 padding;
    // glm::vec4 tangent;
};

struct CpuSubMesh {
    uint32_t indexBase;
    uint32_t indexCount;
    uint32_t materialIndex;
};

// GPUに依存しない、純粋なCPU上のデータコンテナ
struct CpuModelData {
    std::vector<VertexPosition> positions;
    std::vector<VertexAttributes> attributes;
    std::vector<uint32_t> indices;
    std::vector<CpuSubMesh> subMeshes;
};


class ModelImporter {
public:
    static CpuModelData loadFromFile(const std::string& filepath);
};