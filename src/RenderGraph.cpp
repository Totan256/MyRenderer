#include "RenderGraph.hpp"
#include <map>
#include <queue>
#include <vector>
#include <set>
#include <stdexcept>
#include <algorithm>



namespace rhi{
    
    bool RenderGraph::isWriteUsage(rhi::ResourceUsage usage) {
        using namespace rhi;
        return usage == ResourceUsage::StorageWrite || 
            usage == ResourceUsage::ColorAttachment || 
            usage == ResourceUsage::DepthStencilWrite || 
            usage == ResourceUsage::TransferDst;
    }

    std::vector<uint32_t> RenderGraph::getSortPasses(std::vector<uint32_t> passIndices){
        size_t numPasses = m_logicalNodes.size();
        std::vector <std::set<uint32_t>> adj(numPasses);
        std::vector<uint32_t> inDegree(numPasses, 0);
        for (size_t resIdx = 0; resIdx < m_resourceRegistry.size(); ++resIdx){
            const auto& reg = m_resourceRegistry[resIdx];
            struct Access { uint32_t passIdx; rhi::ResourceUsage usage; };
            std::vector<Access> accesses;
            for (uint32_t p : reg.producers) accesses.push_back({p, rhi::ResourceUsage::StorageWrite}); // 代表として
            
            std::sort(accesses.begin(), accesses.end(), [](const Access& a, const Access& b) {
                return a.passIdx < b.passIdx;
            });

            uint32_t lastWritePass = 0xFFFFFFFF;
            std::vector<uint32_t> readsSinceLastWrite;

            for (const auto& access : accesses) {
                if (isWriteUsage(access.usage)) {
                    // WAW (Write-After-Write): 前の書き込みが終わってから書く
                    if (lastWritePass != 0xFFFFFFFF) {
                        if (adj[lastWritePass].insert(access.passIdx).second) {
                            inDegree[access.passIdx]++;
                        }
                    }
                    // WAR (Write-After-Read): 前の読み込みがすべて終わってから上書きする
                    for (uint32_t readPass : readsSinceLastWrite) {
                        if (adj[readPass].insert(access.passIdx).second) {
                            inDegree[access.passIdx]++;
                        }
                    }
                    readsSinceLastWrite.clear();
                    lastWritePass = access.passIdx;
                } else {
                    // RAW (Read-After-Write): 書き込みが終わってから読み込む
                    if (lastWritePass != 0xFFFFFFFF) {
                        if (adj[lastWritePass].insert(access.passIdx).second) {
                            inDegree[access.passIdx]++;
                        }
                    }
                    readsSinceLastWrite.push_back(access.passIdx);
                }
            }
        }
        std::queue<uint32_t> queue;
        for (uint32_t i : passIndices) {
            if (inDegree[i] == 0) {
                queue.push(i);
            }
        }
        std::vector<uint32_t> sortedPasses;
        while (!queue.empty()) {
            uint32_t u = queue.front();
            queue.pop();
            sortedPasses.push_back(u);
            for (uint32_t v : adj[u]) {
                inDegree[v]--;
                if (inDegree[v] == 0) queue.push(v);
            }
        }
        if (sortedPasses.size() != passIndices.size()) {
            throw std::runtime_error("RenderGraph contains a cycle!");
        }
        return sortedPasses;
    }

    void RenderGraph::calculateLifetimes(const std::vector<uint32_t>& sortedPassIndices) {
        if (!sortedPassIndices.empty()) return;
        // 1. 全リソースのライフタイムを初期化
        // m_resourceRegistry のサイズ分だけライフタイム情報を確保
        m_resourceLifetimes.assign(m_resourceRegistry.size(), {0xFFFFFFFF, 0});
        // 2. ソートされた実行順序でパスを走査
        for (uint32_t executionIndex = 0; executionIndex < (uint32_t)sortedPassIndices.size(); ++executionIndex) {
            uint32_t passIdx = sortedPassIndices[executionIndex];
            const auto& node = m_logicalNodes[passIdx];
            // パスが使用する全リソースハンドルをチェック
            for (rhi::ResourceHandle h : node.resources) {
                auto& lifetime = m_resourceLifetimes[h];
                // 最初に使われたタイミングを記録
                if (lifetime.firstPass == 0xFFFFFFFF) {
                    lifetime.firstPass = executionIndex;
                }
                // 最後に使われたタイミングを更新
                lifetime.lastPass = executionIndex;
            }
        }
        // 3. インポートされたリソース（外部リソース）の扱い
        for (size_t i = 0; i < m_resourceRegistry.size(); ++i) {
            if (m_resourceRegistry[i].isImported) {
                // 外部リソース（スワップチェーン等）はグラフの最初から最後まで生存しているとみなす
                m_resourceLifetimes[i].firstPass = 0;
                m_resourceLifetimes[i].lastPass = (uint32_t)sortedPassIndices.size() - 1;
            }
        }
    }
}