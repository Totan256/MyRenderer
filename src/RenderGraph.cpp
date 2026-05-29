#include "RenderGraph.hpp"
#include <queue>
#include <stdexcept>
#include <iostream>

namespace rhi {

    bool RenderGraph::isWriteUsage(rhi::ResourceState state) {
        switch (state) {
            case ResourceState::StorageWrite:
            case ResourceState::ColorAttachment:
            case ResourceState::DepthStencilWrite:
            case ResourceState::TransferDst:
                return true;
            default:
                return false;
        }
    }

    std::vector<uint32_t> RenderGraph::getSortPasses(std::vector<uint32_t> passIndices) {
        // パス間の依存関係（DAG）を構築
        std::vector<std::vector<uint32_t>> adj(m_passes.size());
        std::vector<uint32_t> inDegree(m_passes.size(), 0);

        for (uint32_t passIdx : passIndices) {
            const auto& pass = m_passes[passIdx];
            
            // このパスが読み込むリソースについて、それを書き込む（生産する）パスへの依存を張る
            for (size_t i = 0; i < pass->getResourceHandles().size(); ++i) {
                ResourceHandle h = pass->getResourceHandles()[i];
                ResourceState state = pass->getRequirements()[i].state;
                
                if (!isWriteUsage(state)) {
                    for (uint32_t producerIdx : m_resourceRegistry[h].producers) {
                        if (producerIdx != passIdx) { // 自己ループの回避
                            adj[producerIdx].push_back(passIdx);
                            inDegree[passIdx]++;
                        }
                    }
                }
            }
        }

        // Kahnのアルゴリズムによるトポロジカルソート
        std::queue<uint32_t> q;
        for (uint32_t passIdx : passIndices) {
            if (inDegree[passIdx] == 0) {
                q.push(passIdx);
            }
        }

        std::vector<uint32_t> sortedIndices;
        while (!q.empty()) {
            uint32_t u = q.front();
            q.pop();
            sortedIndices.push_back(u);

            for (uint32_t v : adj[u]) {
                inDegree[v]--;
                if (inDegree[v] == 0) {
                    q.push(v);
                }
            }
        }

        if (sortedIndices.size() != passIndices.size()) {
            throw std::runtime_error("RenderGraph compilation failed: cyclic dependency detected!");
        }

        return sortedIndices;
    }

    void RenderGraph::calculateLifetimes(const std::vector<uint32_t>& sortedPassIndices) {
        m_resourceLifetimes.clear();
        m_resourceLifetimes.resize(m_resourceRegistry.size(), {0xFFFFFFFF, 0});

        // 実行順序（ソート済みインデックス）に基づいて寿命を計算
        for (uint32_t i = 0; i < sortedPassIndices.size(); ++i) {
            uint32_t passIdx = sortedPassIndices[i];
            const auto& pass = m_passes[passIdx];
            
            for (ResourceHandle h : pass->getResourceHandles()) {
                if (m_resourceLifetimes[h].firstPass == 0xFFFFFFFF) {
                    m_resourceLifetimes[h].firstPass = i;
                }
                m_resourceLifetimes[h].lastPass = i;
            }
        }
    }
}