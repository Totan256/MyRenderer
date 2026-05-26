#include "ModelImporter.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <filesystem>

CpuModelData ModelImporter::loadFromFile(const std::string& filepath) {
    Assimp::Importer importer;
    // ポリゴンを三角形化し、UVを反転（Vulkan向け）
    const aiScene* scene = importer.ReadFile(filepath, 
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_JoinIdenticalVertices);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        throw std::runtime_error("Assimp Error: " + std::string(importer.GetErrorString()));
    }

    CpuModelData model;
    std::vector<VertexPosition> positions;
    std::vector<VertexAttributes> attributes;
    std::vector<uint32_t> indices;

    // TODO: マテリアルのテクスチャロード処理（ここでは省略、後で拡張可能）

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        
        CpuSubMesh subMesh{};
        subMesh.indexBase = indices.size();
        subMesh.materialIndex = mesh->mMaterialIndex;

        uint32_t vertexOffset = positions.size();

        // 頂点データの抽出（ストリーム分離）
        for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
            VertexPosition pos;
            pos.position = { mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z };
            positions.push_back(pos);

            VertexAttributes attr{};
            if (mesh->HasNormals()) {
                attr.normal = { mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z };
            }
            if (mesh->mTextureCoords[0]) {
                attr.uv = { mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y };
            }
            attributes.push_back(attr);
        }

        // インデックスデータの抽出
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                indices.push_back(vertexOffset + face.mIndices[k]);
            }
        }
        
        subMesh.indexCount = indices.size() - subMesh.indexBase;
        model.subMeshes.push_back(subMesh);
    }
    model.positions = std::move(positions);
    model.attributes = std::move(attributes);
    model.indices = std::move(indices);
    return model;
}