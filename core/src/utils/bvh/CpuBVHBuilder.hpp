#pragma once
#include "BVHBuilder.hpp"

enum class BVHSplitMethod { SpatialMedian, SAH, BinnedSAH };

// --- プロファイリング用コンフィグ ---
struct BVHConfig {
    uint32_t maxDepth = 32;
    uint32_t minPrimitivesPerLeaf = 2;
};

class CpuBVHBuilder : public IBVHBuilder {
public:
    CpuBVHBuilder(BVHSplitMethod method = BVHSplitMethod::SpatialMedian, BVHConfig config = {});
    CpuBvhData build(const CpuModelData& modelData) override;

private:
    BVHSplitMethod m_method;
    BVHConfig m_config;
    
    struct PrimitiveInfo {
        uint32_t indexOffset; 
        AABB bounds;
        glm::vec3 center;
    };

    std::vector<PrimitiveInfo> m_primitives;
    std::vector<uint32_t> m_originalIndices;
    std::vector<BVHNode> m_nodes;
    uint32_t m_maxDepth;
    uint32_t m_leafNodes;

    void updateNodeBounds(uint32_t nodeIdx);
    void subdivide(uint32_t nodeIdx, uint32_t depth);
};