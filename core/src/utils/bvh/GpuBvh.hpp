#pragma once
#include <memory>
#include "rhi/Device.hpp"
#include "core/RenderGraph.hpp"
#include "utils/StringHash.hpp"

struct GpuBvh {
    std::unique_ptr<rhi::Buffer> nodeBuffer;
    std::unique_ptr<rhi::Buffer> reorderedIndexBuffer;

    rhi::ResourceHandle hNodes = rhi::InvalidResource;
    rhi::ResourceHandle hReorderedIndices = rhi::InvalidResource;

    void importToGraph(rhi::RenderGraph& graph) {
        hNodes = graph.importResource(nodeBuffer.get(), "ModelBvhNodes"_hash);
        // 注意: ここで "ModelIdx" のハッシュを再配列版のバッファで上書き登録します
        hReorderedIndices = graph.importResource(reorderedIndexBuffer.get(), "ModelIdx"_hash); 
    }
};