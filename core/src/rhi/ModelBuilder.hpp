#include <memory>
#include <vector>
#include "Device.hpp"
#include "utils/ModelImporter.hpp"


struct GpuModel {
    std::unique_ptr<rhi::Buffer> positionBuffer;
    std::unique_ptr<rhi::Buffer> attributeBuffer;
    std::unique_ptr<rhi::Buffer> indexBuffer;
    std::vector<CpuSubMesh> subMeshes;

    rhi::ResourceHandle hPosition = rhi::InvalidResource;
    rhi::ResourceHandle hAttribute = rhi::InvalidResource;
    rhi::ResourceHandle hIndex = rhi::InvalidResource;

    // グラフに自分の中身を一括インポートするヘルパー関数
    void importToGraph(rhi::RenderGraph& graph) {
        // ※StringHashはモデルのID等でユニークにするとなお良し
        hPosition = graph.importResource(positionBuffer.get(), "ModelPos"_hash);
        hAttribute = graph.importResource(attributeBuffer.get(), "ModelAttr"_hash);
        hIndex = graph.importResource(indexBuffer.get(), "ModelIdx"_hash);
    }
};

class ModelBuilder {
public:
    // CpuModelDataを受け取り、バッファ作成とアップロードキューイングを一括で行う
    static std::unique_ptr<GpuModel> buildAndEnqueue(
        rhi::Device& device, 
        rhi::UploadManager* uploadManager, 
        const CpuModelData& cpuData) 
    {
        auto gpuModel = std::make_unique<GpuModel>();
        gpuModel->subMeshes = cpuData.subMeshes;

        // 1. バッファの作成
        gpuModel->positionBuffer = device.createBuffer({cpuData.positions.size() * sizeof(VertexPosition), rhi::BufferUsageFlags::StorageBuffer | rhi::BufferUsageFlags::TransferDst});
        gpuModel->attributeBuffer = device.createBuffer({cpuData.attributes.size() * sizeof(VertexAttributes), rhi::BufferUsageFlags::StorageBuffer | rhi::BufferUsageFlags::TransferDst});
        gpuModel->indexBuffer = device.createBuffer({cpuData.indices.size() * sizeof(uint32_t), rhi::BufferUsageFlags::IndexBuffer | rhi::BufferUsageFlags::TransferDst | rhi::BufferUsageFlags::StorageBuffer});

        // 2. UploadManager への一括エンキュー (この時点では転送されない)
        uploadManager->enqueueBufferUpload(gpuModel->positionBuffer.get(), cpuData.positions.data(), cpuData.positions.size() * sizeof(VertexPosition));
        uploadManager->enqueueBufferUpload(gpuModel->attributeBuffer.get(), cpuData.attributes.data(), cpuData.attributes.size() * sizeof(VertexAttributes));
        uploadManager->enqueueBufferUpload(gpuModel->indexBuffer.get(), cpuData.indices.data(), cpuData.indices.size() * sizeof(uint32_t));

        return gpuModel;
    }
};