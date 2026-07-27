#pragma once
#include <memory>
#include "BVHTypes.hpp"
#include "BVHBuilder.hpp"
#include "GpuBvh.hpp"
// ModelBuilder と同様に UploadManager を利用
namespace rhi { class UploadManager; }

class BVHManager {
public:
    static std::unique_ptr<GpuBvh> buildAndEnqueue(
        rhi::Device& device,
        rhi::UploadManager* uploadManager,
        const CpuModelData& cpuData,
        std::unique_ptr<IBVHBuilder> builder)
    {
        // 1. CPU上でBVHを構築
        CpuBvhData bvhData = builder->build(cpuData);
        
        // 2. プロファイリング情報の出力
        bvhData.stats.printToConsole();

        // 3. GPUバッファの作成
        auto gpuBvh = std::make_unique<GpuBvh>();
        
        size_t nodeBufferSize = bvhData.nodes.size() * sizeof(BVHNode);
        gpuBvh->nodeBuffer = device.createBuffer({
            nodeBufferSize, 
            rhi::BufferUsageFlags::StorageBuffer | rhi::BufferUsageFlags::TransferDst
        });

        size_t indexBufferSize = bvhData.reorderedIndices.size() * sizeof(uint32_t);
        gpuBvh->reorderedIndexBuffer = device.createBuffer({
            indexBufferSize, 
            rhi::BufferUsageFlags::StorageBuffer | rhi::BufferUsageFlags::TransferDst | rhi::BufferUsageFlags::IndexBuffer
        });

        // 4. 転送キューへ登録
        uploadManager->enqueueBufferUpload(gpuBvh->nodeBuffer.get(), bvhData.nodes.data(), nodeBufferSize);
        uploadManager->enqueueBufferUpload(gpuBvh->reorderedIndexBuffer.get(), bvhData.reorderedIndices.data(), indexBufferSize);

        return gpuBvh;
    }
};