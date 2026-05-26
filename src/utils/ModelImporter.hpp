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
    glm::vec3 position;
};

struct VertexAttributes {
    glm::vec3 normal;
    glm::vec2 uv;
    // glm::vec4 tangent;
};

struct SubMesh {
    uint32_t indexBase;
    uint32_t indexCount;
    uint32_t materialIndex;
};

struct Model {
    std::unique_ptr<rhi::Buffer> positionBuffer;
    std::unique_ptr<rhi::Buffer> attributeBuffer;
    std::unique_ptr<rhi::Buffer> indexBuffer;
    
    std::vector<SubMesh> subMeshes;
    // ロードされたテクスチャのBindless Index
    std::vector<uint32_t> albedoTextureIndices;
};

class ModelImporter {
public:
    static std::unique_ptr<Model> loadFromFile(const std::string& filepath, rhi::Device& device);
};