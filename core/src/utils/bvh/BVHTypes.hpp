#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <iostream>
#include <cfloat>

struct AABB {
    glm::vec3 min = { FLT_MAX, FLT_MAX, FLT_MAX };
    glm::vec3 max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

    void grow(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    void grow(const AABB& b) {
        min = glm::min(min, b.min);
        max = glm::max(max, b.max);
    }
    float surfaceArea() const {
        glm::vec3 d = max - min;
        if (d.x < 0 || d.y < 0 || d.z < 0) return 0.0f;
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }
    glm::vec3 center() const {
        return (min + max) * 0.5f;
    }
};

// 32バイト (std430アライメント互換)
struct BVHNode {
    glm::vec3 aabbMin;
    uint32_t leftChildOrPrimitiveOffset;
    glm::vec3 aabbMax;
    uint32_t rightChildOrPrimitiveCount; 
    // rightChildOrPrimitiveCount が 0 の場合: 内部ノード (leftChild... は左の子ノードのインデックス)
    // 0 より大きい場合: 葉ノード (leftChild... はプリミティブの開始オフセット)
};

struct BVHBuildStatistics {
    std::string buildMethodName;
    double buildTimeMs = 0.0;
    uint32_t numNodes = 0;
    uint32_t numLeafNodes = 0;
    uint32_t maxDepth = 0;
    size_t memoryUsageBytes = 0;

    void printToConsole() const {
        std::cout << "--- BVH Build Statistics ---\n"
                  << " Method     : " << buildMethodName << "\n"
                  << " Time       : " << buildTimeMs << " ms\n"
                  << " Nodes      : " << numNodes << "\n"
                  << " Leaf Nodes : " << numLeafNodes << "\n"
                  << " Max Depth  : " << maxDepth << "\n"
                  << " Memory     : " << memoryUsageBytes / 1024.0 << " KB\n"
                  << "----------------------------\n";
    }
};

struct CpuBvhData {
    std::vector<BVHNode> nodes;
    std::vector<uint32_t> reorderedIndices; // 構築時に再配列されたインデックス
    BVHBuildStatistics stats;
};