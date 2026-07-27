#pragma once
#include "BVHTypes.hpp"
#include "../ModelImporter.hpp"

class IBVHBuilder {
public:
    virtual ~IBVHBuilder() = default;
    
    // CPUモデルデータを受け取り、再配列済みのBVHデータを構築して返す
    virtual CpuBvhData build(const CpuModelData& modelData) = 0;
};