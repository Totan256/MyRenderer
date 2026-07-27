#include "CpuBVHBuilder.hpp"
#include <chrono>
#include <algorithm>

CpuBVHBuilder::CpuBVHBuilder(BVHSplitMethod method, BVHConfig config) 
    : m_method(method), m_config(config) {}

CpuBvhData CpuBVHBuilder::build(const CpuModelData& modelData) {
    auto startTime = std::chrono::high_resolution_clock::now();

    m_originalIndices = modelData.indices;
    uint32_t numPrimitives = m_originalIndices.size() / 3;
    m_primitives.resize(numPrimitives);

    for (uint32_t i = 0; i < numPrimitives; ++i) {
        m_primitives[i].indexOffset = i * 3;
        AABB bounds;
        for (int j = 0; j < 3; ++j) {
            uint32_t vertexIdx = m_originalIndices[i * 3 + j];
            bounds.grow(glm::vec3(modelData.positions[vertexIdx].position));
        }
        m_primitives[i].bounds = bounds;
        m_primitives[i].center = bounds.center();
    }

    m_nodes.clear();
    m_nodes.reserve(numPrimitives * 2);
    m_maxDepth = 0;
    m_leafNodes = 0;

    BVHNode rootNode;
    rootNode.leftChildOrPrimitiveOffset = 0;
    rootNode.rightChildOrPrimitiveCount = numPrimitives;
    m_nodes.push_back(rootNode);

    updateNodeBounds(0);
    subdivide(0, 1);

    CpuBvhData result;
    result.nodes = std::move(m_nodes);
    
    result.reorderedIndices.reserve(m_originalIndices.size());
    for (const auto& prim : m_primitives) {
        result.reorderedIndices.push_back(m_originalIndices[prim.indexOffset]);
        result.reorderedIndices.push_back(m_originalIndices[prim.indexOffset + 1]);
        result.reorderedIndices.push_back(m_originalIndices[prim.indexOffset + 2]);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double buildTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    result.stats.buildMethodName = "Spatial Median";
    result.stats.buildTimeMs = buildTime;
    result.stats.numNodes = result.nodes.size();
    result.stats.numLeafNodes = m_leafNodes;
    result.stats.maxDepth = m_maxDepth;
    result.stats.memoryUsageBytes = result.nodes.size() * sizeof(BVHNode);

    return result;
}

void CpuBVHBuilder::updateNodeBounds(uint32_t nodeIdx) {
    BVHNode& node = m_nodes[nodeIdx];
    AABB bounds;
    for (uint32_t i = 0; i < node.rightChildOrPrimitiveCount; ++i) {
        bounds.grow(m_primitives[node.leftChildOrPrimitiveOffset + i].bounds);
    }
    node.aabbMin = bounds.min;
    node.aabbMax = bounds.max;
}

void CpuBVHBuilder::subdivide(uint32_t nodeIdx, uint32_t depth) {
    m_maxDepth = std::max(m_maxDepth, depth);
    BVHNode& node = m_nodes[nodeIdx];

    // コンフィグを利用した終了条件
    if (node.rightChildOrPrimitiveCount <= m_config.minPrimitivesPerLeaf || depth >= m_config.maxDepth) {
        m_leafNodes++;
        return;
    }

    AABB centroidBounds;
    for (uint32_t i = 0; i < node.rightChildOrPrimitiveCount; ++i) {
        centroidBounds.grow(m_primitives[node.leftChildOrPrimitiveOffset + i].center);
    }
    glm::vec3 extent = centroidBounds.max - centroidBounds.min;
    
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > extent[axis]) axis = 2;

    float splitPos = centroidBounds.center()[axis];
    
    auto it = std::partition(m_primitives.begin() + node.leftChildOrPrimitiveOffset,
                             m_primitives.begin() + node.leftChildOrPrimitiveOffset + node.rightChildOrPrimitiveCount,
                             [axis, splitPos](const PrimitiveInfo& p) {
                                 return p.center[axis] < splitPos;
                             });

    uint32_t leftCount = std::distance(m_primitives.begin() + node.leftChildOrPrimitiveOffset, it);
    if (leftCount == 0 || leftCount == node.rightChildOrPrimitiveCount) {
        m_leafNodes++;
        return;
    }

    uint32_t leftChildIdx = m_nodes.size();
    m_nodes.push_back(BVHNode());
    uint32_t rightChildIdx = m_nodes.size();
    m_nodes.push_back(BVHNode());

    BVHNode& currentNode = m_nodes[nodeIdx];

    m_nodes[leftChildIdx].leftChildOrPrimitiveOffset = currentNode.leftChildOrPrimitiveOffset;
    m_nodes[leftChildIdx].rightChildOrPrimitiveCount = leftCount;

    m_nodes[rightChildIdx].leftChildOrPrimitiveOffset = currentNode.leftChildOrPrimitiveOffset + leftCount;
    m_nodes[rightChildIdx].rightChildOrPrimitiveCount = currentNode.rightChildOrPrimitiveCount - leftCount;

    currentNode.leftChildOrPrimitiveOffset = leftChildIdx;
    currentNode.rightChildOrPrimitiveCount = 0;

    updateNodeBounds(leftChildIdx);
    updateNodeBounds(rightChildIdx);

    subdivide(leftChildIdx, depth + 1);
    subdivide(rightChildIdx, depth + 1);
}